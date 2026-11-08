#!/usr/bin/env bash
# Installs what building the browser player from source needs on Ubuntu / Debian: the .NET SDK the release builds
# it with and that SDK's wasm-tools workload, which brings the Emscripten, LLVM and Node.js the player compiles with.
# The SDK goes into ~/.dotnet-web, beside the .NET the rest of the build uses (install_dotnet.sh), and CMake takes it
# from there (cmake/web_player.cmake). Emscripten runs on the system's Python, which has to be 3.10 or newer.
# --cloudflared also installs cloudflared, Cloudflare's tunnel tool, from its latest release: the Deploy window's
# Share Public Link runs it. Editor packages carry the browser player, so deploying from one needs none of this.
# Run it as your own user (bash install_web_tools.sh [--cloudflared]): the SDK goes into your home folder, and the
# script asks for sudo itself for apt.
set -euo pipefail

USAGE="Usage: bash install_web_tools.sh [--cloudflared]"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASE_WORKFLOW="$REPO_DIR/.github/workflows/release.yml"
WEB_DOTNET_DIR="$HOME/.dotnet-web"   # cmake/web_player.cmake
WEB_DOTNET="$WEB_DOTNET_DIR/dotnet"
PYTHON_MIN="3.10"                    # cmake/web_player.cmake
CLOUDFLARED_RELEASES_URL="https://github.com/cloudflare/cloudflared/releases/latest/download"

install_cloudflared=0
for argument in "$@"; do
    case "$argument" in
        --cloudflared) install_cloudflared=1 ;;
        -h|--help) echo "$USAGE"; exit 0 ;;
        *) echo "$USAGE" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script as your own user, not with sudo: it installs .NET into your home folder." >&2
    exit 1
fi

# The value of an env entry of the release workflow, so a source build uses what the release packages build with.
read_release_pin() {
    local value
    value="$(tr -d '\r' < "$RELEASE_WORKFLOW" | sed -n "s/^  $1: '\(.*\)'\$/\1/p")"
    if [ -z "$value" ]; then
        echo "$RELEASE_WORKFLOW has no $1." >&2
        exit 1
    fi
    echo "$value"
}

# Whether WEB_DOTNET_DIR holds the SDK version $1.
has_web_sdk() {
    [ -x "$WEB_DOTNET" ] && "$WEB_DOTNET" --list-sdks 2>/dev/null | grep -qF "$1 ["
}

# Whether the python3 on the PATH is new enough for Emscripten.
has_new_enough_python() {
    python3 -c "import sys; sys.exit(sys.version_info[:2] < tuple(map(int, sys.argv[1].split('.'))))" "$PYTHON_MIN"
}

install_apt_packages() {
    # .NET needs ICU (any version); libicu-dev brings the one this Ubuntu release has.
    local packages=(curl ca-certificates python3 ninja-build)
    if ! ldconfig -p | grep 'libicuuc\.so' >/dev/null; then
        packages+=(libicu-dev)
    fi
    sudo apt-get update
    sudo apt-get install -y "${packages[@]}"
}

install_web_sdk() {
    local sdk_version="$1"
    if has_web_sdk "$sdk_version"; then
        return
    fi
    # An interrupted install leaves the version folder, which the installer takes as done and never completes.
    rm -rf "$WEB_DOTNET_DIR/sdk/$sdk_version"
    local download_dir
    download_dir="$(mktemp -d)"
    curl -fsSL -o "$download_dir/dotnet-install.sh" https://dot.net/v1/dotnet-install.sh
    bash "$download_dir/dotnet-install.sh" --version "$sdk_version" --install-dir "$WEB_DOTNET_DIR" --no-path
    rm -rf "$download_dir"
    if ! has_web_sdk "$sdk_version"; then
        echo "The .NET SDK $sdk_version is not in $WEB_DOTNET_DIR after the install." >&2
        exit 1
    fi
}

# The workload comes from the manifests the SDK version carries, so it brings the same Emscripten as the release's.
install_wasm_tools() {
    "$WEB_DOTNET" workload install wasm-tools --skip-manifest-update
}

install_cloudflared_package() {
    local download_dir
    download_dir="$(mktemp -d)"
    # apt reads a local package as its own user.
    chmod 755 "$download_dir"
    curl -fL -o "$download_dir/cloudflared.deb" \
        "$CLOUDFLARED_RELEASES_URL/cloudflared-linux-$(dpkg --print-architecture).deb"
    sudo apt-get install -y "$download_dir/cloudflared.deb"
    rm -rf "$download_dir"
}

sdk_version="$(read_release_pin WEB_DOTNET_SDK_VERSION)"
install_apt_packages
install_web_sdk "$sdk_version"
install_wasm_tools
if [ "$install_cloudflared" -eq 1 ]; then
    install_cloudflared_package
fi
echo "Done: .NET SDK $sdk_version with the wasm-tools workload in $WEB_DOTNET_DIR."
if ! has_new_enough_python; then
    echo "Emscripten needs Python $PYTHON_MIN or newer, and python3 is $(python3 --version 2>&1 | cut -d' ' -f2)." >&2
    echo "Install a newer one and configure with -DUNRAVEL_WEB_PYTHON=<its python3>." >&2
fi
echo "Configure your build tree again (for example: cmake -B build/Release) and look for 'Web player: on'."
