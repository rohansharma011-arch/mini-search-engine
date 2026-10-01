@echo off
REM Double-click this (or run .\start_ui.bat) to build if needed and open the web UI.
cd /d "%~dp0"
if not exist bin\web_server.exe (
  call build.bat || (pause & exit /b 1)
)
echo Starting Mini Search Engine at http://localhost:8080  (close this window to stop)
bin\web_server.exe documents
pause
