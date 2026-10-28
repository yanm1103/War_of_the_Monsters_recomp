# Teste das saidas na campanha co-op (wotm_coop.inc, WOTM_COOPKILL): abre a fase 1 da aventura com todos os jogadores como bots
# (WOTM_AIVSAI=1, sem mouse preso), 1 vida cada, derruba os jogadores na ordem dada e, a cada dump [cooplog], tira uma foto da
# janela. Fotos e log em <Out>\<ordem>\. Uso: tools\coop_mortes.ps1 -Ordem 23,1234 [-Monitor 1] [-Segundos 6] [-Out <pasta>] (Monitor 1 = segundo monitor)
param(
    [string[]]$Ordem = @("23"),
    [int]$Monitor = 1,
    [int]$Jogadores = 4,
    [int]$Segundos = 6,
    [string]$Match = "",
    [string]$Out = ""
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$runtime = (Get-Content (Join-Path $repo ".wotm_recomp_dir") -ErrorAction SilentlyContinue | Select-Object -First 1)
if (-not $runtime) { $runtime = Join-Path (Split-Path -Parent $repo) "wotm-recomp-win" }
$log = Join-Path $runtime "logs\live.err.log"
if (-not $Out) { $Out = Join-Path $runtime ("coop_mortes\" + (Get-Date -Format "yyyy-MM-dd_HHmmss")) }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CoopWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
"@
[CoopWin]::SetProcessDPIAware() | Out-Null

# Foto do conteudo da janela do jogo (PrintWindow com PW_RENDERFULLCONTENT): nunca da tela, que pode ter outra janela por cima
function Shot([string]$file) {
    $p = Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { return }
    $r = New-Object CoopWin+RECT
    [CoopWin]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($w -le 0 -or $h -le 0) { return }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [CoopWin]::PrintWindow($p.MainWindowHandle, $hdc, 3)   # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    if (-not $ok) { $g.Dispose(); $bmp.Dispose(); return }
    # metade do tamanho: basta para conferir o layout
    $small = New-Object System.Drawing.Bitmap $bmp, ([int]($w / 2)), ([int]($h / 2))
    $small.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose(); $small.Dispose()
}

foreach ($o in $Ordem) {
    $dir = Join-Path $Out $o
    New-Item -ItemType Directory -Force $dir | Out-Null
    Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $mons = @("3", "1", "2", "4")[0..($Jogadores - 1)]
    $env:WOTM_MATCH = $(if ($Match) { $Match } else { ($mons -join ",") + ",1" }); $env:WOTM_MODE = "1"; $env:WOTM_PLAYERS = "$Jogadores"
    # todos os jogadores sao bots e sem mouse preso: o teste roda sem tocar no teclado nem no cursor de quem esta usando o PC
    $env:WOTM_AIVSAI = "1"; $env:WOTM_MOUSECAM = "0"
    $env:WOTM_LIVES = "1"; $env:WOTM_MONITOR = "$Monitor"; $env:WOTM_CHEATS = "inv"; $env:WOTM_COOPKILL = "$o@$Segundos"
    Remove-Item $log -ErrorAction SilentlyContinue
    $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", "`"$repo\run_live.bat`" -Auto 0" -WindowStyle Hidden -PassThru
    $seen = 0; $t0 = Get-Date; $done = $false
    # ultimo dump esperado: o do ultimo passo (ou o da ultima saida); tempo maximo: carga + passos + folga
    $limit = 120 + $Segundos * ($o.Length + 2)
    while (-not $done -and ((Get-Date) - $t0).TotalSeconds -lt $limit) {
        Start-Sleep -Milliseconds 300
        if (-not (Test-Path $log)) { continue }
        $dumps = @(Select-String -Path $log -Pattern '^\[cooplog\] [^ ].*: g_frame' -ErrorAction SilentlyContinue)
        while ($seen -lt $dumps.Count) {
            $seen++
            Shot (Join-Path $dir ("{0:D2}.png" -f $seen))
        }
        if ($seen -ge $o.Length + 1) { Start-Sleep -Seconds 2; $done = $true }   # comeco da luta + um por saida
    }
    Start-Sleep -Seconds 1
    Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    Copy-Item $log (Join-Path $dir "live.err.log") -ErrorAction SilentlyContinue
    Write-Output "$o : $seen dumps em $dir"
}
