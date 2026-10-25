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
# One-off force push after a history rewrite: create .git\force-push-once to trigger it (pushes main + tags, then removes the flag)
$force = Join-Path $repo '.git\force-push-once'
if (Test-Path $force) {
    $o1 = (git push --force origin main 2>&1) -join ' '; $e1 = $LASTEXITCODE
    $o2 = (git push --force origin --tags 2>&1) -join ' '; $e2 = $LASTEXITCODE
    Log "force push main (exit $e1): $o1"
    Log "force push tags (exit $e2): $o2"
    if ($e1 -eq 0 -and $e2 -eq 0) { Remove-Item $force }
}
git fetch -q origin 2>$null
$ahead = git rev-list --count 'origin/main..HEAD' 2>$null
if ($LASTEXITCODE -ne 0 -or [int]$ahead -gt 0) {
    $out = (git push -u origin main 2>&1) -join ' '
    Log "push (exit $LASTEXITCODE): $out"
}
# keep the log short
if ((Test-Path $log) -and (Get-Item $log).Length -gt 200KB) { Get-Content $log -Tail 300 | Set-Content $log }
