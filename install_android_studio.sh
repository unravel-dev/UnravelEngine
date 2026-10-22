#!/usr/bin/env bash
# Installs what the UnravelEngine Android deploy needs on Ubuntu / Debian without Android Studio:
# JDK 17, the SDK command-line tools, platform-tools (adb), the compile SDK and the pinned NDK.
set -euo pipefail

SDK_DIR="${ANDROID_HOME:-$HOME/Android/Sdk}"
CMDLINE_TOOLS_BUILD="15859902"
CMDLINE_TOOLS_SHA256="4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"
NDK_VERSION="30.0.16248370"   # cmake/android_player.cmake
COMPILE_SDK="android-37"      # game/android/app/build.gradle.kts
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
"$SDKMANAGER" "platform-tools" "platforms;${COMPILE_SDK}" "ndk;${NDK_VERSION}"

if ! grep -q "ANDROID_HOME=" "$HOME/.profile"; then
    cat >> "$HOME/.profile" <<EOF
export ANDROID_HOME="$SDK_DIR"
export JAVA_HOME="$JDK_DIR"
export PATH="\$ANDROID_HOME/platform-tools:\$PATH"
EOF
fi
echo "Done. Log out and back in so the editor sees ANDROID_HOME / JAVA_HOME, then run: adb devices"