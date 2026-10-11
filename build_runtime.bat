@echo off
rem Recompila o runtime (ps2EntryRunner.exe) para o jogo ao vivo pegar as mudancas de native/.
rem O ninja nao rastreia native/wotm_native.hpp (fica fora da arvore do PS2Recomp), entao este script "toca" o ps2_runtime.cpp
rem antes de compilar. Acha sozinho a pasta do runtime (a mesma do run_live.bat).
rem Uso:  build_runtime.bat           -> build_dev (sem LTCG, ~1,5 min; o que o run_live.bat usa por padrao)
rem       build_runtime.bat build     -> build com LTCG (mais lento; usado por run_live.bat -Build build)
setlocal enableextensions
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
set "FOUND="

if defined WOTM_RECOMP_DIR if exist "%WOTM_RECOMP_DIR%\run_live.ps1" set "FOUND=%WOTM_RECOMP_DIR%"

if not defined FOUND if exist "%REPO%\.wotm_recomp_dir" (
    set /p SAVED=<"%REPO%\.wotm_recomp_dir"
    call :check "%SAVED%"
)
if not defined FOUND call :check "%REPO%"
if not defined FOUND call :check "%REPO%\..\wotm-recomp-win"
if not defined FOUND for /d %%D in ("%REPO%\..\*") do if not defined FOUND call :check "%%~fD"

if not defined FOUND (
    echo Nao achei run_live.ps1 perto de "%REPO%".
    set /p ASKED=Cole o caminho da pasta do runtime ^(a que contem run_live.ps1^):
    call :check "%ASKED%"
    if not defined FOUND (
        echo Pasta invalida.
        exit /b 1
    )
)
> "%REPO%\.wotm_recomp_dir" echo %FOUND%

set "BAT=build_dev.bat"
set "LOG=build_dev.log"
if /i "%~1"=="build" (
    set "BAT=build.bat"
    set "LOG=build.log"
)
if not exist "%FOUND%\%BAT%" (
    echo Nao achei "%FOUND%\%BAT%".
    exit /b 1
)

echo Runtime: %FOUND%
echo Compilando com %BAT% ^(pode levar alguns minutos; log em %LOG%^)...
set "RT=%FOUND%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$d=$env:RT; (Get-Item \"$d\PS2Recomp\ps2xRuntime\src\lib\ps2_runtime.cpp\").LastWriteTime = Get-Date; cmd /c \"`\"$d\$env:BAT`\"\"; Get-Content \"$d\$env:LOG\" -Tail 2"
echo.
echo Se a ultima linha for ninja_exit=0, o executavel esta atualizado.
exit /b %errorlevel%

:check
rem %1 = pasta candidata; define FOUND se tiver run_live.ps1
if exist "%~1\run_live.ps1" set "FOUND=%~f1"
exit /b 0
