#!/usr/bin/env bash
# Installs what the UnravelEngine Android deploy needs on Ubuntu / Debian without Android Studio:
# JDK 17, the SDK command-line tools, platform-tools (adb) and the pinned NDK. Gradle downloads the
# compile SDK platform and the build tools on the first deploy, under the names the SDK repository uses.
# Run it as your own user (bash install_android_studio.sh): the SDK belongs in your home folder, and
# the script asks for sudo itself for apt.
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script as your own user, not with sudo: it installs the SDK into your home folder." >&2
    exit 1
fi

SDK_DIR="${ANDROID_HOME:-$HOME/Android/Sdk}"
CMDLINE_TOOLS_BUILD="15859902"
CMDLINE_TOOLS_SHA256="4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"
NDK_VERSION="30.0.16248370"   # cmake/android_player.cmake
JDK_DIR="/usr/lib/jvm/java-17-openjdk-$(dpkg --print-architecture)"

sudo apt-get update
sudo apt-get install -y openjdk-17-jdk-headless curl unzip android-sdk-platform-tools-common

SDKMANAGER="$SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
if [ ! -x "$SDKMANAGER" ]; then
    download_dir="$(mktemp -d)"
    zip="$download_dir/cmdline-tools.zip"
    curl -fL -o "$zip" "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_BUILD}_latest.zip"
    echo "$CMDLINE_TOOLS_SHA256  $zip" | sha256sum -c -
    unzip -q "$zip" -d "$download_dir"
    mkdir -p "$SDK_DIR/cmdline-tools"
    mv "$download_dir/cmdline-tools" "$SDK_DIR/cmdline-tools/latest"
    rm -rf "$download_dir"
fi

export JAVA_HOME="$JDK_DIR"
(yes || true) | "$SDKMANAGER" --licenses > /dev/null
"$SDKMANAGER" "platform-tools" "ndk;${NDK_VERSION}"

if ! grep -q "ANDROID_HOME=" "$HOME/.profile"; then
    cat >> "$HOME/.profile" <<EOF
export ANDROID_HOME="$SDK_DIR"
export JAVA_HOME="$JDK_DIR"
export PATH="\$ANDROID_HOME/platform-tools:\$PATH"
EOF
fi
echo "Done. Log out and back in so the editor sees ANDROID_HOME / JAVA_HOME, then run: adb devices"