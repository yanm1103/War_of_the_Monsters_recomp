# Smoke dos monstros de mods/: uma luta curta de IA com cada monstro custom, sem ninguem olhando, e a leitura do log atras do que
# nunca deveria acontecer: as linhas [check] dos invariantes do runtime (native/wotm_check.h; NATIVE.md, "Runtime invariants"),
# queda ([CRASH]), congelamento ([vigia] CONGELOU), o jogo saindo antes da hora e a luta que nao comecou.
# Com -MoveLog cada luta roda com o treino ligado e grava a telemetria de golpe (WOTM_HITLOG), que o tools/move_measure.py compara
# com o previsto pelo move_check.
# A janela abre no segundo monitor, sem som e sem roubar o foco.
# Saida em <runtime>\smoke\<data_hora>\: <monstro>.log (as linhas que interessam), <monstro>.moves.csv (-MoveLog), resumo.csv e
# smoke.json (o que o tools/check.py le: uma entrada por achado).
#
# Uso: powershell -File tools\smoke.ps1 [-Monsters godzilla_ste,cj] [-Opponent 1] [-Level 1] [-Secs 70] [-MoveLog] [-Out <pasta>]
#   Sem -Monsters: todo mods\*\monsters\*\monster.json. -Opponent e -Level: indices do GLOSSARY.md (MonsterLongNames, GameLevelNames).
param(
    [string]$Monsters = '',
    [string]$Opponent = '1',
    [int]$Level = 1,
    [int]$Secs = 70,
    [switch]$MoveLog,
    [string]$Out = '',
    [string]$Build = 'build_dev'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$runtime = if ($env:WOTM_RECOMP_DIR) { $env:WOTM_RECOMP_DIR } elseif (Test-Path "$repo\.wotm_recomp_dir") { (Get-Content "$repo\.wotm_recomp_dir" -TotalCount 1).Trim() } else { '' }
if (-not $runtime -or -not (Test-Path "$runtime\run_live.ps1")) { throw "Pasta do runtime nao encontrada: rode run_live.bat uma vez ou defina WOTM_RECOMP_DIR." }

$ids = @($Monsters -split ',' | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
if ($ids.Count -eq 0) {
    $ids = @(Get-ChildItem "$repo\mods" -Directory | Where-Object { -not $_.Name.StartsWith('_') } | ForEach-Object {
            Get-ChildItem (Join-Path $_.FullName 'monsters') -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'monster.json') } | ForEach-Object {
                $j = Get-Content (Join-Path $_.FullName 'monster.json') -Raw | ConvertFrom-Json
                if ($j.id) { [string]$j.id } else { $_.Name }
            }
        } | Sort-Object -Unique)
}
if ($ids.Count -eq 0) { throw "Nenhum monstro em mods\*\monsters." }
if (-not $Out) { $Out = Join-Path $runtime ("smoke\" + (Get-Date -Format 'yyyy-MM-dd_HHmmss')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'resumo.csv'
'monstro,segundos,rodadas,checks,crash,congelou,saiu_antes,sem_luta,achados' | Set-Content $summary -Encoding ASCII
Write-Host "Smoke: $($ids.Count) monstros de $Secs s (~$([math]::Ceiling($ids.Count * ($Secs + 10) / 60)) min). Saida: $Out"

$saved = @{}
$vars = @{ WOTM_REPO = $repo; WOTM_AIVSAI = '1'; WOTM_CHEATS = 'inv,esp'; WOTM_FULLSCREEN = '0'; WOTM_NOFOCUS = '1'; WOTM_MOUSECAM = '0'; WOTM_MONITOR = '1'; WOTM_MUTE = '1'; WOTM_FOTOS = '0'; WOTM_FORCESPECIAL = '12' }
if ($MoveLog) { $vars.WOTM_TRAINING = '1'; $vars.WOTM_TRAINING_DUMMY = '1'; $vars.Remove('WOTM_CHEATS') }
foreach ($k in @($vars.Keys) + 'WOTM_MATCH', 'WOTM_HITLOG', 'WOTM_CHEATS') { $saved[$k] = [Environment]::GetEnvironmentVariable($k) }
foreach ($k in $vars.Keys) { [Environment]::SetEnvironmentVariable($k, $vars[$k]) }
if ($MoveLog) { Remove-Item Env:WOTM_CHEATS -ErrorAction SilentlyContinue }

$all = New-Object System.Collections.Generic.List[object]
$n = 0
try {
    foreach ($id in $ids) {
        $n++
        Write-Host ("[{0}/{1}] {2}" -f $n, $ids.Count, $id)
        $env:WOTM_MATCH = "$id,$Opponent,$Level"
        $moves = Join-Path $Out "$id.moves.csv"
        if ($MoveLog) { $env:WOTM_HITLOG = $moves }
        Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue | Stop-Process -Force
        $start = Get-Date
        Start-Process powershell -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$runtime\run_live.ps1`" -Auto 0 -Build $Build" -WindowStyle Hidden | Out-Null
        Start-Sleep -Seconds 8
        $early = $false
        while (((Get-Date) - $start).TotalSeconds -lt $Secs) {
            if (-not (Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue)) { $early = $true; break }
            Start-Sleep -Seconds 2
        }
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        Get-Process ps2EntryRunner, wotm -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Seconds 2

        $liveLog = if (Test-Path "$runtime\logs\live.err.log") { "$runtime\logs\live.err.log" } else { "$runtime\live.err.log" }
        $lines = @(Select-String -Path $liveLog -Pattern '\[check\]|\[partida\]|\[vigia\] CONGELOU|\[CRASH\]|\[custom\]|\[mods\]' | ForEach-Object { $_.Line })
        $lines | Set-Content (Join-Path $Out "$id.log") -Encoding UTF8

        $found = New-Object System.Collections.Generic.List[object]
        # [check] <chave> n=<vezes> | <detalhe>: a ultima linha de cada chave traz a maior contagem
        $checks = @{}
        foreach ($l in $lines) {
            if ($l -match '\[check\] (\S+) n=(\d+) \| (.*)$') { $checks[$Matches[1]] = @{ n = [int]$Matches[2]; detail = $Matches[3] } }
        }
        foreach ($k in ($checks.Keys | Sort-Object)) { $found.Add([pscustomobject]@{ monster = $id; code = $k; count = $checks[$k].n; text = $checks[$k].detail }) }
        $crash = @($lines | Where-Object { $_ -match '\[CRASH\]' }).Count
        $frozen = @($lines | Where-Object { $_ -match 'CONGELOU' }).Count
        $fights = @($lines | Where-Object { $_ -match '\[partida\] \d+: monstro' }).Count
        $rounds = @($lines | Where-Object { $_ -match 'rodada \d+ terminou' }).Count
        if ($crash) { $found.Add([pscustomobject]@{ monster = $id; code = 'crash'; count = $crash; text = 'the game crashed ([CRASH] in the log)' }) }
        if ($frozen) { $found.Add([pscustomobject]@{ monster = $id; code = 'frozen'; count = $frozen; text = 'the game froze ([vigia] CONGELOU)' }) }
        if ($early -and -not $crash) { $found.Add([pscustomobject]@{ monster = $id; code = 'exited'; count = 1; text = "the game closed by itself after $elapsed s" }) }
        if (-not $fights) { $found.Add([pscustomobject]@{ monster = $id; code = 'no_fight'; count = 1; text = 'the fight never started (no [partida] line): unknown monster id or a load that failed' }) }
        # cobertura: quem traz sopro proprio e nao soprou na luta nao teve o sopro conferido
        $hasBreath = @($lines | Where-Object { $_ -match "\[mods\] monstro .*[/\\]$([regex]::Escape($id)): sopro proprio" }).Count
        $breathed = @($lines | Where-Object { $_ -match '\[custom\] sopro proprio de' }).Count
        if ($fights -and $hasBreath -and -not $breathed) { $found.Add([pscustomobject]@{ monster = $id; code = 'breath_not_seen'; count = 1; text = "the monster has its own breath and never used it in $elapsed s: the breath was not checked (raise -Secs)" }) }
        foreach ($f in $found) { $all.Add($f) }
        '{0},{1},{2},{3},{4},{5},{6},{7},{8}' -f $id, $elapsed, $rounds, $checks.Count, $crash, $frozen, [int]$early, [int](-not $fights), (($found | ForEach-Object { $_.code }) -join ' ') | Add-Content $summary -Encoding ASCII
        if ($found.Count) { Write-Host ("   ACHADOS: " + (($found | ForEach-Object { "$($_.code) x$($_.count)" }) -join ', ')) -ForegroundColor Yellow }
        else { Write-Host '   ok' }
    }
}
finally {
    foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
}
$json = Join-Path $Out 'smoke.json'
[pscustomobject]@{ monsters = $ids; secs = $Secs; findings = $all.ToArray() } | ConvertTo-Json -Depth 5 | Set-Content $json -Encoding UTF8
Write-Host "Resumo: $summary"
Write-Host "JSON: $json"
