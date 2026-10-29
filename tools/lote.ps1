# Lote de lutas IA contra IA no runtime (WOTM_MATCH + WOTM_AIVSAI), para achar bugs sem ninguem olhando.
# Cada combinacao (monstro do jogador x primeira IA x fase) roda por -Secs segundos numa execucao propria: a luta e continua
# (m_killTarget 0, o derrubado volta sozinho) e cada rodada sai no log como [partida]. O numero de IAs gira por -AIs ao longo do
# lote (combinacao n usa o elemento n mod tamanho); as IAs alem da primeira sao sorteadas de -Opponents com a semente -Seed, entao o
# mesmo comando repete o mesmo lote. O vigia do runtime grava [vigia] CONGELOU (com ram_/vram_vigia_*.bin) e [vigia] LENTO; uma
# queda sai como [CRASH] ou como o processo terminando antes da hora.
# A cada -Check segundos o runtime compara, num quadro, o que o microcodigo do VU1 desenharia com o que a camada nativa desenha,
# por objeto e em numeros (WOTM_VU1CHECK, linhas [vu1check]; ver docs/guides/NATIVE.md).
# Saida em <runtime>\lote\<data_hora>\: <jogador>_<ias com +>_<fase>.log (linhas [partida]/[vigia]/[CRASH]/[vu1check] e a fase/monstros
# carregados), .png (a tela ~12 s antes do fim), os dumps do vigia e resumo.csv.
#
# Uso: powershell -File tools\lote.ps1 [-Monsters 1,2,3] [-Opponents 1,2,3] [-Levels 1,2] [-AIs 1,2,3] [-Seed 1] [-Secs 180] [-Max 0] [-Check 1]
#   Indices: monstros e fases como no GLOSSARY.md (MonsterLongNames, GameLevelNames). Sem -Opponents, usa a mesma lista de -Monsters.
#   -AIs: quantidades de IAs (1 a 7) que se alternam. -Max N para depois de N combinacoes (0 = todas). -Check 0 desliga a comparacao.
#   As listas sao texto separado por virgula (com -File o PowerShell nao converte "1,2" em lista).
param(
    [string]$Monsters = '1,2,3,4,5,7,8,9,10,11',
    [string]$Opponents = '',
    [string]$Levels = '1,2,4,5,7,8,9,10,13,14,15,16',
    [string]$AIs = '1,2,3',
    [int]$Seed = 1,
    [int]$Secs = 180,
    [int]$Max = 0,
    [int]$Check = 1,
    [string]$Build = 'build_dev'   # build_dev_nm = runtime cobaia com as funcoes reescritas (build_runtime.bat nm); rode o mesmo lote nos dois e compare o resumo.csv
)
$ErrorActionPreference = 'Stop'
function Split-Ints([string]$s) { @($s -split ',' | Where-Object { $_.Trim() } | ForEach-Object { [int]$_.Trim() }) }
$monList = Split-Ints $Monsters
$oppList = Split-Ints $Opponents
$levList = Split-Ints $Levels
$aiCounts = Split-Ints $AIs
if (@($aiCounts | Where-Object { $_ -lt 1 -or $_ -gt 7 }).Count) { throw "-AIs aceita de 1 a 7." }
$repo = Split-Path $PSScriptRoot -Parent
$runtime = if ($env:WOTM_RECOMP_DIR) { $env:WOTM_RECOMP_DIR } elseif (Test-Path "$repo\.wotm_recomp_dir") { (Get-Content "$repo\.wotm_recomp_dir" -TotalCount 1).Trim() } else { '' }
if (-not $runtime -or -not (Test-Path "$runtime\run_live.ps1")) { throw "Pasta do runtime nao encontrada: rode run_live.bat uma vez ou defina WOTM_RECOMP_DIR." }
if ($oppList.Count -eq 0) { $oppList = $monList }

$out = Join-Path $runtime ("lote\" + (Get-Date -Format 'yyyy-MM-dd_HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$disc = "$runtime\discroot"
$summary = Join-Path $out 'resumo.csv'
'jogador,ias,fase,segundos,rodadas,vitorias,congelou,lento,crash,saiu_antes,checagens,casados_pior,objetos_divergentes,textura_difere_pior,objetos_textura,fx_casados_pior,fx_tipos_divergentes,vif1_fora_de_passo,uv_difere_pior,cor_difere_pior,alfa_difere_pior,abe_difere_pior,alpha_difere_pior' | Set-Content $summary -Encoding ASCII

# Cada combinacao: @(jogador, ia1, [ia2, [ia3,]] fase)
$rng = New-Object System.Random $Seed
$combos = New-Object System.Collections.Generic.List[int[]]
foreach ($l in $levList) {
    foreach ($a in $monList) {
        foreach ($b in $oppList) {
            $k = $aiCounts[$combos.Count % $aiCounts.Count]
            $c = @($a, $b)
            for ($i = 1; $i -lt $k; $i++) { $c += $oppList[$rng.Next($oppList.Count)] }
            $combos.Add([int[]]($c + $l))
        }
    }
}
if ($Max -gt 0 -and $combos.Count -gt $Max) { $combos = $combos.GetRange(0, $Max) }
$total = $combos.Count
Write-Host "Lote: $total combinacoes de $Secs s (~$([math]::Ceiling($total * ($Secs + 10) / 60)) min). Saida: $out"

$env:WOTM_REPO = $repo
$env:WOTM_AIVSAI = '1'
$env:WOTM_SETTINGS = '0'   # sem o settings.ini do painel/launcher: lote igual em toda maquina
if ($Check -gt 0) { $env:WOTM_VU1CHECK = [string]$Check } else { Remove-Item Env:WOTM_VU1CHECK -ErrorAction SilentlyContinue }
$n = 0
foreach ($c in $combos) {
    $n++
    $player = $c[0]; $level = $c[-1]
    $foes = $c[1..($c.Count - 2)]
    $tag = '{0}_{1}_{2}' -f $player, ($foes -join '+'), $level
    $env:WOTM_MATCH = $c -join ','
    Write-Host ("[{0}/{1}] jogador {2} contra {3}, fase {4}" -f $n, $total, $player, ($foes -join ' '), $level)
    Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-ChildItem $disc -Filter '*_vigia_*.bin' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem $disc -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
    $env:PS2X_SHOT_FRAMES = [string][math]::Max(60, ($Secs - 12) * 60)   # uma captura perto do fim (quadros da tela, ~60/s)
    $start = Get-Date
    Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$runtime\run_live.ps1`" -Auto 0 -Build $Build" -WindowStyle Minimized | Out-Null
    Start-Sleep -Seconds 5
    $early = $false
    while (((Get-Date) - $start).TotalSeconds -lt $Secs) {
        if (-not (Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue)) { $early = $true; break }
        Start-Sleep -Seconds 2
    }
    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2

    $liveLog = if (Test-Path "$runtime\logs\live.err.log") { "$runtime\logs\live.err.log" } else { "$runtime\live.err.log" }   # run_live.ps1 antigo grava na raiz
    $lines = @(Select-String -Path $liveLog -Pattern '\[partida\]|\[wotm\] carga|\[vigia\]|\[CRASH\]|\[native\] recursos|\[vu1check\]|\[flare\]' | ForEach-Object { $_.Line })
    $lines | Set-Content (Join-Path $out "$tag.log") -Encoding UTF8
    Get-ChildItem $disc -Filter '*_vigia_*.bin' -ErrorAction SilentlyContinue | ForEach-Object { Move-Item $_.FullName (Join-Path $out ("{0}_{1}" -f $tag, $_.Name)) -Force }
    Get-ChildItem $disc -Filter 'shot_*.png' -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { Move-Item $_.FullName (Join-Path $out "$tag.png") -Force }

    $rounds = @($lines | Where-Object { $_ -match 'rodada \d+ terminou' })
    $wins = ''   # vitorias de cada monstro na ultima rodada, jogador primeiro, separadas por /
    if ($rounds.Count -gt 0) { $wins = (@([regex]::Matches($rounds[-1], 'vitorias (-?\d+);') | ForEach-Object { $_.Groups[1].Value }) -join '/') }
    $frozen = @($lines | Where-Object { $_ -match 'CONGELOU' }).Count
    $slow = @($lines | Where-Object { $_ -match 'LENTO' }).Count
    $crash = @($lines | Where-Object { $_ -match '\[CRASH\]' }).Count
    # comparacao VU1 x nativo: quantas checagens, o pior % de triangulos do VU1 casados e quantos objetos (polyPkt) apareceram com diferenca
    $checks = @($lines | ForEach-Object { if ($_ -match '\[vu1check\] g=\d+ objetos .* casados ([\d.]+)%') { [double]$Matches[1] } })
    $worst = if ($checks.Count) { ($checks | Measure-Object -Minimum).Minimum.ToString([cultureinfo]::InvariantCulture) } else { '' }   # ponto decimal (CSV)
    $diverging = @($lines | ForEach-Object { if ($_ -match '\[vu1check\]\s+pkt ([0-9a-f]+)') { $Matches[1] } } | Sort-Object -Unique).Count
    # textura: o pior numero de triangulos casados com TEX0 diferente do nativo num quadro e quantos objetos tiveram textura diferente
    $texDiff = @($lines | ForEach-Object { if ($_ -match '\[vu1check\] g=\d+ .* textura difere (\d+)') { [int]$Matches[1] } })
    $texWorst = if ($texDiff.Count) { ($texDiff | Measure-Object -Maximum).Maximum } else { '' }
    $texObjs = @($lines | ForEach-Object { if ($_ -match '\[vu1check\]\s+textura pkt ([0-9a-f]+)') { $Matches[1] } } | Sort-Object -Unique).Count
    # particulas: o pior % de quadrados do VU1 casados num quadro (so quadros com particulas) e os tipos de sistema que divergiram
    $fx = @($lines | ForEach-Object { if ($_ -match '\[vu1check\] g=\d+ particulas: .* quadrados vu (\d+) .* casados ([\d.]+)%') { if ([int]$Matches[1] -gt 0) { [double]$Matches[2] } } })
    $fxWorst = if ($fx.Count) { ($fx | Measure-Object -Minimum).Minimum.ToString([cultureinfo]::InvariantCulture) } else { '' }
    $fxTypes = (@($lines | ForEach-Object { if ($_ -match '\[vu1check\]\s+fx \d+ preset \d+ tipo (\d+)') { $Matches[1] } } | Sort-Object { [int]$_ } -Unique) -join '/')
    # codigos VIF1 invalidos desde o boot (o VIF1 saiu de passo e leu dados como comandos; pista 5 do VU1CHECK.md): a maior contagem vista
    $vifBad = @($lines | ForEach-Object { if ($_ -match '\[vu1check\] g=\d+ objetos .* VIF1 fora de passo (\d+)') { [int]$Matches[1] } })
    $vifBadMax = if ($vifBad.Count) { ($vifBad | Measure-Object -Maximum).Maximum } else { '' }
    # atributos dos triangulos casados (uv, cor, alfa, ABE, ALPHA): o pior numero de triangulos diferentes num quadro
    $attrWorst = foreach ($name in 'uv difere', 'cor difere', 'alfa difere', 'ABE difere', 'ALPHA difere') {
        $v = @($lines | ForEach-Object { if ($_ -match ('\[vu1check\] g=\d+ atributos: .*\| ' + $name + ' (\d+)')) { [int]$Matches[1] } })
        if ($v.Count) { ($v | Measure-Object -Maximum).Maximum } else { '' }
    }
    '{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15},{16},{17},{18},{19},{20},{21},{22}' -f $player, ($foes -join '+'), $level, $elapsed, $rounds.Count, $wins, $frozen, $slow, $crash,
        [int]$early, $checks.Count, $worst, $diverging, $texWorst, $texObjs, $fxWorst, $fxTypes, $vifBadMax, $attrWorst[0], $attrWorst[1], $attrWorst[2], $attrWorst[3], $attrWorst[4] | Add-Content $summary -Encoding ASCII
    if ($frozen -or $crash -or $early) { Write-Host ("   PROBLEMA: congelou={0} crash={1} saiu_antes={2}" -f $frozen, $crash, [int]$early) -ForegroundColor Red }
    else { Write-Host ("   ok: {0} rodadas, lento={1}" -f $rounds.Count, $slow) }
}
Remove-Item Env:WOTM_MATCH, Env:WOTM_AIVSAI, Env:WOTM_VU1CHECK, Env:PS2X_SHOT_FRAMES -ErrorAction SilentlyContinue
Write-Host "Resumo: $summary"
