# Star Swarm auto-push: commits any pending changes and pushes new commits to GitHub.
# Run every 15 minutes (hidden) by the "StarSwarm AutoPush" scheduled task.
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # ...\Code\tools -> repo root
Set-Location $repo
$log = Join-Path $repo 'autopush.log'
function Log($m) { "$(Get-Date -Format s)  $m" | Add-Content -Path $log }

git add -A 2>$null
if (git status --porcelain) {
    git commit -q -m "Auto-save $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    Log 'committed pending changes'
}
git fetch -q origin 2>$null
$ahead = git rev-list --count 'origin/main..HEAD' 2>$null
if ($LASTEXITCODE -ne 0 -or [int]$ahead -gt 0) {
    $out = (git push -u origin main 2>&1) -join ' '
    Log "push (exit $LASTEXITCODE): $out"
}
# keep the log short
if ((Test-Path $log) -and (Get-Item $log).Length -gt 200KB) { Get-Content $log -Tail 300 | Set-Content $log }
