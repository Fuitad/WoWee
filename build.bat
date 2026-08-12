@echo off
REM Convenience wrapper - launches the PowerShell build script.
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
