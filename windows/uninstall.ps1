# Removes what install.ps1 put there. (The .exe installer has its own entry in
# Settings > Apps, so use that if you installed with it.)
#
#   powershell -ExecutionPolicy Bypass -File windows\uninstall.ps1

$ErrorActionPreference = 'Stop'
$target = Join-Path $env:LOCALAPPDATA 'Programs\Maschine Jam MIDI'
$link = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup\Maschine Jam MIDI.lnk'

Get-Process -Name jam-midi -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

if (Test-Path $link) { Remove-Item $link -Force; Write-Host "Removed $link" }
if (Test-Path $target) { Remove-Item $target -Recurse -Force; Write-Host "Removed $target" }
else { Write-Host "Nothing installed at $target" }

Write-Host ""
Write-Host "Left alone on purpose: loopMIDI, and NI's NIHardwareService." -ForegroundColor Cyan
Write-Host "To put NI's service back the way it was (needs admin):"
Write-Host "  Set-Service -Name NIHardwareService -StartupType Automatic"
Write-Host "  Start-Service -Name NIHardwareService"
