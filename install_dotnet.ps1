# Installs the .NET 10 SDK that UnravelEngine builds with (the Android player needs 10) with winget. Microsoft's
# installer puts it in Program Files\dotnet beside the SDKs already there and on the PATH, where the editor's .NET
# host and CMake find it. The build trees under build\ forget the dotnet CMake found before, so their next build
# finds this one.
# Run it with: powershell -ExecutionPolicy Bypass -File install_dotnet.ps1
$ErrorActionPreference = 'Stop'

$DOTNET_CHANNEL = '10.0'   # cmake/android_player.cmake
$DOTNET_PACKAGE = 'Microsoft.DotNet.SDK.10'
$DOTNET = Join-Path $env:ProgramFiles 'dotnet\dotnet.exe'
$REPO_DIR = $PSScriptRoot
# winget's exit code when the package is installed and no newer version exists.
$WINGET_NO_UPGRADE = -1978335189

# The SDK versions of the channel in Program Files\dotnet, newest last; empty when it has none.
function Get-ChannelSdks {
    if (-not (Test-Path $DOTNET)) {
        return @()
    }
    @(& $DOTNET --list-sdks | ForEach-Object { ($_ -split ' ')[0] } | Where-Object { $_.StartsWith("$DOTNET_CHANNEL.") })
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

# CMake keeps the dotnet it found in each build tree's cache; without it the next build looks again.
function Reset-BuildTreeDotnet {
    $cached_line = 'DOTNET_EXECUTABLE:FILEPATH=' + $DOTNET.Replace('\', '/')
    foreach ($cache in Get-ChildItem (Join-Path $REPO_DIR 'build\*\CMakeCache.txt') -ErrorAction SilentlyContinue) {
        $lines = [IO.File]::ReadAllLines($cache.FullName)
        $kept = @($lines | Where-Object { -not $_.StartsWith('DOTNET_EXECUTABLE:') -or $_ -eq $cached_line })
        if ($kept.Count -ne $lines.Count) {
            [IO.File]::WriteAllLines($cache.FullName, $kept)
            Write-Host "$($cache.DirectoryName) forgets its old dotnet and configures again at its next build."
        }
    }
}

if (-not (Get-ChannelSdks)) {
    Install-WingetPackage $DOTNET_PACKAGE
}
$sdk_version = Get-ChannelSdks | Select-Object -Last 1
if (-not $sdk_version) {
    throw "The .NET $DOTNET_CHANNEL SDK is not in $(Split-Path $DOTNET) after the install."
}
Reset-BuildTreeDotnet
Write-Host "Done: .NET SDK $sdk_version in $(Split-Path $DOTNET). Open a new terminal so it finds dotnet on the PATH,"
Write-Host "then check with: dotnet --version"
