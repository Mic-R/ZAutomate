@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "DEFAULT_INSTALL_DIR=%LocalAppData%\Programs\zautomate"
set "LICENSE_FILE=%SCRIPT_DIR%LICENSE"
set "APP_EXE=%SCRIPT_DIR%zautomate.exe"
set "CURL_DLL=%SCRIPT_DIR%libcurl.dll"

echo.
echo ZAutomate Windows installer helper
echo ==================================
echo.

if not exist "%APP_EXE%" (
  echo Error: zautomate.exe was not found next to this script.
  pause
  exit /b 1
)

if not exist "%CURL_DLL%" (
  echo Error: libcurl.dll was not found next to this script.
  pause
  exit /b 1
)

echo License:
if exist "%LICENSE_FILE%" (
  more < "%LICENSE_FILE%"
) else (
  echo The LICENSE file was not bundled with this release.
)
echo.
choice /C YN /M "Do you accept the license terms and want to continue"
if errorlevel 2 (
  echo Installation cancelled.
  exit /b 1
)

set /p INSTALL_DIR=Install location [%DEFAULT_INSTALL_DIR%]: 
if "%INSTALL_DIR%"=="" set "INSTALL_DIR=%DEFAULT_INSTALL_DIR%"

echo.
echo Installing to "%INSTALL_DIR%"
mkdir "%INSTALL_DIR%" 2>nul
if errorlevel 1 (
  echo Error: could not create install directory.
  pause
  exit /b 1
)

copy /Y "%APP_EXE%" "%INSTALL_DIR%\" >nul || goto :copy_failed
copy /Y "%CURL_DLL%" "%INSTALL_DIR%\" >nul || goto :copy_failed

for %%F in (*.dll) do (
  if /I not "%%~nxF"=="libcurl.dll" copy /Y "%%~fF" "%INSTALL_DIR%\" >nul
)

if exist "%LICENSE_FILE%" copy /Y "%LICENSE_FILE%" "%INSTALL_DIR%\LICENSE.txt" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$shell = New-Object -ComObject WScript.Shell;" ^
  "$shortcut = $shell.CreateShortcut([IO.Path]::Combine($env:USERPROFILE,'Desktop','ZAutomate.lnk'));" ^
  "$shortcut.TargetPath = [IO.Path]::Combine('%INSTALL_DIR%','zautomate.exe');" ^
  "$shortcut.WorkingDirectory = '%INSTALL_DIR%';" ^
  "$shortcut.IconLocation = ([IO.Path]::Combine('%INSTALL_DIR%','zautomate.exe') + ',0');" ^
  "$shortcut.Save()"

if errorlevel 1 (
  echo Warning: could not create desktop shortcut.
) else (
  echo Desktop shortcut created.
)

echo.
set "START_MENU_DIR=%AppData%\Microsoft\Windows\Start Menu\Programs"
if not exist "%START_MENU_DIR%" mkdir "%START_MENU_DIR%" >nul 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$shell = New-Object -ComObject WScript.Shell;" ^
  "$shortcut = $shell.CreateShortcut([IO.Path]::Combine($env:APPDATA,'Microsoft','Windows','Start Menu','Programs','ZAutomate.lnk'));" ^
  "$shortcut.TargetPath = [IO.Path]::Combine('%INSTALL_DIR%','zautomate.exe');" ^
  "$shortcut.WorkingDirectory = '%INSTALL_DIR%';" ^
  "$shortcut.IconLocation = [IO.Path]::Combine('%INSTALL_DIR%','zautomate.exe,0');" ^
  "$shortcut.Save()"

if errorlevel 1 (
  echo Warning: could not create Start Menu shortcut.
) else (
  echo Start Menu shortcut created.
)

echo.
echo Installation complete.
echo You can start ZAutomate from: "%INSTALL_DIR%\zautomate.exe"
pause
exit /b 0

:copy_failed
echo Error: failed to copy one or more files.
pause
exit /b 1