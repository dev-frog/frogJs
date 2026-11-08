#!/bin/sh
# Install FrogJS on macOS or Linux (on Windows: inside WSL).
#
#   curl -fsSL https://raw.githubusercontent.com/dev-frog/frogJs/main/install.sh | sh
#
# It installs V8 and libuv with Homebrew if they are missing, builds FrogJS from
# source, puts the frogjs command in ~/.frogjs/bin, and adds that to your PATH.
# It builds instead of downloading a binary because frogjs links to the V8 on
# your machine, and a binary built for another V8 version wouldn't start.
#
# Settings, as environment variables:
#   FROGJS_VERSION         a tag such as v1.0.0, or "main" (default: the latest release)
#   FROGJS_DIR             where to install (default: ~/.frogjs)
#   FROGJS_NO_MODIFY_PATH  set to 1 to leave your shell profile alone
#   FROGJS_SOURCE          build this local checkout instead of downloading
#   V8_PATH, LIBUV_PATH    use these installs instead of Homebrew's
#
# Remove it with:  rm -r ~/.frogjs  (and the FrogJS lines in your shell profile)

set -eu

REPO="dev-frog/frogJs"
DIR="${FROGJS_DIR:-$HOME/.frogjs}"

say() { printf '%s\n' "==> $*"; }
fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}
need() { command -v "$1" > /dev/null 2>&1 || fail "$2"; }

case "$(uname -s)" in
    Darwin) OS=macos ;;
    Linux) OS=linux ;;
    MINGW* | MSYS* | CYGWIN*)
        fail "Windows isn't supported directly. Install WSL ('wsl --install' in PowerShell), then run this inside it." ;;
    *) fail "unsupported system: $(uname -s)" ;;
esac

if [ "$OS" = linux ]; then
    BUILD_TOOLS_HINT="install a C++ compiler and make (Debian/Ubuntu: sudo apt-get install build-essential)"
else
    BUILD_TOOLS_HINT="install the Xcode command line tools: xcode-select --install"
fi
need make "make is missing: $BUILD_TOOLS_HINT"
need g++ "a C++ compiler is missing: $BUILD_TOOLS_HINT"
need tar "tar is missing"

# ---- Dependencies ----

if [ -z "${V8_PATH:-}" ]; then
    command -v brew > /dev/null 2>&1 || fail "FrogJS needs V8 and libuv, which it gets from Homebrew.
Install Homebrew from https://brew.sh and run this again, or set V8_PATH and LIBUV_PATH."
    # Only what is missing: an installed V8 stays at its version
    for formula in v8 libuv; do
        if brew list --versions "$formula" > /dev/null 2>&1; then
            say "$formula is already installed"
        else
            say "Installing $formula with Homebrew"
            brew install "$formula"
        fi
    done
fi

# ---- Source ----

WORK="$(mktemp -d)"
trap 'rm -r "$WORK"' EXIT

if [ -n "${FROGJS_SOURCE:-}" ]; then
    say "Using the source in $FROGJS_SOURCE"
    SRC="$WORK/source"
    mkdir -p "$SRC"
    (cd "$FROGJS_SOURCE" && tar -cf - --exclude ./build --exclude ./node_modules --exclude ./.git .) | tar -xf - -C "$SRC"
else
    need curl "curl is missing"
    VERSION="${FROGJS_VERSION:-}"
    if [ -z "$VERSION" ]; then
        VERSION="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" 2> /dev/null |
            sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n 1 || true)"
        [ -n "$VERSION" ] || VERSION=main
    fi
    case "$VERSION" in
        main) URL="https://github.com/$REPO/archive/refs/heads/main.tar.gz" ;;
        *) URL="https://github.com/$REPO/archive/refs/tags/$VERSION.tar.gz" ;;
    esac
    say "Downloading FrogJS $VERSION"
    curl -fsSL "$URL" -o "$WORK/source.tar.gz" || fail "could not download $URL"
    mkdir -p "$WORK/source"
    tar -xzf "$WORK/source.tar.gz" -C "$WORK/source" --strip-components 1
    SRC="$WORK/source"
fi

# ---- Build ----

JOBS="$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)"
say "Building (this takes a minute or two)"
if ! make -C "$SRC" -j "$JOBS" > "$WORK/build.log" 2>&1; then
    tail -n 30 "$WORK/build.log" >&2
    fail "the build failed. Please report it: https://github.com/$REPO/issues"
fi

# Check it before replacing anything that works
"$SRC/build/frogjs" -e "if (typeof require('fs').readFileSync !== 'function') process.exit(1)" ||
    fail "the new build doesn't run. Please report it: https://github.com/$REPO/issues"

mkdir -p "$DIR/bin"
# A new file, then a rename: a running frogjs keeps working
cp "$SRC/build/frogjs" "$DIR/bin/frogjs.new"
mv "$DIR/bin/frogjs.new" "$DIR/bin/frogjs"
cp "$SRC/LICENSE" "$DIR/LICENSE"
INSTALLED="$("$DIR/bin/frogjs" --version)"
say "Installed FrogJS $INSTALLED in $DIR/bin"

# ---- PATH ----

case ":$PATH:" in
    *":$DIR/bin:"*)
        say "$DIR/bin is already on your PATH"
        ;;
    *)
        LINE="export PATH=\"$DIR/bin:\$PATH\""
        case "${SHELL:-}" in
            */zsh) PROFILE="$HOME/.zshrc" ;;
            */bash)
                if [ "$OS" = macos ]; then PROFILE="$HOME/.bash_profile"; else PROFILE="$HOME/.bashrc"; fi
                ;;
            *) PROFILE="$HOME/.profile" ;;
        esac
        if [ "${FROGJS_NO_MODIFY_PATH:-}" = 1 ]; then
            say "Add this to your shell profile to use frogjs:"
            printf '    %s\n' "$LINE"
        else
            if ! grep -Fqs "$LINE" "$PROFILE"; then
                printf '\n# FrogJS\n%s\n' "$LINE" >> "$PROFILE"
                say "Added $DIR/bin to your PATH in $PROFILE"
            fi
            say "Open a new terminal, or run:"
            printf '    %s\n' "$LINE"
        fi
        ;;
esac

say "Done. Try: frogjs -p \"'Hello from ' + process.version\""
if [ -z "${V8_PATH:-}" ]; then
    say "After 'brew upgrade v8', run this installer again: frogjs is built for the V8 it found."
fi
