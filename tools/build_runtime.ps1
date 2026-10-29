# Chamado por build_runtime.bat: roda o build do runtime e mostra uma barra de progresso.
# Ninja so da poucos passos ([1/4] compila o ps2_runtime.cpp, que leva quase todo o tempo), entao a barra
# combina os passos do log com o tempo do ultimo build (guardado em <runtime>\.wotm_build_secs_<bat>).
# Uso: build_runtime.ps1 <pasta do runtime> <bat> <log>
param([string]$Dir, [string]$Bat, [string]$Log)

$ErrorActionPreference = 'Stop'
$logPath  = Join-Path $Dir $Log
New-Item -ItemType Directory -Force (Split-Path $logPath -Parent) | Out-Null   # logs\ do runtime: o .bat grava ali
$timeFile = Join-Path $Dir (".wotm_build_secs_" + [IO.Path]::GetFileNameWithoutExtension($Bat))

$expected = if ($Bat -eq 'build.bat') { 300.0 } else { 90.0 }
try { if (Test-Path $timeFile) { $expected = [double](Get-Content $timeFile -TotalCount 1) } } catch {}
if ($expected -lt 5) { $expected = 5 }

# Runtime "cobaia" (build_runtime.bat nm): mesmo runtime e hooks, mas o jogo traduzido vem de <repo>\recomp\nm (ELF com as funcoes
# reescritas; sh tools/wsl/recomp_nm.sh). Configura build_dev_nm a partir do configure_dev.bat e cria o build_dev_nm.bat na 1a vez.
if ($Bat -eq 'build_dev_nm.bat') {
    $repo = Split-Path $PSScriptRoot -Parent
    $nmDir = Join-Path $repo 'recomp\nm'
    if (-not (Test-Path (Join-Path $nmDir 'register_functions.cpp'))) { throw "recomp\nm nao existe: no WSL, sh tools/wsl/recomp_nm.sh" }
    $nmCmake = ($nmDir -replace '\\', '/')
    if (-not (Test-Path "$Dir\build_dev_nm\build.ninja")) {
        $cfg = Get-Content "$Dir\configure_dev.bat" -Raw
        $cfg = $cfg -replace 'build_dev"', "build_dev_nm`" -DPS2X_RUNNER_DIR=$nmCmake"
        Set-Content "$Dir\configure_dev_nm.bat" $cfg -NoNewline
        Write-Host "configurando build_dev_nm (jogo traduzido de $nmDir)..."
        & $env:ComSpec /c "`"$Dir\configure_dev_nm.bat`"" | Select-Object -Last 3
        if (-not (Test-Path "$Dir\build_dev_nm\build.ninja")) { throw "configure_dev_nm.bat falhou" }
    }
    if (-not (Test-Path "$Dir\$Bat")) {
        (Get-Content "$Dir\build_dev.bat" -Raw) -replace 'build_dev', 'build_dev_nm' | Set-Content "$Dir\$Bat" -NoNewline
    }
}
(Get-Item "$Dir\PS2Recomp\ps2xRuntime\src\lib\ps2_runtime.cpp").LastWriteTime = Get-Date
# microprogramas do VU0 em C++ (gerados do ELF; sem o .inc o runtime usa so o interpretador). Vao para <build>\wotm_gen, que o
# CMake do runtime poe no caminho de include (e codigo do jogo: fora do repo e fora da arvore do PS2Recomp)
$gen = Join-Path $PSScriptRoot 'vu0native.py'
$genDir = Join-Path $Dir ([IO.Path]::GetFileNameWithoutExtension($Bat)) | Join-Path -ChildPath 'wotm_gen'
New-Item -ItemType Directory -Force $genDir | Out-Null
# cobaia: hooks e leituras da camada nativa com os enderecos do ELF nmall (gerado pelo recomp_nm.sh; native/wotm_syms.h o usa se
# estiver no caminho de include, e so o wotm_gen do build_dev_nm o tem)
if ($Bat -eq 'build_dev_nm.bat') {
    $nmSyms = Join-Path $repo 'recomp\nm\wotm_syms_nm.h'
    if (-not (Test-Path $nmSyms)) { throw "falta $nmSyms (rode de novo sh tools/wsl/recomp_nm.sh)" }
    Copy-Item $nmSyms $genDir -Force
}
# versao do build (native/wotm_version.hpp: no log e no titulo da janela): a tag do release ou o commit, -dirty com mudanca local.
# So regrava quando muda (o ninja recompila quem inclui).
$repoDir = Split-Path $PSScriptRoot -Parent
# o build roda com um PATH minimo (o vcvars quebra com o normal): o git pode nao estar nele
$git = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $git) { $git = @("$env:ProgramFiles\Git\cmd\git.exe", "${env:ProgramFiles(x86)}\Git\cmd\git.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1 }
$ver = $null
if ($git) { try { $ver = (& $git -C $repoDir describe --tags --always --dirty 2>$null) } catch {} }
if (-not $ver) { $ver = 'dev'; Write-Host 'git nao achado: versao "dev"' -ForegroundColor Yellow }
$verFile = Join-Path $genDir 'wotm_version.gen.h'
$verText = "#define WOTM_VERSION `"$ver`"`r`n"
if (-not (Test-Path $verFile) -or (Get-Content $verFile -Raw) -ne $verText) { [IO.File]::WriteAllText($verFile, $verText) }
$inc = Join-Path $genDir 'wotm_vu0_native.gen.inc'
$elf = Join-Path $PSScriptRoot '..\disc\SCUS_971.97'
if ((Test-Path $elf) -and (-not (Test-Path $inc) -or (Get-Item $inc).LastWriteTime -lt (Get-Item $gen).LastWriteTime)) {
    & python $gen $elf $inc | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host 'vu0native.py falhou: VU0 fica no interpretador' -ForegroundColor Yellow }
}
# (o main.cpp ficava num lote do unity build com o jogo traduzido e o ninja nao o recompilava; desde 2026-10-19 o jogo traduzido e a
# biblioteca de objetos ps2_game_code e o main.cpp compila sozinho em cada executavel, ps2EntryRunner e wotm)
if (Test-Path $logPath) { Remove-Item $logPath -Force }

# a saida do proprio .bat (vcvars etc.) vai para arquivo para nao quebrar a linha da barra; so aparece se falhar
$outFile = Join-Path $env:TEMP 'wotm_build_runtime.out'
$errFile = Join-Path $env:TEMP 'wotm_build_runtime.err'
$proc = Start-Process -FilePath $env:ComSpec -ArgumentList "/c `"`"$Dir\$Bat`"`"" -NoNewWindow -PassThru `
    -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$null = $proc.Handle  # sem pegar o handle cedo, ExitCode volta vazio no PS 5.1
$sw = [Diagnostics.Stopwatch]::StartNew()
$full = [char]0x2588; $empty = [char]0x2591

function Read-Log {
    # o cmake ainda esta escrevendo: abre com compartilhamento para nao brigar pelo arquivo
    try {
        $fs = [IO.File]::Open($logPath, 'Open', 'Read', 'ReadWrite')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    } catch { return '' }
}

function Fmt([double]$s) { '{0}:{1:00}' -f [int][math]::Floor($s / 60), [int]($s % 60) }

function Draw([double]$frac, [string]$what) {
    $width = 30
    $n = [int][math]::Floor($frac * $width)
    $bar = ([string]$full * $n) + ([string]$empty * ($width - $n))
    $t = $sw.Elapsed.TotalSeconds
    $line = '  {0} {1,3}%  {2} / ~{3}  {4}' -f $bar, [int]($frac * 100), (Fmt $t), (Fmt ([math]::Max($expected, $t))), $what
    $max = 79
    try { $max = [Console]::WindowWidth - 1 } catch {}
    if ($line.Length -gt $max) { $line = $line.Substring(0, $max) }
    [Console]::Write("`r" + $line.PadRight($max))
}

$frac = 0.0; $what = 'preparando...'
while (-not $proc.HasExited) {
    $steps = [regex]::Matches((Read-Log), '(?m)^\[(\d+)/(\d+)\]\s*(.*)$')
    if ($steps.Count -gt 0) {
        $m = $steps[$steps.Count - 1]
        $done = [int]$m.Groups[1].Value; $total = [int]$m.Groups[2].Value
        $what = '[' + $done + '/' + $total + '] ' + ($m.Groups[3].Value -replace '^(Building|Linking) CXX (object|static library|executable) ', '')
        $what = $what -replace '^(\[\d+/\d+\] ).*[\\/]', '$1'
        # o ninja so escreve "[1/4] ... ps2_runtime.cpp.obj" quando a compilacao acaba
        if ($done -eq 0) { $what = 'compilando ps2_runtime.cpp' }
        $floor = if ($total -gt 0 -and $what -match 'ps2EntryRunner|wotm.exe') { 0.95 } elseif ($total -gt 0 -and $what -match '\.lib') { 0.9 } else { 0 }
    } else { $floor = 0 }
    # tempo vs. ultimo build, sem chegar a 100% antes de terminar
    $t = [math]::Min($sw.Elapsed.TotalSeconds / $expected, 0.99)
    $frac = [math]::Max([math]::Max($frac, $t), $floor)
    Draw $frac $what
    Start-Sleep -Milliseconds 250
}
$proc.WaitForExit()
$rc = $proc.ExitCode
# o build_dev.bat nao repassa a falha do ninja (sai com 0 e o .exe antigo fica): confere o log tambem
if ($rc -eq 0 -and (Read-Log) -match '(?m)^FAILED:|error C\d{4}|error LNK\d{4}') { $rc = 1 }
$secs = $sw.Elapsed.TotalSeconds

if ($rc -eq 0) {
    Draw 1.0 'pronto'
    [Console]::WriteLine()
    try { Set-Content -Path $timeFile -Value ([int][math]::Ceiling($secs)) -Encoding ascii } catch {}
} else {
    Draw $frac 'FALHOU'
    [Console]::WriteLine()
    foreach ($f in $outFile, $errFile) { if (Test-Path $f) { Get-Content $f | Where-Object { $_ -ne '' } } }
    # mostra os erros do compilador/linker em vez de so a ultima linha
    $errs = (Read-Log) -split "`r?`n" | Where-Object { $_ -match 'error|FAILED' } | Select-Object -First 20
    $errs | ForEach-Object { Write-Host $_ -ForegroundColor Red }
}
exit $rc
