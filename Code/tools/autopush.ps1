# Star Swarm auto-push (run every 15 minutes, hidden, by the "StarSwarm AutoPush" scheduled task).
#  1. Backup: snapshots the whole working tree (minus .gitignore'd files) onto the "autosave" branch and pushes it.
#     It uses a private index, so it never touches main, the staging area or the files you're working on.
#  2. Publishes: pushes main when it has commits GitHub doesn't have yet (main's history stays the real commits).
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # ...\Code\tools -> repo root
Set-Location $repo
$log = Join-Path $repo 'autopush.log'
function Log($m) { "$(Get-Date -Format s)  $m" | Add-Content -Path $log }

# --- 1. snapshot to the autosave branch ---
$env:GIT_INDEX_FILE = Join-Path $repo '.git\autosave-index'
git read-tree HEAD 2>$null
git add -A 2>$null
$tree = git write-tree
Remove-Item Env:\GIT_INDEX_FILE
$prev = git rev-parse -q --verify refs/heads/autosave 2>$null
$prevTree = if ($prev) { git rev-parse "$prev^{tree}" } else { '' }
$headTree = git rev-parse 'HEAD^{tree}'
if ($tree -ne $prevTree -and -not (-not $prev -and $tree -eq $headTree)) {
    $parents = @()
    if ($prev) { $parents += @('-p', $prev) }
    git merge-base --is-ancestor HEAD $prev 2>$null
    if (-not $prev -or $LASTEXITCODE -ne 0) { $parents += @('-p', 'HEAD') }   # keep main reachable: always a fast-forward
    $c = git commit-tree $tree @parents -m "Auto-save $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    if ($c) { git update-ref refs/heads/autosave $c; Log "snapshot $($c.Substring(0,7)) on autosave" }
}
if (git rev-parse -q --verify refs/heads/autosave 2>$null) {
    $remoteSave = git rev-parse -q --verify refs/remotes/origin/autosave 2>$null
    if ($remoteSave -ne (git rev-parse refs/heads/autosave)) {
        $out = (git push origin autosave 2>&1) -join ' '
        Log "push autosave (exit $LASTEXITCODE): $out"
    }
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

# --- 2. publish main's real commits ---
git fetch -q origin 2>$null
$ahead = git rev-list --count 'origin/main..main' 2>$null
if ($LASTEXITCODE -ne 0 -or [int]$ahead -gt 0) {
    $out = (git push -u origin main 2>&1) -join ' '
    Log "push main (exit $LASTEXITCODE): $out"
}
# keep the log short
if ((Test-Path $log) -and (Get-Item $log).Length -gt 200KB) { Get-Content $log -Tail 300 | Set-Content $log }
