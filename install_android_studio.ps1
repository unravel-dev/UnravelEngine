# Installs what the UnravelEngine Android deploy needs on Windows without Android Studio: the SDK command-line
# tools, platform-tools (adb) and the pinned NDK in the folder Android Studio uses, %LOCALAPPDATA%\Android\Sdk
# (ANDROID_HOME when it is set), and JDK 17 with winget when there is no JDK yet. The android CLI accepts the SDK
# license itself (licenses\android-sdk-license), which lets Gradle download the compile SDK platform and the build
# tools on the first deploy. --no-metrics turns off the CLI's usage data. A JDK this script installs becomes
# JAVA_HOME, where the editor looks first; with Android Studio installed, the editor takes its bundled JDK.
# Run it with: powershell -ExecutionPolicy Bypass -File install_android_studio.ps1
$ErrorActionPreference = 'Stop'

$SDK_DIR = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$CMDLINE_TOOLS_BUILD = '15859902'
$CMDLINE_TOOLS_SHA256 = '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a'
$NDK_VERSION = '30.0.16248370'   # cmake/android_player.cmake
$JDK_PACKAGE = 'Microsoft.OpenJDK.17'
$JDK_PARENT_DIR = Join-Path $env:ProgramFiles 'Microsoft'
$ANDROID_CLI = Join-Path $SDK_DIR 'cmdline-tools\latest\bin\android.exe'
# Windows' own tar, which unpacks zips past the 260-character path limit that Expand-Archive stops at (the tools
# carry 165-character paths), and not a GNU tar a Git Bash PATH puts first.
$TAR = Join-Path $env:SystemRoot 'System32\tar.exe'
# winget's exit code when the package is installed and no newer version exists.
$WINGET_NO_UPGRADE = -1978335189

# Installs a winget package, or upgrades it when it is installed already.
function Install-WingetPackage([string]$Id) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget was not found. Install App Installer from the Microsoft Store, or install $Id yourself."
    }
    winget install --id $Id --exact
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $WINGET_NO_UPGRADE) {
        throw "winget could not install $Id (exit code $LASTEXITCODE)."
    }
}

# A JDK where the editor looks for one (editor/core/deploy/android.cpp): JAVA_HOME, then Android Studio's bundled
# one; $null when there is none.
function Find-Jdk {
    $candidates = @($env:JAVA_HOME, [Environment]::GetEnvironmentVariable('JAVA_HOME', 'User'))
    $studios = Get-ChildItem (Join-Path $env:ProgramFiles 'Android'), (Join-Path $env:LOCALAPPDATA 'Programs') `
        -Directory -Filter 'Android Studio*' -ErrorAction SilentlyContinue
    $candidates += @($studios | ForEach-Object { Join-Path $_.FullName 'jbr' })
    $candidates | Where-Object { $_ -and (Test-Path (Join-Path $_ 'bin\java.exe')) } | Select-Object -First 1
}

# JDK 17 from winget, made JAVA_HOME for this user.
function Install-Jdk {
    Install-WingetPackage $JDK_PACKAGE
    $jdk = Get-ChildItem $JDK_PARENT_DIR -Directory -Filter 'jdk-17*' -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Name -replace '^jdk-([0-9.]+).*$', '$1') } | Select-Object -Last 1
    if (-not $jdk) {
        throw "JDK 17 is not in $JDK_PARENT_DIR after the install."
    }
    [Environment]::SetEnvironmentVariable('JAVA_HOME', $jdk.FullName, 'User')
    Write-Host "JAVA_HOME is $($jdk.FullName) for programs started from now on."
}

function Install-CommandLineTools {
    if (Test-Path $ANDROID_CLI) {
        return
    }
    $tools_dir = Join-Path $SDK_DIR 'cmdline-tools'
    $latest_dir = Join-Path $tools_dir 'latest'
    # Unpacked beside its final place, so the move stays on one drive.
    $download_dir = Join-Path $tools_dir ('download-' + [IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Force $download_dir | Out-Null
    try {
        $zip = Join-Path $download_dir 'cmdline-tools.zip'
        curl.exe --fail --location --retry 3 --output $zip `
            "https://dl.google.com/android/repository/commandlinetools-win-${CMDLINE_TOOLS_BUILD}_latest.zip"
        if ($LASTEXITCODE -ne 0) {
            throw 'Downloading the SDK command-line tools failed.'
        }
        if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $CMDLINE_TOOLS_SHA256) {
            throw "$zip does not have the SHA-256 Google publishes for it."
        }
        & $TAR -xf $zip -C $download_dir
        if ($LASTEXITCODE -ne 0) {
            throw "Unpacking $zip failed."
        }
        # Command-line tools already there are older than the android CLI. They go into the download folder in one
        # rename, which no path length stops, and leave with it.
        if (Test-Path $latest_dir) {
            Move-Item $latest_dir (Join-Path $download_dir 'previous')
        }
        Move-Item (Join-Path $download_dir 'cmdline-tools') $latest_dir
    }
    finally {
        Remove-Item $download_dir -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $download_dir) {
            Write-Warning "$download_dir could not be deleted completely; delete it by hand."
        }
    }
}

if (-not (Find-Jdk)) {
    Install-Jdk
}
Install-CommandLineTools
& $ANDROID_CLI --no-metrics "--sdk=$SDK_DIR" sdk install platform-tools "ndk/$NDK_VERSION"
if ($LASTEXITCODE -ne 0) {
    throw "Installing platform-tools and NDK $NDK_VERSION failed (exit code $LASTEXITCODE)."
}
Write-Host "Done: the Android SDK is in $SDK_DIR. Restart the editor so it sees the SDK and JDK, then connect a"
Write-Host "phone and check with: $SDK_DIR\platform-tools\adb.exe devices"
