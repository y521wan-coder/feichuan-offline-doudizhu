# build.ps1 - Build helper script
param(
    [string]$Preset = "debug-x64",
    [switch]$Configure,
    [switch]$Build,
    [switch]$Test,
    [switch]$Clean,
    [switch]$All
)

$ErrorActionPreference = "Stop"

# Set up MSVC environment
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
cmd /c "`"$vcvars`" x64 >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}

# Add CMake and Ninja to PATH
$cmakePath = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaPath = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$env:PATH = "$cmakePath;$ninjaPath;$env:PATH"

if ($Clean) {
    Remove-Item -Recurse -Force "build\$Preset" -ErrorAction SilentlyContinue
    Write-Host "Cleaned build directory"
}

if ($Configure -or $All) {
    Write-Host "Configuring with preset $Preset..."
    cmake --preset=$Preset
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if ($Build -or $All) {
    Write-Host "Building..."
    cmake --build --preset=$Preset
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if ($Test -or $All) {
    Write-Host "Testing..."
    Push-Location "build\$Preset"
    ctest --output-on-failure
    Pop-Location
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

Write-Host "Done!"
