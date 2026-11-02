@echo off
rem Wake Velocity performance collection (G0w). Double-click to run the controlled benchmark.
rem The default run is the ordinary river and four-boat stress scenarios, waves on, no AA,
rem three repeats each: a few minutes on a normal desktop. Pass "full" for the complete
rem G0 matrix (flat/waves x AA off/4x4 x ordinary/stress). Pass "check" to verify the fixture
rem without a graphics context. Results stay under Documents\Wake Velocity\performance.
setlocal EnableExtensions
cd /d "%~dp0"

set "TOOL=%~dp0boat_water_review.exe"
if not exist "%TOOL%" (
  echo Cannot find boat_water_review.exe beside this launcher.
  exit /b 1
)

if /I "%~1"=="check" (
  "%TOOL%" --stress-state
  if errorlevel 1 exit /b %errorlevel%
  echo.
  echo Collection launcher paths and fixture are valid. No report was written.
  exit /b 0
)

set "MODE=default"
if /I "%~1"=="full" set "MODE=full"

set "REPORT_ROOT=%USERPROFILE%\Documents\Wake Velocity\performance"
if not exist "%REPORT_ROOT%" mkdir "%REPORT_ROOT%"
if not exist "%REPORT_ROOT%" (
  echo Cannot create "%REPORT_ROOT%".
  exit /b 1
)

set "RUN=%REPORT_ROOT%\collect-%RANDOM%-%RANDOM%"
mkdir "%RUN%" || exit /b 1
set "LOG=%RUN%\collect.log"
echo Wake Velocity performance collection > "%LOG%"
echo Output: %RUN% >> "%LOG%"
if exist "%~dp0build.txt" type "%~dp0build.txt" >> "%LOG%"

set "FAILED=0"
if "%MODE%"=="full" (
  set "PRESETS=flat waves"
  set "AAS=off 4x4"
) else (
  set "PRESETS=waves"
  set "AAS=off"
)

for %%P in (%PRESETS%) do (
  for %%A in (%AAS%) do (
    for %%S in (river stress) do (
      for %%R in (1 2 3) do (
        set "PREFIX=%RUN%\%%S-%%P-aa%%A-r%%R"
        echo Running %%S %%P AA %%A, repeat %%R ...
        if /I "%%P"=="waves" (
          "%TOOL%" --benchmark "%RUN%\%%S-%%P-aa%%A-r%%R" --aa %%A --scenario %%S --waves >> "%LOG%" 2>&1
        ) else (
          "%TOOL%" --benchmark "%RUN%\%%S-%%P-aa%%A-r%%R" --aa %%A --scenario %%S >> "%LOG%" 2>&1
        )
        if errorlevel 1 (
          echo Failed: %%S %%P AA %%A repeat %%R >> "%LOG%"
          set "FAILED=1"
        )
      )
    )
  )
)

rem Keep a readable summary beside the reports. The Mac comparison command is in the README.
if exist "%RUN%\river-waves-aaoff-r1.json" (
  echo. >> "%LOG%"
  echo Completed runs are preserved even if another run failed. >> "%LOG%"
)

echo Packaging the report folder ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop'; Compress-Archive -Path '%RUN%\*' -DestinationPath '%RUN%.zip' -Force"
if errorlevel 1 (
  echo Archiving failed. Copy the whole folder instead: "%RUN%"
  explorer "%RUN%"
  if "%FAILED%"=="1" exit /b 1
  exit /b 0
)

echo Report archive: %RUN%.zip
explorer "%RUN%"
if "%FAILED%"=="1" (
  echo One or more runs failed; completed reports are still present.
  exit /b 1
)
exit /b 0
