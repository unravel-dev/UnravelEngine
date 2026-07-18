# Recreate Cursor / Claude skill junctions to .agents/skills (Windows).
# Run from repo root:  powershell -File tasks/link-agent-dirs.ps1

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

New-Item -ItemType Directory -Force -Path .cursor, .claude | Out-Null
$target = (Resolve-Path ".agents\skills").Path

foreach ($path in @(".cursor\skills", ".claude\skills")) {
    if (Test-Path $path) {
        Remove-Item $path -Force -Recurse
    }
    New-Item -ItemType Junction -Path $path -Target $target | Out-Null
    Write-Host "OK $path -> $target"
}

# Remove obsolete rules junction if present
if (Test-Path ".cursor\rules") {
    Remove-Item ".cursor\rules" -Force -Recurse
    Write-Host "Removed obsolete .cursor/rules"
}

Write-Host "Done."
