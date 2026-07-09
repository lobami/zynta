#!/usr/bin/env bash
# =============================================================================
# zynta installer
# =============================================================================
# curl-installable:  curl -fsSL .../install.sh | sh
#
# What this does:
#   1. Detect OS + arch.
#   2. Install build deps (clang++17, sqlite3, libpq if macOS).
#   3. Clone zynta + novis to ~/.local/share/zynta/ (or use existing).
#   4. Build both from source.
#   5. Drop `zynta` and `novis` in ~/.local/bin/ (override with PREFIX).
#   6. Print next-step instructions.
#
# Re-runnable: detects an existing install and upgrades in place.
# Idempotent: safe to run multiple times.
# Uninstall:   curl ... | sh -s -- --uninstall
# Force:       curl ... | sh -s -- --force
# Channel:     curl ... | sh -s -- --channel main    (or a tag/branch)

set -euo pipefail

ZYNTA_REPO="${ZYNTA_REPO:-https://github.com/lobami/zynta.git}"
NOVIS_REPO="${NOVIS_REPO:-https://github.com/lobami/novis.git}"
PREFIX="${PREFIX:-$HOME/.local}"
SHARE="$PREFIX/share/zynta"
BIN_DIR="$PREFIX/bin"
CHANNEL="${CHANNEL:-main}"
FORCE=0
UNINSTALL=0

# ---- arg parsing ----------------------------------------------------------
for arg in "$@"; do
    case "$arg" in
        --uninstall) UNINSTALL=1 ;;
        --force|-f)  FORCE=1 ;;
        --channel=*) CHANNEL="${arg#*=}" ;;
        --channel)   shift; CHANNEL="${1:-main}" ;;
        --prefix=*)   PREFIX="${arg#*=}" ;;
        --help|-h)
            sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)  echo "zynta-install: unknown arg '$arg'" >&2; exit 2 ;;
    esac
done

# ---- helpers --------------------------------------------------------------
say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarn:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"; }

detect_os() {
    case "$(uname -s)" in
        Darwin) echo "macos" ;;
        Linux)  echo "linux" ;;
        *)      die "unsupported OS: $(uname -s)" ;;
    esac
}

# ---- uninstall ------------------------------------------------------------
if [[ $UNINSTALL -eq 1 ]]; then
    say "removing zynta from $PREFIX"
    rm -rf "$SHARE"
    rm -f "$BIN_DIR/zynta"
    rm -f "$BIN_DIR/novis"
    say "done. Restart your shell to refresh PATH."
    exit 0
fi

# ---- preflight ------------------------------------------------------------
say "zynta installer"
say "  prefix:        $PREFIX"
say "  channel:       $CHANNEL"
say "  zynta repo:    $ZYNTA_REPO"
say "  novis repo:    $NOVIS_REPO"

mkdir -p "$SHARE" "$BIN_DIR"

OS=$(detect_os)
say "  detected OS:   $OS"

# ---- build deps ------------------------------------------------------------
if [[ $OS == "macos" ]]; then
    if ! command -v brew >/dev/null 2>&1; then
        die "Homebrew is required on macOS. Install from https://brew.sh"
    fi
    need clang++
    for pkg in cmake sqlite3 libpq mysql-client; do
        if ! brew list "$pkg" >/dev/null 2>&1; then
            say "installing build dep: $pkg"
            brew install "$pkg" >/dev/null
        fi
    done
else
    if command -v apt-get >/dev/null 2>&1; then
        say "ensuring build deps (apt)"
        sudo apt-get update -qq
        sudo apt-get install -y build-essential clang \
            libsqlite3-dev libpq-dev default-libmysqlclient-dev pkg-config
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc-c++ clang sqlite-devel \
            postgresql-devel mysql-devel pkgconfig
    else
        warn "could not detect a package manager; ensure clang++, sqlite3, libpq, and libmysqlclient are installed"
    fi
fi

need git
need clang++

# ---- clone / update zynta --------------------------------------------------
if [[ -d "$SHARE/zynta/.git" ]]; then
    say "updating existing zynta checkout"
    (cd "$SHARE/zynta" && git fetch --tags origin && git checkout "$CHANNEL" && git pull --ff-only) || \
        warn "zynta update failed; using existing checkout"
else
    say "cloning zynta ($CHANNEL)"
    git clone --branch "$CHANNEL" --depth 1 "$ZYNTA_REPO" "$SHARE/zynta"
fi

# ---- clone / update novis --------------------------------------------------
if [[ -d "$SHARE/novis/.git" ]]; then
    say "updating existing novis checkout"
    (cd "$SHARE/novis" && git fetch --tags origin && git pull --ff-only) || true
else
    say "cloning novis (the language runtime zynta uses)"
    git clone --depth 1 "$NOVIS_REPO" "$SHARE/novis"
fi

# ---- build zynta -----------------------------------------------------------
say "building zynta"
(cd "$SHARE/zynta" && make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" BUILD_TYPE=Release)

# ---- build novis with zynta integration -----------------------------------
say "building novis (with zynta integration)"
(cd "$SHARE/novis" && make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)")

# ---- link ------------------------------------------------------------------
ln -sf "$SHARE/zynta/bin/zynta"     "$BIN_DIR/zynta"
ln -sf "$SHARE/novis/novis"          "$BIN_DIR/novis"

# ---- PATH hint -------------------------------------------------------------
case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        warn "$BIN_DIR is not on your PATH"
        warn "add this to your ~/.zshrc (or ~/.bashrc):"
        warn "    export PATH=\"$BIN_DIR:\$PATH\""
        ;;
esac

# ---- done ------------------------------------------------------------------
say "zynta is installed"
say "next: run \`zynta new myapp\` to scaffold a project"
say "      or visit https://github.com/lobami/zynta for examples"
