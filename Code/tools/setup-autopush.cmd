@echo off
rem One-time setup: first push to GitHub + scheduled task that pushes new versions every 15 minutes.
cd /d "%~dp0..\.."
echo.
echo === Star Swarm - GitHub auto-push setup ===
echo Pushing to https://github.com/GiladGlo/StarSwarm (a GitHub sign-in window may appear)...
git push -u origin main
if errorlevel 1 (
    echo.
    echo GitHub already has commits - merging them in, then pushing again...
    git pull origin main --allow-unrelated-histories --no-edit
    git push -u origin main
    if errorlevel 1 ( echo. & echo Push failed - see the message above. & pause & exit /b 1 )
)
schtasks /Create /F /TN "StarSwarm AutoPush" /SC MINUTE /MO 15 /TR "wscript.exe \"%~dp0autopush.vbs\"" >nul
if errorlevel 1 ( echo Could not create the scheduled task. & pause & exit /b 1 )
echo.
echo Done. New versions are pushed to GitHub automatically every 15 minutes.
echo Log: %CD%\autopush.log     To remove: schtasks /Delete /TN "StarSwarm AutoPush" /F
pause
