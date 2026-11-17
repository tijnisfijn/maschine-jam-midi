# Installs Maschine Jam MIDI for the current user, with no installer and no admin rights.
# The equivalent of scripts/install.sh on macOS. Use this, or the .exe installer built by
# windows\installer\build-installer.ps1 - they put the files in the same place.
#
#   powershell -ExecutionPolicy Bypass -File windows\install.ps1            # install
#   powershell -ExecutionPolicy Bypass -File windows\install.ps1 -Startup   # and start at logon
#
# Nothing here is signed; see "Signing" in windows\README.md.

param(
  [switch]$Startup,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $env:LOCALAPPDATA 'Programs\Maschine Jam MIDI'

if (-not $SkipBuild) {
  & powershell -ExecutionPolicy Bypass -File (Join-Path $here 'build.ps1')
  if ($LASTEXITCODE -ne 0) { throw "the build failed" }
}

$build = Join-Path $here 'build'
foreach ($exe in 'jam-midi.exe','jam-send.exe','strip-test.exe') {
  if (-not (Test-Path (Join-Path $build $exe))) { throw "missing $exe in $build" }
}

# Stop a copy that is already running, or the copy will fail.
Get-Process -Name jam-midi -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item (Join-Path $build '*.exe') $target -Force
foreach ($doc in @('README.md')) {
  $p = Join-Path $here $doc
  if (Test-Path $p) { Copy-Item $p (Join-Path $target 'README-windows.md') -Force }
}
foreach ($doc in @('MIDI-MAP.md','PROTOCOL.md','LICENSE','NOTICE')) {
  $p = Join-Path (Split-Path -Parent $here) $doc
  if (Test-Path $p) { Copy-Item $p $target -Force }
}

Write-Host "Installed into $target" -ForegroundColor Green

if ($Startup) {
  $link = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup\Maschine Jam MIDI.lnk'
  $s = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
  $s.TargetPath = Join-Path $target 'jam-midi.exe'
  $s.WorkingDirectory = $target
  $s.WindowStyle = 7   # minimised
  $s.Description = 'Maschine Jam MIDI driver'
  $s.Save()
  Write-Host "Starts at logon: $link" -ForegroundColor Green
}

Write-Host ""
Write-Host "Before it works properly:" -ForegroundColor Cyan
if (Test-Path "$env:SystemRoot\System32\teVirtualMIDI64.dll") {
  Write-Host "  [ok]   a virtual MIDI driver is installed"
} else {
  Write-Host "  [todo] install loopMIDI (https://www.tobias-erichsen.de/software/loopmidi.html)" -ForegroundColor Yellow
  Write-Host "         so the driver has a MIDI port your apps can open"
}
$ni = Get-Service -Name NIHardwareService -ErrorAction SilentlyContinue
if ($null -eq $ni) {
  Write-Host "  [ok]   NI's service is not installed"
} elseif ($ni.Status -eq 'Running') {
  Write-Host "  [todo] stop NI's service, or it will fight over the LEDs (needs admin):" -ForegroundColor Yellow
  Write-Host "         Stop-Service -Name NIHardwareService -Force"
  Write-Host "         Set-Service  -Name NIHardwareService -StartupType Manual"
} else {
  Write-Host "  [ok]   NI's service is stopped ($($ni.StartType) start)"
}
Write-Host ""
Write-Host "Run it with: `"$target\jam-midi.exe`""
