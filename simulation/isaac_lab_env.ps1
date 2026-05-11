$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$IsaacLabRoot = Join-Path $RepoRoot "external\IsaacLab"
$VenvScripts = Join-Path $IsaacLabRoot ".venv\Scripts"

if (-not (Test-Path $IsaacLabRoot)) {
  throw "Isaac Lab was not found at $IsaacLabRoot"
}

if (-not (Test-Path (Join-Path $VenvScripts "python.exe"))) {
  throw "Isaac Lab venv was not found at $VenvScripts"
}

$env:OMNI_KIT_ACCEPT_EULA = "YES"
$env:PYTHONNOUSERSITE = "1"
$env:Path = "$VenvScripts;$env:Path"

Set-Location $IsaacLabRoot
