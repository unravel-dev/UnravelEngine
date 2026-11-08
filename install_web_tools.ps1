# Installs what building the browser player from source needs on Windows: the .NET SDK the release builds it with
# and that SDK's wasm-tools workload, which brings the Emscripten, LLVM, Node.js and Python the player compiles
# with. The SDK goes into .dotnet-web in your user folder, beside the .NET the rest of the build uses
# (install_dotnet.ps1), and CMake takes it from there (cmake/web_player.cmake). -Cloudflared also installs
# cloudflared, Cloudflare's tunnel tool, with winget, or upgrades it: the Deploy window's Share Public Link runs it.
# Editor packages carry the browser player, so deploying from one needs none of this.
# Run it with: powershell -ExecutionPolicy Bypass -File install_web_tools.ps1 [-Cloudflared]
param(
    [switch]$Cloudflared
)
$ErrorActionPreference = 'Stop'

$RELEASE_WORKFLOW = Join-Path $PSScriptRoot '.github\workflows\release.yml'
$WEB_DOTNET_DIR = Join-Path $env:USERPROFILE '.dotnet-web'   # cmake/web_player.cmake
$WEB_DOTNET = Join-Path $WEB_DOTNET_DIR 'dotnet.exe'
$DOTNET_INSTALL_URL = 'https://dot.net/v1/dotnet-install.ps1'
$CLOUDFLARED_PACKAGE = 'Cloudflare.cloudflared'
# winget's exit code when the package is installed and no newer version exists.
$WINGET_NO_UPGRADE = -1978335189

# The value of an env entry of the release workflow, so a source build uses what the release packages build with.
function Read-ReleasePin([string]$Name) {
    $match = Select-String -Path $RELEASE_WORKFLOW -Pattern "^  ${Name}: '(.*)'$" | Select-Object -First 1
    if (-not $match) {
        throw "$RELEASE_WORKFLOW has no $Name."
    }
    $match.Matches[0].Groups[1].Value
}

# Whether WEB_DOTNET_DIR holds the SDK version $Version.
function Test-WebSdk([string]$Version) {
    (Test-Path $WEB_DOTNET) -and [bool](& $WEB_DOTNET --list-sdks | Where-Object { $_.StartsWith("$Version [") })
}

function Install-WebSdk([string]$Version) {
    if (Test-WebSdk $Version) {
        return
    }
    # An interrupted install leaves the version folder, which the installer takes as done and never completes.
    Remove-Item (Join-Path $WEB_DOTNET_DIR "sdk\$Version") -Recurse -Force -ErrorAction SilentlyContinue
    $installer = Join-Path ([IO.Path]::GetTempPath()) ('dotnet-install-' + [IO.Path]::GetRandomFileName() + '.ps1')
    try {
        curl.exe --fail --silent --show-error --location --retry 3 --output $installer $DOTNET_INSTALL_URL
        if ($LASTEXITCODE -ne 0) {
            throw 'Downloading dotnet-install.ps1 failed.'
        }
        # A process of its own: Windows PowerShell would stop the installer at the first stderr line of a tool
        # it runs. It unpacks .NET 11 with the tar on the PATH, which has to be Windows' own: a Git Bash PATH puts
        # GNU tar first, which reads C: as a remote host.
        $env:PATH = (Join-Path $env:SystemRoot 'System32') + ";$env:PATH"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer -Version $Version -InstallDir $WEB_DOTNET_DIR -NoPath
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet-install.ps1 could not install the .NET SDK $Version (exit code $LASTEXITCODE)."
        }
    }
    finally {
        Remove-Item $installer -ErrorAction SilentlyContinue
    }
    if (-not (Test-WebSdk $Version)) {
        throw "The .NET SDK $Version is not in $WEB_DOTNET_DIR after the install."
    }
}

# The workload comes from the manifests the SDK version carries, so it brings the same Emscripten as the release's.
function Install-WasmTools {
    & $WEB_DOTNET workload install wasm-tools --skip-manifest-update
    if ($LASTEXITCODE -ne 0) {
        throw "Installing the wasm-tools workload failed (exit code $LASTEXITCODE)."
    }
}

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

$sdk_version = Read-ReleasePin 'WEB_DOTNET_SDK_VERSION'
Install-WebSdk $sdk_version
Install-WasmTools
if ($Cloudflared) {
    Install-WingetPackage $CLOUDFLARED_PACKAGE
}
Write-Host "Done: .NET SDK $sdk_version with the wasm-tools workload in $WEB_DOTNET_DIR."
Write-Host "Configure your build tree again (for example: cmake -B build/Release) and look for 'Web player: on'."
