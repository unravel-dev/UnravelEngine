# Generate API docs: Doxygen (Script + Engine) then moxygen -> Markdown.
# Moxygen does not run Doxyfiles; this script chains both tools.
#
# Prerequisites:
#   - doxygen on PATH
#   - Node.js 20+ and either `moxygen` on PATH or npm (uses npx)
#
# Usage (from repo root):
#   powershell -File tasks/generate-api-docs.ps1
#   powershell -File tasks/generate-api-docs.ps1 -ScriptOnly
#   powershell -File tasks/generate-api-docs.ps1 -EngineOnly
#   powershell -File tasks/generate-api-docs.ps1 -SkipDoxygen
#   powershell -File tasks/generate-api-docs.ps1 -SkipMoxygen

[CmdletBinding()]
param(
    [switch] $ScriptOnly,
    [switch] $EngineOnly,
    [switch] $SkipDoxygen,
    [switch] $SkipMoxygen
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

function Rename-ApiIndexToReadme([string] $MarkdownDir)
{
    $apiPath = Join-Path $MarkdownDir "api.md"
    $readmePath = Join-Path $MarkdownDir "README.md"
    if (-not (Test-Path $apiPath))
    {
        Write-Host "Warning: expected index missing: $apiPath"
        return
    }
    if (Test-Path $readmePath)
    {
        Remove-Item -Force $readmePath
    }
    Move-Item -Force $apiPath $readmePath
    Write-Host "Renamed api.md -> README.md in $MarkdownDir"
}

function Invoke-Moxygen([string] $XmlDir, [string] $OutputPattern, [string] $Language)
{
    if (-not (Test-Path (Join-Path $XmlDir "index.xml")))
    {
        throw "Doxygen XML missing (expected index.xml): $XmlDir"
    }
    $outDir = Split-Path -Parent $OutputPattern
    if ($outDir -and -not (Test-Path $outDir))
    {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $moxygenArgs = @(
        "--html-anchors",
        "--classes",
        "--language", $Language,
        "--output", $OutputPattern,
        $XmlDir
    )
    if (Get-Command moxygen -ErrorAction SilentlyContinue)
    {
        Write-Host "moxygen $($moxygenArgs -join ' ')"
        & moxygen @moxygenArgs
        if ($LASTEXITCODE -ne 0)
        {
            throw "moxygen failed with exit code $LASTEXITCODE"
        }
    }
    else
    {
        Assert-Command "npx"
        Write-Host "npx --yes moxygen $($moxygenArgs -join ' ')"
        & npx --yes moxygen @moxygenArgs
        if ($LASTEXITCODE -ne 0)
        {
            throw "npx moxygen failed with exit code $LASTEXITCODE"
        }
    }
    Rename-ApiIndexToReadme -MarkdownDir $outDir
}

$runScript = (-not $EngineOnly) -or $ScriptOnly
$runEngine = (-not $ScriptOnly) -or $EngineOnly
if ($ScriptOnly -and $EngineOnly)
{
    $runScript = $true
    $runEngine = $true
}

if (-not $SkipDoxygen)
{
    Assert-Command "doxygen"
    Push-Location $docs
    try
    {
        if ($runScript)
        {
            Write-Host "=== doxygen Script-Doxyfile ==="
            & doxygen Script-Doxyfile
            if ($LASTEXITCODE -ne 0)
            {
                throw "doxygen Script-Doxyfile failed with exit code $LASTEXITCODE"
            }
        }
        if ($runEngine)
        {
            Write-Host "=== doxygen Engine-Doxyfile ==="
            & doxygen Engine-Doxyfile
            if ($LASTEXITCODE -ne 0)
            {
                throw "doxygen Engine-Doxyfile failed with exit code $LASTEXITCODE"
            }
        }
    }
    finally
    {
        Pop-Location
    }
}

if (-not $SkipMoxygen)
{
    if ($runScript)
    {
        Write-Host "=== moxygen script-api ==="
        # C# sources; moxygen has no csharp templates (only cpp/java). java is closer.
        Invoke-Moxygen `
            -XmlDir (Join-Path $docs "script-api\xml") `
            -OutputPattern (Join-Path $docs "markdown\script-api\%s.md") `
            -Language "java"
    }
    if ($runEngine)
    {
        Write-Host "=== moxygen engine-api ==="
        Invoke-Moxygen `
            -XmlDir (Join-Path $docs "engine-api\xml") `
            -OutputPattern (Join-Path $docs "markdown\engine-api\%s.md") `
            -Language "cpp"
    }
}

Write-Host "Done."
Write-Host "  HTML:     docs/script-api/html , docs/engine-api/html"
Write-Host "  XML:      docs/script-api/xml  , docs/engine-api/xml"
Write-Host "  Markdown: docs/markdown/script-api , docs/markdown/engine-api"
