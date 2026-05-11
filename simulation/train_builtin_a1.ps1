param(
  [int]$NumEnvs = 1024,
  [int]$MaxIterations = 1000,
  [switch]$Help
)

$ErrorActionPreference = "Stop"

if ($Help) {
  Write-Host "Runs built-in Unitree A1 flat-ground velocity training with RSL-RL."
  Write-Host "Usage: .\simulation\train_builtin_a1.ps1 [-NumEnvs 1024] [-MaxIterations 1000]"
  exit 0
}

. "$PSScriptRoot\isaac_lab_env.ps1"

.\isaaclab.bat -p scripts\reinforcement_learning\rsl_rl\train.py `
  --task Isaac-Velocity-Flat-Unitree-A1-v0 `
  --headless `
  --num_envs $NumEnvs `
  --max_iterations $MaxIterations
