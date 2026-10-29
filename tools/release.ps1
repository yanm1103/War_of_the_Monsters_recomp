# ABOUTME: Monta o zip do release (item 14 do TODO.md): wotm.exe do build_dev, PDB, DLLs, mods, licencas e o texto do jogador.
# ABOUTME: Confere que o exe e do commit atual e que nada do disco entrou (so os MECHA.* do mod Mecha, decisao do dono).
#
# Uso: powershell -ExecutionPolicy Bypass -File tools\release.ps1 [-Runtime <pasta>] [-Build build_dev] [-Out <pasta>] [-AllowDirty]
# Antes: a tag do release no repo (git tag v0.1.0) e build_runtime.bat, para o exe sair com a versao certa (wotm_version.hpp).
# Sai em <runtime>\release\: WotM-<versao>-win64.zip e .sha256. Fora do repo: o exe tem o codigo traduzido do jogo.
param([string]$Runtime = '', [string]$Build = 'build_dev', [string]$Out = '', [switch]$AllowDirty)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
function Fail([string]$m) { Write-Host "ERRO: $m" -ForegroundColor Red; exit 1 }

# --- pastas -----------------------------------------------------------------------------------------------------------------------
if (-not $Runtime) {
    $saved = Join-Path $repo '.wotm_recomp_dir'
    $Runtime = if (Test-Path $saved) { (Get-Content $saved -TotalCount 1).Trim() } else { Join-Path (Split-Path $repo -Parent) 'wotm-recomp-win' }
}
$bin = Join-Path $Runtime "$Build\ps2xRuntime"
$exe = Join-Path $bin 'wotm.exe'
if (-not (Test-Path $exe)) { Fail "nao achei $exe (rode build_runtime.bat)" }
if (-not $Out) { $Out = Join-Path $Runtime 'release' }
$outFull = [IO.Path]::GetFullPath($Out)
if (($outFull + '\').StartsWith([IO.Path]::GetFullPath($repo).TrimEnd('\') + '\',[StringComparison]::OrdinalIgnoreCase)) { Fail "a saida ($outFull) nao pode ficar dentro do repo" }

# --- versao: o exe tem que ser do estado atual do repo ------------------------------------------------------------------------------
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) { $git = @("$env:ProgramFiles\Git\cmd\git.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1 }
if (-not $git) { Fail 'git nao achado' }
$now = (& $git -C $repo describe --tags --always --dirty).Trim()
$verFile = Join-Path $Runtime "$Build\wotm_gen\wotm_version.gen.h"
if (-not (Test-Path $verFile)) { Fail "falta $verFile (compile com build_runtime.bat)" }
$built = ([regex]::Match((Get-Content $verFile -Raw), '"([^"]+)"')).Groups[1].Value
if ($built -ne $now) { Fail "o exe e da versao '$built' e o repo esta em '$now': rode build_runtime.bat de novo" }
if ($now -like '*-dirty' -and -not $AllowDirty) { Fail "repo com mudancas locais ($now): commite antes (ou -AllowDirty para teste)" }
if ((Get-Item $exe).LastWriteTime -lt (Get-Item $verFile).LastWriteTime) { Fail 'o wotm.exe e mais velho que a versao gravada: o ultimo build falhou?' }

# exe de janela (sem console): subsistema 2 no cabecalho PE
$b = [IO.File]::ReadAllBytes($exe)
$sub = [BitConverter]::ToUInt16($b, [BitConverter]::ToInt32($b, 0x3C) + 24 + 68)
if ($sub -ne 2) { Fail "wotm.exe com subsistema $sub (esperado 2, janela): o CMake do runtime esta sem o /SUBSYSTEM:WINDOWS do alvo wotm" }

# --- ferramentas do Visual Studio: dumpbin (dependencias) e o runtime do MSVC -------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Fail 'vswhere.exe nao achado (Visual Studio ou Build Tools)' }
$dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -Last 1
$crt = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT' | Where-Object { $_ -notmatch 'onecore' } | Select-Object -Last 1
if (-not $dumpbin) { Fail 'dumpbin.exe nao achado' }
if (-not $crt) { Fail 'pasta Microsoft.VC14x.CRT do redist nao achada' }

# --- DLLs: fechamento das dependencias do exe ---------------------------------------------------------------------------------------
# da pasta do build (FFmpeg e o que ele puxa) ou do redist do MSVC; o resto tem que ser do Windows (System32 ou api-ms-win-*)
$sys32 = Join-Path $env:WINDIR 'System32'
$dlls = [ordered]@{}
$queue = New-Object System.Collections.Queue
$queue.Enqueue($exe)
while ($queue.Count) {
    $f = $queue.Dequeue()
    $deps = & $dumpbin /nologo /dependents $f | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] }
    foreach ($d in $deps) {
        $key = $d.ToLower()
        if ($dlls.Contains($key)) { continue }
        $local = Join-Path $bin $d
        $fromCrt = Join-Path $crt $d
        if (Test-Path $local) { $dlls[$key] = $local; $queue.Enqueue($local) }
        elseif (Test-Path $fromCrt) { $dlls[$key] = $fromCrt }
        elseif ($key -like 'api-ms-win-*' -or (Test-Path (Join-Path $sys32 $d))) { continue }
        else { Fail "dependencia $d (de $(Split-Path $f -Leaf)) nao esta no build, no redist nem no Windows" }
    }
}

# --- pasta do pacote ----------------------------------------------------------------------------------------------------------------
$name = "WotM-$now-win64"
$stage = Join-Path $outFull $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
$pdb = Join-Path $bin 'wotm.pdb'
if (-not (Test-Path $pdb)) { Fail "falta $pdb" }
Copy-Item $pdb $stage   # no zip, decisao do dono: quem relata uma queda le a pilha com nomes
foreach ($d in $dlls.Values) { Copy-Item $d $stage }

# mods: os arquivos que o git rastreia em mods\ e os quatro do Mecha gerados do disco (tools/mechamon.py; no .gitignore)
$mecha = 'mods/Mecha/monsters/mecha/files'
$mechaFiles = 'MECHA.NGP', 'MECHA.PTR', 'MECHA.TEX', 'MECHA.RTX' | ForEach-Object { "$mecha/$_" }
$modFiles = @(& $git -C $repo ls-files mods) + $mechaFiles
foreach ($m in $modFiles) {
    $src = Join-Path $repo $m
    if (-not (Test-Path $src)) { Fail "falta $m (o Mecha: python tools/mechamon.py mods/Mecha/monsters/mecha/files)" }
    $dst = Join-Path $stage $m
    New-Item -ItemType Directory -Force (Split-Path $dst -Parent) | Out-Null
    Copy-Item $src $dst
}

# texto do jogador e licencas
Copy-Item (Join-Path $PSScriptRoot 'release\README.txt') $stage
Copy-Item (Join-Path $PSScriptRoot 'release\THIRD_PARTY.txt') $stage
$lic = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'release\licenses\*') $lic
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $lic 'WotM-GPL-3.0.txt')
$fromBuild = @{
    'PS2Recomp-LICENSE.md' = Join-Path $Runtime 'PS2Recomp\LICENSE'
    'raylib-LICENSE.txt'   = Join-Path $Runtime "$Build\_deps\raylib-src\LICENSE"
    'GLFW-LICENSE.md'      = Join-Path $Runtime "$Build\_deps\raylib-src\src\external\glfw\LICENSE.md"
}
foreach ($k in $fromBuild.Keys) {
    if (-not (Test-Path $fromBuild[$k])) { Fail "licenca nao achada: $($fromBuild[$k])" }
    Copy-Item $fromBuild[$k] (Join-Path $lic $k)
}
# cada arquivo citado no THIRD_PARTY.txt tem que estar no pacote
$cited = [regex]::Matches((Get-Content (Join-Path $stage 'THIRD_PARTY.txt') -Raw), 'licenses\\([\w.\-]+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
foreach ($c in $cited) { if (-not (Test-Path (Join-Path $lic $c))) { Fail "THIRD_PARTY.txt cita licenses\$c, que nao esta no pacote" } }
foreach ($d in $dlls.Keys) { if ((Get-Content (Join-Path $stage 'THIRD_PARTY.txt') -Raw) -notmatch [regex]::Escape($d)) { Fail "a DLL $d nao esta listada no THIRD_PARTY.txt" } }

# --- nada do disco nem do PC de quem empacota ---------------------------------------------------------------------------------------
$allowed = $mechaFiles | ForEach-Object { ($_ -replace '/', '\').ToLower() }
$gameExt = '.ngp', '.ptr', '.tex', '.rtx', '.pss', '.snd', '.vb', '.vh', '.hd', '.bd', '.glb', '.irx', '.iso', '.elf', '.bin', '.ico'
$bad = @()
foreach ($f in Get-ChildItem $stage -Recurse -File) {
    $rel = $f.FullName.Substring($stage.Length + 1)
    $low = $rel.ToLower()
    if ($allowed -contains $low) { continue }
    if ($f.Name -like 'SCUS_*' -or $f.Name -like 'BASCUS*' -or $gameExt -contains $f.Extension.ToLower()) { $bad += $rel }
    if ($f.Name -in 'save.json', 'settings.ini', 'wotm.ini' -or $f.Extension -eq '.log') { $bad += $rel }
}
foreach ($dir in Get-ChildItem $stage -Recurse -Directory) {
    if ($dir.Name -in 'discroot', 'disc', 'mc0', 'mc1', 'screenshots', 'MON', 'LVL', 'MOV') { $bad += $dir.FullName.Substring($stage.Length + 1) + '\' }
}
if ($bad) { Fail ("arquivos que nao podem ir no zip:`n  " + ($bad -join "`n  ")) }

# --- zip e SHA256 -------------------------------------------------------------------------------------------------------------------
$zip = Join-Path $outFull "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# entrada por entrada: o CreateFromDirectory do Windows PowerShell 5.1 grava '\' nos nomes, e fora do Explorer o zip sai sem pastas
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$za = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in Get-ChildItem $stage -Recurse -File) {
        $entry = $name + '/' + ($f.FullName.Substring($stage.Length + 1) -replace '\\', '/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $za.Dispose() }
$sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Set-Content -Path "$zip.sha256" -Value "$sha  $name.zip" -Encoding ascii

$files = Get-ChildItem $stage -Recurse -File
Write-Host ("{0}: {1} arquivos, {2:N1} MB descompactado, zip {3:N1} MB" -f $name, $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), ((Get-Item $zip).Length / 1MB))
Write-Host ("DLLs: " + (($dlls.Values | ForEach-Object { Split-Path $_ -Leaf }) -join ', '))
Write-Host "zip:    $zip"
Write-Host "sha256: $sha"
