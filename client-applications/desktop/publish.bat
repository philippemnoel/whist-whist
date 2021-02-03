@echo off

setlocal enabledelayedexpansion

set "map=%*"
call :gettoken -branch branch
call :gettoken -version version
call :gettoken -bucket bucket
call :gettoken -publish publish

if "%1%" == "--help" (
    call :printhelp
) else (
    echo Make sure to run this script in a x86_x64 terminal on Windows.

    PowerShell.exe -ExecutionPolicy Bypass -Command "& './setVersion.ps1'" -version %version% -bucket %bucket%

    cd ..\..\protocol
    cmake . -DCMAKE_BUILD_TYPE=Release -G "NMake Makefiles"
    nmake FractalClient
    cd ..\client-applications\desktop
    rmdir /S/Q protocol-build
    mkdir protocol-build
    cd protocol-build
    mkdir desktop
    mkdir desktop\loading
    cd ..
    xcopy /s ..\..\protocol\desktop\build64\Windows protocol-build\desktop

    REM Note: we no longer add the logo to the executable because the logo gets set
    REM in the protocol directly via SDL

    REM initialize yarn first
    yarn -i

    if "%publish%" == "true" (
        yarn package-ci
        REM yarn package-ci && curl -X POST -H Content-Type:application/json  -d "{ \"branch\" : \"%1\", \"version\" : \"%2\" }" https://staging-server.fractal.co/version
    ) else (
        yarn package
    )
)

goto :eof
:printhelp
echo Usage: build [OPTION 1] [OPTION 2] ...
echo.
echo Note: Make sure to run this script in a x86_x64 terminal on Windows.
echo Note: All arguments to both long and short options are mandatory.
echo.
echo   -branch=BRANCH                set the Github protocol branch that you
echo                                   want the client app to run
echo.
echo   -version=VERSION              set the version number of the client app
echo                                   must be greater than the current version
echo                                   in S3 bucket
echo.
echo   -bucket=BUCKET                set the S3 bucket to upload to (if -publish=true)
echo                                   options are:
echo.
echo                                  fractal-windows-application-release [External Windows bucket for regular users]
echo                                  fractal-mac-application-release     [External macOS bucket for regular users]
echo                                  fractal-linux-application-release   [External Linux Ubuntu bucket for regular users]
echo                                  fractal-windows-application-staging [External Windows bucket for staging users]
echo                                  fractal-mac-application-staging     [External macOS bucket for staging users]
echo                                  fractal-linux-application-staging   [External Linux Ubuntu bucket for staging users]
echo                                  fractal-windows-application-dev     [Internal Windows bucket for development]
echo                                  fractal-mac-application-dev         [Internal macOS bucket for development]
echo                                  fractal-linux-application-dev       [Internal Linux Ubuntu bucket for development]
echo.
echo   -publish=PUBLISH              set whether to publish to S3 and auto-update live apps
echo                                   defaults to false, options are true/false
exit /b 0

:gettoken
call set "tmpvar=%%map:*%1=%%"
if "%tmpvar%"=="%map%" (set "%~2=") else (
for /f "tokens=1 delims= " %%a in ("%tmpvar%") do set tmpvar=%%a
set "%~2=!tmpvar:~1!"
)
exit /b

