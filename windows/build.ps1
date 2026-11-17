# Builds jam-midi, jam-send and strip-test into windows/build.
#
# Needs Visual Studio Build Tools 2022 with the "Desktop development with C++" workload
# (which brings MSVC, the Windows SDK, CMake and Ninja). Nothing else.
#
#   powershell -ExecutionPolicy Bypass -File windows\build.ps1

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found. Install Visual Studio Build Tools 2022." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { $vsPath = & $vswhere -latest -products * -property installationPath }
if (-not $vsPath) { throw "No Visual Studio with the C++ tools found." }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# Prefer whatever is on PATH; fall back to the copies that ship with Visual Studio.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (-not (Test-Path $cmake)) { throw "cmake not found. Add the CMake component to Build Tools." }

$ninjaDir = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$cmakeDir = Split-Path $cmake

$build = Join-Path $here 'build'
$cmd = @"
call "$vcvars" >nul
set PATH=$cmakeDir;$ninjaDir;%PATH%
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "$build" || exit /b 1
"@

$tmp = [System.IO.Path]::GetTempFileName() + '.bat'
Set-Content -Path $tmp -Value $cmd -Encoding ASCII
try {
  & cmd.exe /c $tmp
  if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }
} finally {
  Remove-Item $tmp -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "Built into $build" -ForegroundColor Green
Get-ChildItem $build -Filter *.exe | Select-Object Name, Length | Format-Table -AutoSize
