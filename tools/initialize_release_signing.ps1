param(
    [switch]$ReplaceUnpublished,
    [switch]$PromptPassword
)
$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "initialize_release_signing.py"
$argsList = @($script)
if ($ReplaceUnpublished) { $argsList += "--replace-unpublished" }
if ($PromptPassword) { $argsList += "--prompt-password" }
& python @argsList
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
