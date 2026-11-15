# Links .claude\skills and .cursor\skills to .agents\skills (directory junctions, no admin rights).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File .agents/link-agent-dirs.ps1 [-IfMissing]
#   -IfMissing  keep links that already work and print nothing then
param([switch]$IfMissing)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root ".agents\skills"
foreach ($tool in @(".claude", ".cursor")) {
    $link = Join-Path $root "$tool\skills"
    if ($IfMissing -and (Test-Path -LiteralPath (Join-Path $link "README.md"))) {
        continue
    }
    $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
    if ($item) {
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Write-Output "Skipped $tool\skills: a real folder, not a link"
            continue
        }
        # Delete() on a junction removes the link only, never the target's files.
        $item.Delete()
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $root $tool) | Out-Null
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    Write-Output "Linked $tool\skills -> .agents\skills; skills load after /reload-skills or in the next session"
}
