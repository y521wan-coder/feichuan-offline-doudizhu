param(
    [switch]$Deploy,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$gameName = -join @([char]0x98DE, [char]0x8239, [char]0x6597, [char]0x5730,
    [char]0x4E3B)
$updaterName = $gameName + (-join @([char]0x66F4, [char]0x65B0, [char]0x5668))
$aiServiceName = $gameName + 'AI' + (-join @([char]0x670D, [char]0x52A1))
$gameFileName = $gameName + '.exe'
$updaterFileName = $updaterName + '.exe'
$aiServiceFileName = $aiServiceName + '.exe'
$releaseExe = Join-Path $root (Join-Path 'build\release-x64' $gameFileName)
$updaterExe = Join-Path $root (Join-Path 'build\release-x64' $updaterFileName)
$aiServiceExe = Join-Path $root (Join-Path 'build\release-x64' $aiServiceFileName)
$nvdaControllerDll = Join-Path $root 'build\release-x64\nvdaControllerClient.dll'
$nvdaControllerLicense = Join-Path $root 'build\release-x64\licenses\NVDA-Controller-Client-LGPL-2.1.txt'
$portable = Join-Path $root 'artifacts\portable-current'
$versionFile = Join-Path $root 'version.txt'
$dist = Join-Path $root 'releases'
$packageStage = Join-Path $root 'build\installer-stage'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
$vcRedist = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Redist\MSVC\v145\vc_redist.x64.exe'
$windeployqt = Join-Path $root 'Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'

$installerScripts = @(Get-ChildItem -LiteralPath (Join-Path $root 'installer') -Filter '*.iss' -File)
if ($installerScripts.Count -ne 1) {
    throw "Expected exactly one Inno Setup script, found $($installerScripts.Count)."
}
$installerScript = $installerScripts[0].FullName

if ($Package) { $Deploy = $true }
if ($Package -and -not (Test-Path -LiteralPath $iscc)) { throw "Missing Inno Setup compiler: $iscc" }
if ($Package -and -not (Test-Path -LiteralPath $versionFile)) { throw "Missing version file: $versionFile" }
if ($Package -and -not (Test-Path -LiteralPath $vcRedist)) { throw "Missing VC++ runtime: $vcRedist" }
if ($Deploy -and -not (Test-Path -LiteralPath $windeployqt)) { throw "Missing windeployqt: $windeployqt" }

& $buildScript -Preset release-x64 -Configure -Build -Test
if ($LASTEXITCODE -ne 0) { throw "Release build or tests failed with exit code $LASTEXITCODE" }

if ($Deploy) {
    $running = Get-Process -Name $gameName -ErrorAction SilentlyContinue
    if ($running) { throw 'Close the game before generating the portable package.' }
    if (-not (Test-Path -LiteralPath $releaseExe)) { throw "Missing release executable: $releaseExe" }
    if (-not (Test-Path -LiteralPath $updaterExe)) { throw "Missing updater executable: $updaterExe" }
    if (-not (Test-Path -LiteralPath $aiServiceExe)) { throw "Missing AI service executable: $aiServiceExe" }
    if (-not (Test-Path -LiteralPath $nvdaControllerDll)) { throw "Missing NVDA Controller Client: $nvdaControllerDll" }
    if (-not (Test-Path -LiteralPath $nvdaControllerLicense)) { throw "Missing NVDA Controller Client license: $nvdaControllerLicense" }

    $resolvedArtifacts = [IO.Path]::GetFullPath((Join-Path $root 'artifacts'))
    $resolvedPortable = [IO.Path]::GetFullPath($portable)
    if (-not $resolvedPortable.StartsWith($resolvedArtifacts + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe portable stage path: $resolvedPortable"
    }
    if (Test-Path -LiteralPath $resolvedPortable) {
        Remove-Item -LiteralPath $resolvedPortable -Recurse -Force
    }
    New-Item -ItemType Directory -Path $resolvedPortable -Force | Out-Null
    Copy-Item -LiteralPath $releaseExe -Destination $resolvedPortable -Force
    Copy-Item -LiteralPath $updaterExe -Destination $resolvedPortable -Force
    Copy-Item -LiteralPath $aiServiceExe -Destination $resolvedPortable -Force
    Copy-Item -LiteralPath $nvdaControllerDll -Destination $resolvedPortable -Force
    New-Item -ItemType Directory -Path (Join-Path $resolvedPortable 'licenses') -Force | Out-Null
    Copy-Item -LiteralPath $nvdaControllerLicense -Destination (Join-Path $resolvedPortable 'licenses') -Force
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $resolvedPortable -Force
    Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $resolvedPortable -Recurse -Force
    New-Item -ItemType Directory -Path (Join-Path $resolvedPortable 'resources') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'assets\sounds') -Destination (Join-Path $resolvedPortable 'resources') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $root 'assets\docs') -Destination (Join-Path $resolvedPortable 'resources') -Recurse -Force

    & $windeployqt --release --dir $resolvedPortable (Join-Path $resolvedPortable $gameFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the game with exit code $LASTEXITCODE" }
    & $windeployqt --release --dir $resolvedPortable (Join-Path $resolvedPortable $updaterFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the updater with exit code $LASTEXITCODE" }
    & $windeployqt --release --dir $resolvedPortable (Join-Path $resolvedPortable $aiServiceFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the AI service with exit code $LASTEXITCODE" }
}

if ($Package) {
    $version = (Get-Content -LiteralPath $versionFile -Encoding UTF8 | Select-Object -First 1).Trim()
    if ($version -notmatch '^\d+\.\d+$') { throw "Installer version must use major.minor format: $version" }

    $resolvedBuild = [IO.Path]::GetFullPath((Join-Path $root 'build'))
    $resolvedStage = [IO.Path]::GetFullPath($packageStage)
    if (-not $resolvedStage.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe installer stage path: $resolvedStage"
    }
    if (Test-Path -LiteralPath $resolvedStage) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $resolvedStage -Force | Out-Null
    Copy-Item -LiteralPath $releaseExe -Destination $resolvedStage -Force
    Copy-Item -LiteralPath $updaterExe -Destination $resolvedStage -Force
    Copy-Item -LiteralPath $aiServiceExe -Destination $resolvedStage -Force
    Copy-Item -LiteralPath $nvdaControllerDll -Destination $resolvedStage -Force
    New-Item -ItemType Directory -Path (Join-Path $resolvedStage 'licenses') -Force | Out-Null
    Copy-Item -LiteralPath $nvdaControllerLicense -Destination (Join-Path $resolvedStage 'licenses') -Force
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $resolvedStage -Force
    New-Item -ItemType Directory -Path (Join-Path $resolvedStage 'docs') -Force | Out-Null
    Copy-Item -Path (Join-Path $root 'assets\docs\*.txt') -Destination (Join-Path $resolvedStage 'docs') -Force
    New-Item -ItemType Directory -Path (Join-Path $resolvedStage 'resources') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'assets\sounds') -Destination (Join-Path $resolvedStage 'resources') -Recurse -Force
    Copy-Item -LiteralPath $vcRedist -Destination $resolvedStage -Force

    & $windeployqt --release --no-translations --dir $resolvedStage (Join-Path $resolvedStage $gameFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the game with exit code $LASTEXITCODE" }
    & $windeployqt --release --no-translations --dir $resolvedStage (Join-Path $resolvedStage $updaterFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the updater with exit code $LASTEXITCODE" }
    & $windeployqt --release --no-translations --dir $resolvedStage (Join-Path $resolvedStage $aiServiceFileName)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for the AI service with exit code $LASTEXITCODE" }

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
