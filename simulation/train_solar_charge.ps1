param(
  [int]$NumEnvs = 32768,
  [int]$MaxIterations = 2500
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\isaac_lab_env.ps1"

$SolarSource = Join-Path $RepoRoot "simulation\source\solar_lab"
if ($env:PYTHONPATH) {
  $env:PYTHONPATH = "$SolarSource;$env:PYTHONPATH"
} else {
  $env:PYTHONPATH = $SolarSource
}

.\isaaclab.bat -p "$RepoRoot\simulation\tools\run_rsl_rl_train_with_solar.py" `
  --task Solar-Charge-Flat-NoIMU-v0 `
  --num_envs $NumEnvs `
  --max_iterations $MaxIterations `
  --headless
