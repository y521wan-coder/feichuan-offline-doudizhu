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
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$workingRoot = $projectRoot
$createdSubstDrive = $null

function Test-SameProject([string]$candidateRoot) {
    $candidateCMake = Join-Path $candidateRoot "CMakeLists.txt"
    $projectCMake = Join-Path $projectRoot "CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $candidateCMake)) { return $false }
    return (Get-FileHash -LiteralPath $candidateCMake -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath $projectCMake -Algorithm SHA256).Hash
}

# CMake 3.31 on this Windows toolchain crashes when the source path contains
# Chinese characters. A subst drive keeps the physical build under the project.
if ($projectRoot -notmatch '^[\x00-\x7F]+$') {
    foreach ($letter in @("R", "Q", "P", "O", "N")) {
        $driveRoot = "${letter}:\"
        if (Test-Path -LiteralPath $driveRoot) {
            if (Test-SameProject $driveRoot) {
                $workingRoot = $driveRoot
                break
            }
            continue
        }
        & subst.exe "${letter}:" $projectRoot
        if ($LASTEXITCODE -ne 0) { continue }
        $createdSubstDrive = "${letter}:"
        $workingRoot = $driveRoot
        break
    }
    if ($workingRoot -eq $projectRoot) {
        throw "No free drive letter is available for the ASCII build path."
    }
}

try {
    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    $bootstrapPath = "C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    $env:PATH = $bootstrapPath
    cmd /c "`"$vcvars`" x64 >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    $cmakePath = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $ninjaPath = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    $env:PATH = "$cmakePath;$ninjaPath;$env:PATH"

    Push-Location $workingRoot
    try {
        if ($Clean) {
            $cleanTarget = Join-Path $workingRoot "build\$Preset"
            $resolvedBuild = [IO.Path]::GetFullPath((Join-Path $workingRoot "build"))
            $resolvedTarget = [IO.Path]::GetFullPath($cleanTarget)
            if (-not $resolvedTarget.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Unsafe build cleanup path: $resolvedTarget"
            }
            Remove-Item -LiteralPath $resolvedTarget -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "Cleaned build directory"
        }

        if ($Configure -or $All) {
            Write-Host "Configuring with preset $Preset..."
            cmake --preset=$Preset --fresh
            if ($LASTEXITCODE -ne 0) { exit 1 }
        }

        if ($Build -or $All) {
            Write-Host "Building..."
            cmake --build --preset=$Preset
            if ($LASTEXITCODE -ne 0) { exit 1 }
        }

        if ($Test -or $All) {
            Write-Host "Testing..."
            ctest --preset=$Preset --output-on-failure
            if ($LASTEXITCODE -ne 0) { exit 1 }
        }
    } finally {
        Pop-Location
    }
} finally {
    if ($createdSubstDrive) {
        & subst.exe $createdSubstDrive /d
    }
}

Write-Host "Done!"
