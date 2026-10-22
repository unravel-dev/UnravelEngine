# Generate the API docs:
#   - Engine C++ API: Doxygen with Engine-Doxyfile -> docs/engine-api/html
#   - Scripting C# API: docfx with docfx.json -> docs/_site
#
# Prerequisites:
#   - doxygen on PATH (Engine docs)
#   - docfx on PATH (Scripting docs): dotnet tool install -g docfx
#
# Usage (from repo root):
#   powershell -ExecutionPolicy Bypass -File docs/generate-api-docs.ps1
#   powershell -ExecutionPolicy Bypass -File docs/generate-api-docs.ps1 -ScriptOnly
#   powershell -ExecutionPolicy Bypass -File docs/generate-api-docs.ps1 -EngineOnly

[CmdletBinding()]
param(
    [switch] $ScriptOnly,
    [switch] $EngineOnly
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$docs = Join-Path $root "docs"

function Assert-Command([string] $Name)
{
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue))
    {
        throw "Required command not found on PATH: $Name"
    }
}

# Neither Doxygen nor docfx deletes old files from its output, so each build
# starts from an empty folder.
function Clear-Output([string] $Path)
{
    if (Test-Path $Path)
    {
        Remove-Item -Recurse -Force $Path
    }
}

$runScript = (-not $EngineOnly) -or $ScriptOnly
$runEngine = (-not $ScriptOnly) -or $EngineOnly
if ($ScriptOnly -and $EngineOnly)
{
    $runScript = $true
    $runEngine = $true
}

if ($runEngine)
{
    Assert-Command "doxygen"
    Write-Host "=== doxygen Engine-Doxyfile ==="
    Clear-Output (Join-Path $docs "engine-api")
    Push-Location $docs
    try
    {
        & doxygen Engine-Doxyfile
        if ($LASTEXITCODE -ne 0)
        {
            throw "doxygen Engine-Doxyfile failed with exit code $LASTEXITCODE"
        }
    }
    finally
    {
        Pop-Location
    }
}

if ($runScript)
{
    Assert-Command "docfx"
    Write-Host "=== docfx docfx.json ==="
    Clear-Output (Join-Path $docs "api")
    Clear-Output (Join-Path $docs "_site")
    # docfx resolves the paths in docfx.json against the file's own folder.
    & docfx (Join-Path $docs "docfx.json")
    if ($LASTEXITCODE -ne 0)
    {
        throw "docfx failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Done."
if ($runEngine)
{
    Write-Host "  Engine C++ API:   docs/engine-api/html"
}
if ($runScript)
{
    Write-Host "  Scripting C# API: docs/_site"
}
