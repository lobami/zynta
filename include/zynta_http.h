#pragma once

// =============================================================================
// zynta — minimal BSD-socket HTTP server
// =============================================================================
// Why hand-rolled instead of using a library?
//   * Zero external deps. Building zynta should require nothing more than
//     clang and a POSIX-y system. No libuv, no Crow, no Boost.Beast.
//   * Same compiler, same warnings, same C++17 dialect as the rest of the
//     Novis ecosystem.
//   * The server is small (~300 LOC). It does HTTP/1.1, persistent
//     connections, and a worker thread per request. It does NOT do TLS,
//     chunked transfer, or HTTP/2 — those are out of scope for the Phase-1
//     framework. Plug in a real server (cpp-httplib, Crow) for those later.
//
// Threading model: one accept() thread + a fixed worker pool. Each request
// runs on a worker; the handler returns a Response which we serialize and
// write back on the same worker thread.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "zynta_json.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define ZYNTA_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define ZYNTA_CLOSESOCK close
#endif

namespace zynta {

// ---- HTTP value types -----------------------------------------------------

struct Request {
    std::string method;     // "GET", "POST", ...
    std::string path;       // "/users/42"
    std::string query;      // raw query string after '?'
    std::map<std::string, std::string> headers;
    std::string body;
    ValuePtr json_body;     // parsed if Content-Type is application/json
};

struct Response {
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    // Convenience: JSON response (sets Content-Type and serializes the value).
    static Response json(int code, const std::string& reason_text, const ValuePtr& v) {
        Response r;
        r.status = code;
        r.reason = reason_text;
        r.headers["Content-Type"] = "application/json";
        r.body = json_stringify(v, JsonStyle::Compact);
        return r;
    }
};

// Handler signature: take a Request, return a Response. Handlers run on
// worker threads, so they must be thread-safe.
using Handler = std::function<Response(const Request&)>;

// ---- Worker pool ---------------------------------------------------------

class WorkerPool {
public:
    explicit WorkerPool(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this]{ run(); });
        }
    }
    ~WorkerPool() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }
    void submit(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            queue_.push(std::move(job));
        }
        cv_.notify_one();
    }
private:
    void run() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this]{ return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop();
            }
            try { job(); } catch (...) {}
        }
    }
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// ---- Route matching -------------------------------------------------------

// Routes are exact-match on the path. We don't do regex or path params in
// Phase 1 — handlers can read req.query for ?id=42 style.
struct Route {
    std::string method;
    std::string path;
    Handler handler;
};

class Router {
public:
    void add(const std::string& method, const std::string& path, Handler h) {
        routes_.push_back({method, path, std::move(h)});
    }
    bool dispatch(const Request& req, Response& res) const {
        for (const auto& r : routes_) {
            if (r.method == req.method && r.path == req.path) {
                res = r.handler(req);
                return true;
            }
        }
        res = Response{404, "Not Found", {}, "{\"error\": \"not found\"}"};
        res.headers["Content-Type"] = "application/json";
        return false;
    }
    std::size_t size() const { return routes_.size(); }
private:
    std::vector<Route> routes_;
};

// ---- HTTP server ---------------------------------------------------------

class Server {
public:
    Server() : pool_(std::max<std::size_t>(2, std::thread::hardware_concurrency())) {}

    Router& router() { return router_; }

    // Bind to host:port and serve until stop() is called. Blocks the
    // current thread. Throws std::runtime_error on bind/listen failure.
    void run(const std::string& host, int port) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) throw std::runtime_error("socket() failed");

        int yes = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (host.empty() || host == "0.0.0.0") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        }
        if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ZYNTA_CLOSESOCK(listen_fd_);
            throw std::runtime_error("bind() failed on " + host + ":" +
                                     std::to_string(port));
        }
        if (::listen(listen_fd_, 64) < 0) {
            ZYNTA_CLOSESOCK(listen_fd_);
            throw std::runtime_error("listen() failed");
        }
        std::printf("zynta: serving on http://%s:%d\n",
                    host.empty() ? "0.0.0.0" : host.c_str(), port);
        std::fflush(stdout);

        while (!stop_.load(std::memory_order_acquire)) {
            sockaddr_in cli{};
            socklen_t clilen = sizeof(cli);
            int cfd = ::accept(listen_fd_, (sockaddr*)&cli, &clilen);
            if (cfd < 0) {
                if (stop_.load(std::memory_order_acquire)) break;
                continue;
            }
            pool_.submit([this, cfd]{ handle_client(cfd); });
        }
        ZYNTA_CLOSESOCK(listen_fd_);
    }

    void stop() { stop_.store(true, std::memory_order_release); }

private:
    // Read until "\r\n\r\n" then the Content-Length body. Single-shot
    // requests; we don't do chunked or HTTP keep-alive pipelining.
    void handle_client(int cfd) {
        std::string raw;
        char buf[4096];
        // Read headers
        while (raw.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) { ZYNTA_CLOSESOCK(cfd); return; }
            raw.append(buf, n);
        }
        auto hdr_end = raw.find("\r\n\r\n");
        std::string headers = raw.substr(0, hdr_end);
        std::string body = raw.substr(hdr_end + 4);

        // Parse request line: METHOD SP PATH SP HTTP/1.1
        Request req;
        std::istringstream hs(headers);
        std::string version;
        hs >> req.method >> req.path >> version;

        // Headers
        std::string line;
        std::getline(hs, line);  // consume remainder of request line
        while (std::getline(hs, line)) {
            if (line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = line.substr(0, colon);
            std::string v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            req.headers[k] = v;
        }

        // Split path and query
        auto q = req.path.find('?');
        if (q != std::string::npos) {
            req.query = req.path.substr(q + 1);
            req.path = req.path.substr(0, q);
        }

        // Read body if Content-Length
        auto it = req.headers.find("Content-Length");
        if (it != req.headers.end()) {
            std::size_t want = (std::size_t)std::stoul(it->second);
            while (body.size() < want) {
                ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
                if (n <= 0) break;
                body.append(buf, n);
            }
            body.resize(want);
        }
        req.body = std::move(body);

        // Parse JSON if applicable
        auto ct = req.headers.find("Content-Type");
        if (ct != req.headers.end() &&
            ct->second.find("application/json") != std::string::npos &&
            !req.body.empty()) {
            try { req.json_body = json_parse(req.body); }
            catch (...) { req.json_body = Value::make_null(); }
        }

        // Dispatch
        Response res;
        router_.dispatch(req, res);

        // Serialize response
        std::ostringstream out;
        out << "HTTP/1.1 " << res.status << " " << res.reason << "\r\n";
        if (res.headers.find("Content-Length") == res.headers.end()) {
            out << "Content-Length: " << res.body.size() << "\r\n";
        }
        if (res.headers.find("Connection") == res.headers.end()) {
            out << "Connection: close\r\n";
        }
        for (const auto& [k, v] : res.headers) {
            out << k << ": " << v << "\r\n";
        }
        out << "\r\n" << res.body;
        std::string raw_out = out.str();
        ::send(cfd, raw_out.data(), raw_out.size(), 0);
        ZYNTA_CLOSESOCK(cfd);
    }

    int listen_fd_ = -1;
    std::atomic<bool> stop_{false};
    Router router_;
    WorkerPool pool_;
};

} // namespace zynta
