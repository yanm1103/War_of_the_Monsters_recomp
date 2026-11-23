param([string]$Out, [string]$Text)
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
Write-Host ("voices: " + (($s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name + "/" + $_.VoiceInfo.Culture }) -join ", "))
$s.SelectVoice('Microsoft David Desktop')
$s.Rate = -2
$s.Volume = 100
$s.SetOutputToWaveFile($Out)
$s.Speak($Text)
$s.Dispose()
Write-Host "ok $Out"
