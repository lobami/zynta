// =============================================================================
// zynta CLI
// =============================================================================
// Small command-line tool that does the user-facing side of the framework:
//   * zynta new <name>   — scaffold a new project
//   * zynta dev          — run the local dev server
//   * zynta build        — compile the project to a native binary
//   * zynta help         — show all commands
//
// The CLI does not link against libzynta; it just templates a directory
// and shells out to `novis zynta-serve` for `zynta dev`.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void die(const std::string& msg) {
    std::fprintf(stderr, "\033[1;31merror:\033[0m %s\n", msg.c_str());
    std::exit(1);
}

void say(const std::string& msg) {
    std::printf("\033[1;34m==>\033[0m %s\n", msg.c_str());
}

std::string read_template(const std::string& path) {
    std::ifstream f(path);
    if (!f) die("missing template: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f) die("cannot write " + path.string());
    f << body;
}

// Render a template by replacing {{var}} placeholders. Keep it simple —
// no escaping, no conditionals, no loops. Templates live next to the
// zynta binary in ../templates/<name>.tpl (relative to the zynta
// source root). The installer symlinks the binary; the templates are
// discovered at runtime via ZYNTA_TEMPLATES or the binary's location.
std::string render(const std::string& body, const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(body.size());
    std::size_t i = 0;
    while (i < body.size()) {
        if (i + 1 < body.size() && body[i] == '{' && body[i+1] == '{') {
            auto j = body.find("}}", i + 2);
            if (j == std::string::npos) { out += body[i++]; continue; }
            std::string key = body.substr(i + 2, j - (i + 2));
            auto it = vars.find(key);
            if (it != vars.end()) out += it->second;
            else out += "{{" + key + "}}";
            i = j + 2;
        } else {
            out += body[i++];
        }
    }
    return out;
}

// Module-level: the path of the running binary, set in main(). We keep
// it as a static so template_dir() can find templates relative to the
// binary location (useful for both the source-tree dev case and the
// installed case where zynta lives in <prefix>/bin/ and templates in
// <prefix>/share/zynta/templates/).
static fs::path g_argv0;

fs::path template_dir() {
    if (const char* t = std::getenv("ZYNTA_TEMPLATES")) return t;
    // Search order:
    //   1. ZYNTA_HOME/share/zynta/templates (set by the installer)
    //   2. /usr/local/share/zynta/templates (Homebrew x86_64)
    //   3. /opt/homebrew/share/zynta/templates (Homebrew arm64)
    //   4. argv[0]'s parent_path().parent_path()/templates (dev-mode
    //      when running `./bin/zynta` from the source tree)
    //   5. argv[0]'s parent_path().parent_path()/share/zynta/templates
    //      (running from a `bin/` install)
    auto try_candidates = [](const fs::path& argv0) -> fs::path {
        if (const char* h = std::getenv("ZYNTA_HOME")) {
            fs::path cand = fs::path(h) / "share" / "zynta" / "templates";
            if (fs::exists(cand)) return cand;
        }
        for (const auto& base : {
                fs::path("/usr/local/share/zynta/templates"),
                fs::path("/opt/homebrew/share/zynta/templates"),
        }) {
            if (fs::exists(base)) return base;
        }
        // argv[0] is the path the user invoked (e.g. "./bin/zynta" or
        // "/usr/local/bin/zynta"). The templates live in
        // <argv0-parent>/../templates for dev-mode or
        // <argv0-parent>/../share/zynta/templates for install-mode.
        if (!argv0.empty()) {
            fs::path me = fs::absolute(argv0);
            // strip the binary name + bin/ prefix
            fs::path root = me.parent_path().parent_path();
            fs::path dev_t = root / "templates";
            if (fs::exists(dev_t)) return dev_t;
            fs::path inst_t = root / "share" / "zynta" / "templates";
            if (fs::exists(inst_t)) return inst_t;
        }
        return fs::path();
    };
    fs::path p = try_candidates(g_argv0);
    if (!p.empty()) return p;
    die("could not locate templates dir; set ZYNTA_TEMPLATES=/path/to/templates or ZYNTA_HOME=/path/to/zynta/install");
}

int cmd_new(int argc, char** argv) {
    if (argc < 1) die("usage: zynta new <name> [--db=sqlite|postgres|mysql]");
    std::string name = argv[0];
    std::string db = "sqlite";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--db=", 0) == 0) db = a.substr(5);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: zynta new <name> [--db=sqlite|postgres|mysql]\n");
            return 0;
        } else {
            die("unknown flag: " + a);
        }
    }
    fs::path target = name;
    if (fs::exists(target)) die("directory already exists: " + name);

    std::map<std::string, std::string> vars{
        {"name", name},
        {"db", db},
        {"db_url", db == "sqlite" ? "sqlite://./" + name + ".db"
                  : db == "postgres" ? "postgresql://user:pass@localhost:5432/" + name
                  : "mysql://user:pass@localhost:3306/" + name},
    };

    fs::path td = template_dir();
    say("scaffolding project '" + name + "' in ./" + name);
    fs::create_directories(target);

    auto emit = [&](const std::string& tpl, const std::string& out) {
        std::string body = read_template((td / tpl).string());
        write_file(target / out, render(body, vars));
    };
    emit("app.zynta.tpl",        "app.zynta");
    emit("zynta.toml.tpl",       "zynta.toml");
    emit("README.md.tpl",        "README.md");
    emit("gitignore.tpl",        ".gitignore");
    emit("schema.sql.tpl",       "schema.sql");

    say("done. next steps:");
    std::printf("    cd %s\n", name.c_str());
    std::printf("    zynta dev          # run on http://127.0.0.1:8080\n");
    std::printf("    zynta build        # compile to a native binary\n");
    return 0;
}

int cmd_dev(int /*argc*/, char** /*argv*/) {
    // shell out to `novis zynta-serve app.zynta`. We assume `novis` is on PATH.
    if (!fs::exists("app.zynta")) die("no app.zynta in current directory");
    say("starting zynta dev server (novis zynta-serve app.zynta)");
    return std::system("novis zynta-serve app.zynta");
}

int cmd_build(int argc, char** argv) {
    // `zynta build` produces a native binary at ./build/app using the novis
    // native backend. We delegate to `novis build app.zynta`.
    if (!fs::exists("app.zynta")) die("no app.zynta in current directory");
    (void)argc; (void)argv;
    say("compiling app.zynta to a native binary");
    return std::system("novis build app.zynta -o build/app");
}

int cmd_help() {
    std::printf("zynta — the web framework for novis\n");
    std::printf("\n");
    std::printf("usage:\n");
    std::printf("    zynta new <name> [--db=sqlite|postgres|mysql]\n");
    std::printf("        scaffold a new project in ./<name>\n");
    std::printf("    zynta dev\n");
    std::printf("        run the local dev server (calls novis zynta-serve)\n");
    std::printf("    zynta build\n");
    std::printf("        compile app.zynta to a native binary via novis build\n");
    std::printf("    zynta help\n");
    std::printf("        show this message\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1) return cmd_help();
    g_argv0 = argv[0];
    if (argc < 2) return cmd_help();
    std::string cmd = argv[1];
    if (cmd == "new")   return cmd_new(argc - 2, argv + 2);
    if (cmd == "dev")   return cmd_dev(argc - 2, argv + 2);
    if (cmd == "build") return cmd_build(argc - 2, argv + 2);
    if (cmd == "help" || cmd == "--help" || cmd == "-h") return cmd_help();
    std::fprintf(stderr, "zynta: unknown command '%s' (try 'zynta help')\n", cmd.c_str());
    return 2;
}
