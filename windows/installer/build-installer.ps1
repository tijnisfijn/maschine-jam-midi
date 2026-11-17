# Builds "Maschine Jam MIDI Setup <version>.exe" into windows\build\installer.
#
# Needs Inno Setup 6 (free, https://jrsoftware.org/isinfo.php). If it is not installed:
#     winget install --id JRSoftware.InnoSetup --source winget
# or pass -InstallInno to have this script do that.
#
#   powershell -ExecutionPolicy Bypass -File windows\installer\build-installer.ps1
#
# The result is NOT signed; see "Signing" in windows\README.md.

param(
  [switch]$InstallInno,
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$windows = Split-Path -Parent $here

if (-not $SkipBuild) {
  Write-Host "Building the driver first..." -ForegroundColor Cyan
  & powershell -ExecutionPolicy Bypass -File (Join-Path $windows 'build.ps1')
  if ($LASTEXITCODE -ne 0) { throw "the driver build failed" }
}

foreach ($exe in 'jam-midi.exe','jam-send.exe','strip-test.exe') {
  $p = Join-Path $windows "build\$exe"
  if (-not (Test-Path $p)) { throw "missing $p - build the driver first" }
}

function Find-ISCC {
  $c = (Get-Command iscc -ErrorAction SilentlyContinue).Source
  if ($c) { return $c }
  # winget installs Inno Setup per-user, so look there too, not only in Program Files.
  foreach ($root in @("${env:ProgramFiles(x86)}", "$env:ProgramFiles", "$env:LOCALAPPDATA\Programs")) {
    foreach ($ver in @('Inno Setup 6', 'Inno Setup 7')) {
      $p = Join-Path $root "$ver\ISCC.exe"
      if (Test-Path $p) { return $p }
    }
  }
  return $null
}

$iscc = Find-ISCC
if (-not $iscc -and $InstallInno) {
  Write-Host "Installing Inno Setup via winget..." -ForegroundColor Cyan
  & winget install --id JRSoftware.InnoSetup --source winget --accept-package-agreements --accept-source-agreements
  $iscc = Find-ISCC
}
if (-not $iscc) {
  throw "Inno Setup not found. Install it with:`n  winget install --id JRSoftware.InnoSetup --source winget`nor re-run this script with -InstallInno."
}

$out = Join-Path $windows 'build\installer'
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $iscc (Join-Path $here 'jam-midi.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }

Write-Host ""
Get-ChildItem $out -Filter *.exe | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
Write-Host "This installer is not signed. Windows SmartScreen will warn about it." -ForegroundColor Yellow
