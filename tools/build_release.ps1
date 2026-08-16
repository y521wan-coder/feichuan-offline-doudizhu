param(
    [switch]$Deploy,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$releaseExe = Join-Path $root 'build\release-x64\FourPlayerDoudizhu.exe'
$portable = Join-Path $root 'build\portable-current'
$versionFile = Join-Path $root 'version.txt'
$installerScripts = @(Get-ChildItem -LiteralPath (Join-Path $root 'installer') -Filter '*.iss' -File)
if ($installerScripts.Count -ne 1) {
    throw "Expected exactly one Inno Setup script, found $($installerScripts.Count)."
}
$installerScript = $installerScripts[0].FullName
$iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
$dist = Join-Path $root 'dist'
$packageStage = Join-Path $root 'build\installer-stage'
$vcRedist = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC\v145\vc_redist.x64.exe'

if (-not (Test-Path -LiteralPath $vcvars)) { throw "Missing vcvars64.bat: $vcvars" }
if (-not (Test-Path -LiteralPath $cmake)) { throw "Missing cmake.exe: $cmake" }
if ($Package -and -not (Test-Path -LiteralPath $iscc)) { throw "Missing Inno Setup compiler: $iscc" }
if ($Package -and -not (Test-Path -LiteralPath $versionFile)) { throw "Missing version file: $versionFile" }
if ($Package -and -not (Test-Path -LiteralPath $vcRedist)) { throw "Missing VC++ runtime: $vcRedist" }
if ($Package) { $Deploy = $true }

$batchPath = Join-Path $env:TEMP ("fpdz_release_{0}.bat" -f $PID)
$batch = @"
@echo off
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
set "VSLANG=1033"
cd /d "$root"
"$cmake" --preset release-x64 --fresh
if errorlevel 1 exit /b %errorlevel%
"$cmake" --build --preset release-x64 --clean-first
if errorlevel 1 exit /b %errorlevel%
"$ctest" --preset release-x64 --output-on-failure
exit /b %errorlevel%
"@
[System.IO.File]::WriteAllText($batchPath, $batch, [System.Text.Encoding]::ASCII)

Push-Location $root
try {
    & cmd.exe /d /c $batchPath
    if ($LASTEXITCODE -ne 0) { throw "Release build or tests failed with exit code $LASTEXITCODE" }

    if ($Deploy) {
        $running = Get-Process -Name FourPlayerDoudizhu -ErrorAction SilentlyContinue
        if ($running) { throw 'Close FourPlayerDoudizhu before deploying the portable build.' }
        if (-not (Test-Path -LiteralPath $releaseExe)) { throw "Missing release executable: $releaseExe" }

        $resolvedBuild = [IO.Path]::GetFullPath((Join-Path $root 'build'))
        $resolvedPortable = [IO.Path]::GetFullPath($portable)
        if (-not $resolvedPortable.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe portable stage path: $resolvedPortable"
        }
        if (Test-Path -LiteralPath $resolvedPortable) {
            Remove-Item -LiteralPath $resolvedPortable -Recurse -Force
        }
        New-Item -ItemType Directory -Path $resolvedPortable -Force | Out-Null
        Copy-Item -LiteralPath $releaseExe -Destination $portable -Force
        Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $portable -Force
        Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $portable -Recurse -Force
        Copy-Item -LiteralPath (Join-Path $root 'resources') -Destination $portable -Recurse -Force
        $windeployqt = Join-Path $root 'Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'
        & $windeployqt --release --dir $portable (Join-Path $portable 'FourPlayerDoudizhu.exe')
        if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
    }

    if ($Package) {
        $version = (Get-Content -LiteralPath $versionFile -Encoding UTF8 | Select-Object -First 1).Trim()
        if ($version -notmatch '^\d+\.\d+$') { throw "Installer version must use major.minor format: $version" }
        $resolvedBuild = [IO.Path]::GetFullPath((Join-Path $root 'build'))
        $resolvedStage = [IO.Path]::GetFullPath($packageStage)
        if (-not $resolvedStage.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe installer stage path: $resolvedStage"
        }
        if (Test-Path -LiteralPath $resolvedStage) {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
        New-Item -ItemType Directory -Path $resolvedStage -Force | Out-Null
        Copy-Item -LiteralPath $releaseExe -Destination $resolvedStage -Force
        Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $resolvedStage -Force
        New-Item -ItemType Directory -Path (Join-Path $resolvedStage 'docs') -Force | Out-Null
        Copy-Item -Path (Join-Path $root 'resources\docs\*.txt') -Destination (Join-Path $resolvedStage 'docs') -Force
        New-Item -ItemType Directory -Path (Join-Path $resolvedStage 'resources') -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $root 'resources\sounds') -Destination (Join-Path $resolvedStage 'resources') -Recurse -Force
        Copy-Item -LiteralPath $vcRedist -Destination $resolvedStage -Force
        $windeployqt = Join-Path $root 'Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'
        & $windeployqt --release --no-translations --dir $resolvedStage (Join-Path $resolvedStage 'FourPlayerDoudizhu.exe')
        if ($LASTEXITCODE -ne 0) { throw "windeployqt for installer failed with exit code $LASTEXITCODE" }
        New-Item -ItemType Directory -Path $dist -Force | Out-Null
        & $iscc "/DAppVersion=$version" "/DSourceDir=$resolvedStage" "/DOutputDir=$dist" $installerScript
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }
        $installerMatches = @(Get-ChildItem -LiteralPath $dist -Filter ("*-Setup-{0}.exe" -f $version) -File)
        if ($installerMatches.Count -ne 1) {
            throw "Expected exactly one installer for version $version, found $($installerMatches.Count)."
        }
        $installer = $installerMatches[0].FullName
        $hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
        Write-Host ("Installer: {0}" -f $installer)
        Write-Host ("SHA-256: {0}" -f $hash.Hash)
    }
} finally {
    Remove-Item -LiteralPath $batchPath -Force -ErrorAction SilentlyContinue
    Pop-Location
}
