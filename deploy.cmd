@echo off
setlocal

rem  EVE bots for Windows.
rem  Author: Igor Polev.
rem
rem  Deploys the release build to a folder you can run the bot from.
rem
rem  Binaries and configuration are copied rather than linked: a full
rem  relink replaces the .exe, and editors save JSON by writing a new file
rem  and renaming it, either of which quietly detaches a hard link and
rem  leaves a stale file behind that still looks right. img_lib is the one
rem  exception - a junction, so patterns edited in the repo are picked up
rem  without deploying again.
rem
rem  Usage:  deploy [target folder]
rem          default target is %USERPROFILE%\Desktop\eve-bots

set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
set "RELEASE=%REPO%\build\windows-msvc\Release"

set "DEST=%~1"
if "%DEST%"=="" set "DEST=%USERPROFILE%\Desktop\eve-bots"

if not exist "%RELEASE%\eve_bots.exe" (
    echo    [ERROR] No release build in "%RELEASE%".
    echo            Build it first:
    echo                cmake --build --preset windows-msvc-release
    exit /b 1
)

echo Deploying to "%DEST%"

rem  A running copy holds its own exe and DLLs open, and the copy would
rem  fail with access denied.
taskkill /IM eve_bots.exe /F >nul 2>&1

if not exist "%DEST%" (
    mkdir "%DEST%" || goto :failed
)

rem  Left alone when it is already there, so a junction pointing somewhere
rem  deliberate is never quietly replaced.
if not exist "%DEST%\img_lib" (
    mklink /J "%DEST%\img_lib" "%REPO%\img_lib" >nul || (
        echo    [ERROR] Could not link img_lib to "%REPO%\img_lib".
        exit /b 1
    )
)

copy /Y "%RELEASE%\*.exe" "%DEST%\" >nul || goto :failed
copy /Y "%RELEASE%\*.dll" "%DEST%\" >nul || goto :failed

rem  Named one by one on purpose: "*.json" would also drag in
rem  CMakePresets.json and vcpkg.json, which belong to the build.
for %%F in (eve_config.json eve_images.json prg_params.json) do (
    copy /Y "%REPO%\%%F" "%DEST%\" >nul || goto :failed
)

rem  eve_bots.json was renamed to eve_config.json; a copy left behind from
rem  an older deploy is dead weight that still looks like configuration.
if exist "%DEST%\eve_bots.json" del /Q "%DEST%\eve_bots.json"

echo.
for %%F in ("%DEST%\eve_bots.exe") do echo   eve_bots.exe  %%~zF bytes  %%~tF
echo.
echo Deployed. Run it with:
echo     cd /d "%DEST%" ^&^& eve_bots.exe
exit /b 0

:failed
echo    [ERROR] Deploy failed while copying into "%DEST%".
exit /b 1
