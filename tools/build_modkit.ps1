# ABOUTME: Monta o WotM_ModKit.exe (tools/modkit_gui.py com Python, Tk e Pillow dentro, PyInstaller) para o zip do release.
# ABOUTME: Venv proprio fora do repo (<runtime>\modkit_build\venv); saida em <runtime>\modkit_build\dist\, com as licencas do que vai dentro.
#
# Uso: powershell -ExecutionPolicy Bypass -File tools\build_modkit.ps1 [-Runtime <pasta>]   (o tools\release.ps1 chama sozinho)
# Precisa do Python 3 no PATH (py ou python). Versoes fixas: PyInstaller e Pillow abaixo.
param([string]$Runtime = '')

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
function Fail([string]$m) { Write-Host "ERRO: $m" -ForegroundColor Red; exit 1 }

if (-not $Runtime) {
    $saved = Join-Path $repo '.wotm_recomp_dir'
    $Runtime = if (Test-Path $saved) { (Get-Content $saved -TotalCount 1).Trim() } else { Join-Path (Split-Path $repo -Parent) 'wotm-recomp-win' }
}
$root = Join-Path $Runtime 'modkit_build'
$venv = Join-Path $root 'venv'
$py = Join-Path $venv 'Scripts\python.exe'

# --- venv com as versoes fixas ------------------------------------------------------------------------------------------------------
$pins = 'pyinstaller==6.16.0', 'pillow==12.0.0'
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

# --- PyInstaller: um exe so, sem console ----------------------------------------------------------------------------------------------
$dist = Join-Path $root 'dist'
$work = Join-Path $root 'work'
$ico = Join-Path $repo 'native\wotm.ico'
Remove-Item -Recurse -Force $dist, $work -ErrorAction SilentlyContinue
& $py -m PyInstaller --noconfirm --clean --onefile --windowed --name WotM_ModKit --icon $ico --add-data "$ico;." `
    --paths (Join-Path $repo 'tools') --hidden-import ngp --distpath $dist --workpath $work --specpath $work `
    (Join-Path $repo 'tools\modkit_gui.py')
if ($LASTEXITCODE) { Fail 'PyInstaller falhou' }
$exe = Join-Path $dist 'WotM_ModKit.exe'
if (-not (Test-Path $exe)) { Fail "falta $exe" }

# --- licencas do que vai dentro do exe (Python, Tcl/Tk, Pillow, bootloader do PyInstaller) -------------------------------------------
$lic = Join-Path $dist 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
$basePrefix = (& $py -c 'import sys; print(sys.base_prefix)').Trim()
$site = (& $py -c 'import sysconfig; print(sysconfig.get_paths()[\"purelib\"])').Trim()
$pyLic = Join-Path $basePrefix 'LICENSE.txt'
if (-not (Test-Path $pyLic)) { Fail "licenca do Python nao achada: $pyLic" }
Copy-Item $pyLic (Join-Path $lic 'Python-LICENSE.txt')
$tcl = Get-ChildItem (Join-Path $basePrefix 'tcl') -Recurse -Filter 'license.terms' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $tcl) { Fail 'licenca do Tcl/Tk (tcl\...\license.terms) nao achada' }
Copy-Item $tcl.FullName (Join-Path $lic 'TclTk-license.terms.txt')
$pil = Get-ChildItem $site -Directory -Filter 'pillow-*.dist-info' | Select-Object -First 1
$pilLic = if ($pil) { Get-ChildItem $pil.FullName -Recurse -File | Where-Object { $_.Name -match '^LICENSE' } | Select-Object -First 1 }
if (-not $pilLic) { Fail 'licenca do Pillow nao achada no dist-info' }
Copy-Item $pilLic.FullName (Join-Path $lic 'Pillow-LICENSE.txt')
$pin = Get-ChildItem $site -Directory -Filter 'pyinstaller-*.dist-info' | Select-Object -First 1
$pinLic = if ($pin) { Get-ChildItem $pin.FullName -Recurse -File | Where-Object { $_.Name -match '^COPYING|^LICENSE' } | Select-Object -First 1 }
if (-not $pinLic) { Fail 'licenca do PyInstaller nao achada no dist-info' }
Copy-Item $pinLic.FullName (Join-Path $lic 'PyInstaller-COPYING.txt')

Write-Host ("WotM_ModKit.exe: {0:N1} MB  ({1})" -f ((Get-Item $exe).Length / 1MB), $exe)
