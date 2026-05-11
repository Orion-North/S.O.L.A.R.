param(
  [switch]$Help
)

$ErrorActionPreference = "Stop"

if ($Help) {
  Write-Host "Runs one headless PPO iteration on Isaac-Velocity-Flat-Unitree-A1-v0."
  Write-Host "Usage: .\simulation\run_builtin_a1_smoke_test.ps1"
  exit 0
}

. "$PSScriptRoot\isaac_lab_env.ps1"

.\isaaclab.bat -p scripts\reinforcement_learning\rsl_rl\train.py `
  --task Isaac-Velocity-Flat-Unitree-A1-v0 `
  --headless `
  --num_envs 16 `
  --max_iterations 1
