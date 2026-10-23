#!/usr/bin/env bash
# Installs the .NET 10 SDK that UnravelEngine builds with (the Android player needs 10) into ~/.dotnet with
# Microsoft's dotnet-install script, which also works where no apt feed carries .NET 10, such as Ubuntu 20.04.
# It makes that SDK the dotnet of every login: ~/.profile sets DOTNET_ROOT, which the editor's .NET host
# takes over the system folders, and puts it first on PATH for CMake and terminals. An install that was
# interrupted is removed and done again. The build trees under build/ forget the dotnet CMake found before,
# so their next build finds this one.
# Run it as your own user (bash install_dotnet.sh): .NET goes into your home folder, and the script asks for
# sudo itself for apt.
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script as your own user, not with sudo: it installs .NET into your home folder." >&2
    exit 1
fi

DOTNET_CHANNEL="10.0"   # cmake/android_player.cmake
DOTNET_DIR="$HOME/.dotnet"
DOTNET="$DOTNET_DIR/dotnet"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The SDK versions of the channel in DOTNET_DIR, newest last; empty when it has none.
list_channel_sdks() {
    if [ -x "$DOTNET" ]; then
        "$DOTNET" --list-sdks 2>/dev/null | grep "^${DOTNET_CHANNEL}\." | cut -d' ' -f1 || true
    fi
}

# .NET needs ICU (any version); libicu-dev brings the one this Ubuntu release has.
packages=(curl ca-certificates)
if ! ldconfig -p | grep 'libicuuc\.so' >/dev/null; then
    packages+=(libicu-dev)
fi
sudo apt-get update
sudo apt-get install -y "${packages[@]}"

if [ -z "$(list_channel_sdks)" ]; then
    # An interrupted install leaves version folders that the installer takes as done and never completes.
    if [ -d "$DOTNET_DIR" ]; then
        find "$DOTNET_DIR" -mindepth 2 -maxdepth 3 -type d -name "${DOTNET_CHANNEL}*" -prune -exec rm -rf {} +
    fi
    download_dir="$(mktemp -d)"
    curl -fsSL -o "$download_dir/dotnet-install.sh" https://dot.net/v1/dotnet-install.sh
    bash "$download_dir/dotnet-install.sh" --channel "$DOTNET_CHANNEL" --install-dir "$DOTNET_DIR"
    rm -rf "$download_dir"
fi
sdk_version="$(list_channel_sdks | tail -n 1)"
if [ -z "$sdk_version" ]; then
    echo "The .NET $DOTNET_CHANNEL SDK is not in $DOTNET_DIR after the install." >&2
    exit 1
fi

if ! grep -q "DOTNET_ROOT=" "$HOME/.profile"; then
    cat >> "$HOME/.profile" <<EOF
export DOTNET_ROOT="$DOTNET_DIR"
export PATH="\$DOTNET_ROOT:\$DOTNET_ROOT/tools:\$PATH"
EOF
fi

# CMake keeps the dotnet it found in each build tree's cache; without it the next build looks again.
for cache in "$REPO_DIR"/build/*/CMakeCache.txt; do
    if [ -f "$cache" ] && grep -q "^DOTNET_EXECUTABLE:" "$cache" && ! grep -qxF "DOTNET_EXECUTABLE:FILEPATH=$DOTNET" "$cache"; then
        sed -i '/^DOTNET_EXECUTABLE:/d' "$cache"
        echo "$(dirname "$cache") forgets its old dotnet and configures again at its next build."
    fi
done

echo "Done: .NET SDK $sdk_version in $DOTNET_DIR. Log out and back in (or run: source ~/.profile) so every"
echo "terminal and the editor use it, then check with: dotnet --version"
