@echo off
rem Roda o jogo recompilado com o desenho nativo ao vivo, achando sozinho a pasta do runtime (a que tem run_live.ps1).
rem Uso:  run_live.bat [-Auto 0|1] [-Stats 1] [-Build build_dev|build]
rem   -Auto 0 = voce joga com o teclado (padrao do script: 1 = roteiro automatico ate a fase)
rem Ordem da busca: variavel WOTM_RECOMP_DIR, .wotm_recomp_dir (salvo na 1a vez), esta pasta, irmas desta pasta
rem (wotm-recomp-win e qualquer outra que tenha run_live.ps1). Nao grava nada do jogo no repo.
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

echo Runtime: %FOUND%
set "WOTM_REPO=%REPO%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%FOUND%\run_live.ps1" %*
exit /b %errorlevel%

:check
rem %1 = pasta candidata; define FOUND se tiver run_live.ps1
if exist "%~1\run_live.ps1" set "FOUND=%~f1"
exit /b 0
