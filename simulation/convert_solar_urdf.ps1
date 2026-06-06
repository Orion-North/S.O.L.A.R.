$ErrorActionPreference = "Stop"

. "$PSScriptRoot\isaac_lab_env.ps1"

$UrdfPath = Join-Path $RepoRoot "simulation\assets\solar\solar.urdf"
$UsdPath = Join-Path $RepoRoot "simulation\assets\solar\solar.usd"

.\isaaclab.bat -p scripts\tools\convert_urdf.py `
  $UrdfPath `
  $UsdPath `
  --headless `
  --joint-target-type position `
  --joint-stiffness 25 `
  --joint-damping 0.5
