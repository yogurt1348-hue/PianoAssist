@echo off
rem Adaptive Follow: drag a .mid file onto this file to convert it into a reference
rem for the AdaptiveFollow MIDI script (written to ...\Settings\Hardware\AdaptiveFollow\data).
setlocal
if "%~1"=="" (
  echo Drag a .mid file onto midi_to_reference.bat, or run:
  echo   midi_to_reference.bat "C:\path\to\part.mid" [--track N] [--name "My part"]
  pause
  exit /b 1
)
set "VENVPY=%LOCALAPPDATA%\AdaptiveFollow\venv\Scripts\python.exe"
if exist "%VENVPY%" (
  "%VENVPY%" "%~dp0midi_to_reference.py" %*
) else (
  py -3 "%~dp0midi_to_reference.py" %*
)
echo.
pause
