# ABOUTME: Monta o WotM_LevelEditor.exe (tools/level_editor/editor.py com Python e a pagina web/ dentro, PyInstaller) para o zip do release.
# ABOUTME: Venv proprio fora do repo (<runtime>\level_editor_build\venv); saida em <runtime>\level_editor_build\dist\, com as licencas do que vai dentro.
#
# Uso: powershell -ExecutionPolicy Bypass -File tools\build_level_editor.ps1 [-Runtime <pasta>]   (o tools\release.ps1 chama sozinho)
# Precisa do Python 3 no PATH (py ou python). Versao fixa do PyInstaller; o editor nao usa numpy (o monglb e o pose, que usam, ficam de fora).
param([string]$Runtime = '')

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
function Fail([string]$m) { Write-Host "ERRO: $m" -ForegroundColor Red; exit 1 }

if (-not $Runtime) {
    $saved = Join-Path $repo '.wotm_recomp_dir'
    $Runtime = if (Test-Path $saved) { (Get-Content $saved -TotalCount 1).Trim() } else { Join-Path (Split-Path $repo -Parent) 'wotm-recomp-win' }
}
$root = Join-Path $Runtime 'level_editor_build'
$venv = Join-Path $root 'venv'
$py = Join-Path $venv 'Scripts\python.exe'
$editor = Join-Path $repo 'tools\level_editor'

# --- venv com as versoes fixas ------------------------------------------------------------------------------------------------------
$pins = @('pyinstaller==6.16.0')
if (-not (Test-Path $py)) {
    $base = (Get-Command py -ErrorAction SilentlyContinue).Source
    $baseArgs = @('-3')
    if (-not $base) { $base = (Get-Command python -ErrorAction SilentlyContinue).Source; $baseArgs = @() }
    if (-not $base) { Fail 'Python 3 nao achado (py ou python no PATH)' }
    New-Item -ItemType Directory -Force $root | Out-Null
    & $base @baseArgs -m venv $venv
    if ($LASTEXITCODE) { Fail 'python -m venv falhou' }
}
& $py -m pip install --disable-pip-version-check -q @pins
if ($LASTEXITCODE) { Fail "pip install $($pins -join ' ') falhou" }

# --- a pagina sem o que e so de teste (web\_test.html) ---------------------------------------------------------------------------------
$web = Join-Path $root 'web'
Remove-Item -Recurse -Force $web -ErrorAction SilentlyContinue
Copy-Item -Recurse (Join-Path $editor 'web') $web
Get-ChildItem $web -Recurse -File -Filter '_*' | Remove-Item -Force

# --- PyInstaller: um exe so, sem console (o editor fecha sozinho quando a pagina some) -------------------------------------------------
$dist = Join-Path $root 'dist'
$work = Join-Path $root 'work'
$ico = Join-Path $repo 'native\wotm.ico'
Remove-Item -Recurse -Force $dist, $work -ErrorAction SilentlyContinue
$hidden = 'lvl', 'lvlglb', 'lvlnames', 'lvlhud', 'lvlprop', 'ngpmesh', 'vifpkt', 'modkit', 'ngp', 'tex', 'gltf', 'iso9660', 'snd989',
    'hudapi', 'pedsapi' |
    ForEach-Object { '--hidden-import', $_ }
$exclude = 'numpy', 'monglb', 'pose', 'tkinter' | ForEach-Object { '--exclude-module', $_ }
& $py -m PyInstaller --noconfirm --clean --onefile --windowed --name WotM_LevelEditor --icon $ico `
    --add-data "$web;web" --add-data "$(Join-Path $editor 'names.json');." `
    --paths (Join-Path $repo 'tools') --paths $editor @hidden @exclude --distpath $dist --workpath $work --specpath $work `
    (Join-Path $editor 'editor.py')
if ($LASTEXITCODE) { Fail 'PyInstaller falhou' }
$exe = Join-Path $dist 'WotM_LevelEditor.exe'
if (-not (Test-Path $exe)) { Fail "falta $exe" }

# --- licencas do que vai dentro do exe (Python, bootloader do PyInstaller, three.js e os icones MDI da pagina) ---------------------
$lic = Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
$basePrefix = (& $py -c 'import sys; print(sys.base_prefix)').Trim()
$site = (& $py -c 'import sysconfig; print(sysconfig.get_paths()[\"purelib\"])').Trim()
$pyLic = Join-Path $basePrefix 'LICENSE.txt'
if (-not (Test-Path $pyLic)) { Fail "licenca do Python nao achada: $pyLic" }
Copy-Item $pyLic (Join-Path $lic 'Python-LICENSE.txt')
$pin = Get-ChildItem $site -Directory -Filter 'pyinstaller-*.dist-info' | Select-Object -First 1
$pinLic = if ($pin) { Get-ChildItem $pin.FullName -Recurse -File | Where-Object { $_.Name -match '^COPYING|^LICENSE' } | Select-Object -First 1 }
if (-not $pinLic) { Fail 'licenca do PyInstaller nao achada no dist-info' }
Copy-Item $pinLic.FullName (Join-Path $lic 'PyInstaller-COPYING.txt')
foreach ($p in @(@('vendor\LICENSE', 'threejs-LICENSE.txt'), @('vendor\mdi\LICENSE', 'MDI-LICENSE.txt'))) {
    $src = Join-Path $editor ('web\' + $p[0])
    if (-not (Test-Path $src)) { Fail "licenca nao achada: $src" }
    Copy-Item $src (Join-Path $lic $p[1])
}

Write-Host ("WotM_LevelEditor.exe: {0:N1} MB  ({1})" -f ((Get-Item $exe).Length / 1MB), $exe)
