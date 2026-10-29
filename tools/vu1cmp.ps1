# Checagem VU1 x nativo sem jogo, a partir de instantaneos (.vu1snap) gravados pelo runtime com WOTM_VU1CHECK_SNAP=<prefixo>.
# Roda `ps2EntryRunner --vu1cmp` (wotm_vu1check.inc) uma vez para todos os arquivos e imprime as linhas [vu1check] de cada um.
#   tools\vu1cmp.ps1 <arquivo.vu1snap | pasta> [-Frame <g_frame>] [-Dump <prefixo>] [-Full] [-Build build_dev]
#   Um arquivo guarda os quadros de uma sessao (gatilho/rajada: WOTM_VU1CHECK_WHEN, WOTM_VU1CHECK_BURST); todos sao comparados.
#   -Frame <g_frame>: so esse quadro do arquivo
#   -Full: todas as linhas [vu1check] (padrao: as linhas de resumo de cada quadro, objetos/atributos/particulas/sprites, e o inventario
#          inteiro, linhas "inv": o que so a referencia ou so o nativo desenha, por dono; ver docs/guides/VU1CHECK.md)
#   -Dump <prefixo>: grava os triangulos de cada checagem em <prefixo>_<g_frame>.csv (o mesmo WOTM_VU1CHECK_DUMP)
#   -NoPng: sem as imagens (padrao: <arquivo>_<g_frame>_{ref,nat,diff,trio}.png ao lado do instantaneo; ref = GS em software do
#           runtime, aproximacao do GS do PS2; nat = camada nativa; diff = pixels diferentes; trio = os tres lado a lado)
# A pasta do runtime vem de WOTM_RECOMP_DIR ou de .wotm_recomp_dir na raiz do repo (gravado pelo build_runtime.bat).
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Frame = 0,
    [string]$Dump = '',
    [switch]$Full,
    [switch]$NoPng,
    [string]$Build = 'build_dev'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$runtime = if ($env:WOTM_RECOMP_DIR) { $env:WOTM_RECOMP_DIR } elseif (Test-Path "$repo\.wotm_recomp_dir") { (Get-Content "$repo\.wotm_recomp_dir" -TotalCount 1).Trim() } else { '' }
if (-not $runtime -or -not (Test-Path "$runtime\$Build\ps2xRuntime\ps2EntryRunner.exe")) {
    Write-Error "Nao achei o runtime ($Build\ps2xRuntime\ps2EntryRunner.exe) em '$runtime': defina WOTM_RECOMP_DIR ou rode build_runtime.bat uma vez."
}
$exe = "$runtime\$Build\ps2xRuntime\ps2EntryRunner.exe"
$files = if (Test-Path $Path -PathType Container) { Get-ChildItem $Path -Filter '*.vu1snap' | Sort-Object { (($_.BaseName -split '_')[-1]) -as [int] }, Name | ForEach-Object { $_.FullName } } else { @((Resolve-Path $Path).Path) }
if (-not $files) { Write-Error "Nenhum .vu1snap em $Path" }
$err = Join-Path $env:TEMP 'wotm_vu1cmp.err.log'
$out = Join-Path $env:TEMP 'wotm_vu1cmp.out.log'
$rc = 0
# Uma execucao so para todos os arquivos (abrir o runtime custava mais que a comparacao); a saida de cada um comeca em "[vu1cmp] arquivo <caminho>"
$args = '--vu1cmp ' + (($files | ForEach-Object { "`"$_`"" }) -join ' ')
if ($Frame -gt 0) { $args += " --frame $Frame" }
if ($Dump) { $args += " --dump `"$Dump`"" }
if ($NoPng) { $args += " --nopng" }
# Start-Process redireciona sem passar pelo PowerShell (o `2>` do PS 5 grava UTF-16 e quebra as linhas longas)
$p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory "$runtime\discroot" -NoNewWindow -Wait -PassThru -RedirectStandardError $err -RedirectStandardOutput $out
$all = @(Get-Content $err)
$blocks = [ordered]@{}
foreach ($f in $files) { $blocks[$f] = New-Object System.Collections.Generic.List[string] }
$curBlock = $null
foreach ($l in $all) {
    if ($l -match '^\[vu1cmp\] arquivo (.+)$') { $curBlock = $blocks[$Matches[1]]; continue }
    if ($null -ne $curBlock) { $curBlock.Add($l) }
}
if ($p.ExitCode -ne 0 -and -not ($all -match '^\[vu1cmp\] arquivo ')) {   # o runtime nem abriu
    $all | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    exit 1
}
foreach ($f in $files) {
    $lines = $blocks[$f]
    Write-Host ("== {0}" -f (Split-Path $f -Leaf)) -ForegroundColor Cyan
    if (-not ($lines -match ': [1-9]\d* quadro\(s\) comparados$')) {   # o arquivo falhou (nao abriu, outro build, sem o quadro pedido)
        $rc = 1
        $lines | Where-Object { $_ -match '\[vu1cmp\]' } | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
        continue
    }
    # padrao: resumos de cada quadro e o inventario inteiro (linhas "inv": o que so um lado desenha, por dono)
    $sel = if ($Full) { $lines | Where-Object { $_ -match '\[vu1check\]|\[vu1cmp\] (imagens|sem quadro|a camada)' } } else { $lines | Where-Object { $_ -match '\[vu1check\] (g=\d+ |inv )|\[vu1cmp\] (imagens|sem quadro|a camada)' } }
    $sel | ForEach-Object { $_ -replace '^\[vu1check\] ', '' }
}
exit $rc
