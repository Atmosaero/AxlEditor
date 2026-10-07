param(
    [switch]$Run,
    [string]$QtRoot = (Join-Path $PSScriptRoot '.tools\Qt\6.12.0\msvc2022_64')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt 6.12 SDK was not found at $QtRoot. Install it first or pass -QtRoot."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'Install Visual Studio with the Desktop development with C++ workload.'
}

$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$env:Path = "$(Split-Path -Parent $vswhere);$env:Path"
$env:VSINSTALLDIR = $vsPath.TrimEnd('\') + '\'
$env:VCINSTALLDIR = Join-Path $env:VSINSTALLDIR 'VC\'
$env:VSLANG = '1033'
# Keep MSVC include diagnostics and CMake/Ninja dependency parsing in UTF-8.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$devEnvironment = & $env:ComSpec /d /c "chcp 65001 >nul && call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize the MSVC environment.' }
foreach ($line in $devEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

$buildDir = Join-Path $PSScriptRoot 'build'
& cmake -S $PSScriptRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

if ($Run) {
    Start-Process -FilePath (Join-Path $buildDir 'AxlEditor.exe') -WorkingDirectory $buildDir
}
