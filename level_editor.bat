@echo off
rem Abre o editor de fase (tools/level_editor/editor.py) numa janela propria (Edge/Chrome em modo app, ou pywebview se instalado).
rem Fechar a janela do editor encerra o programa uns segundos depois (esta janela de console fecha junto).
rem Uso:  level_editor.bat [--port N] [--serve]
rem   sem argumento = --window; os argumentos dados vao direto para o editor.py (veja o comeco dele).
setlocal enableextensions
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
    echo Python 3 nao achado ^(py ou python no PATH^).
    pause
    exit /b 1
)

set "ARGS=%*"
if not defined ARGS set "ARGS=--window"
%PY% "%REPO%\tools\level_editor\editor.py" %ARGS%
set "RC=%errorlevel%"
if not "%RC%"=="0" pause
exit /b %RC%
