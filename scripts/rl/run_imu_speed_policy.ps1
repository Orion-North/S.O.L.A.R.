param(
  [string]$RobotUrl = $env:SOLAR_ROBOT_URL,
  [string]$ApiToken = $env:SOLAR_API_TOKEN,
  [double]$Duration = 15,
  [double]$OutputScale = 0.15,
  [switch]$EnableMotion,
  [switch]$StartTorque,
  [switch]$EstopOnExit,
  [string]$Checkpoint = "",
  [string]$LogJsonl = ""
)

$ErrorActionPreference = "Stop"

if (-not $RobotUrl) {
  $RobotUrl = "http://solar.local"
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Python = Join-Path $RepoRoot "external\IsaacLab\.venv\Scripts\python.exe"
if (-not (Test-Path $Python)) {
  throw "Isaac Lab Python not found at $Python"
}

$RunnerArgs = @(
  "-m", "solar_rl.runner",
  "--robot-url", $RobotUrl,
  "--policy", "imu-speed-rl",
  "--duration", "$Duration",
  "--rl-output-scale", "$OutputScale"
)

if ($ApiToken) { $RunnerArgs += @("--api-token", $ApiToken) }
if ($Checkpoint) { $RunnerArgs += @("--checkpoint", $Checkpoint) }
if ($LogJsonl) { $RunnerArgs += @("--log-jsonl", $LogJsonl) }
if ($EnableMotion) { $RunnerArgs += "--enable-motion" }
if ($StartTorque) { $RunnerArgs += "--start-torque" }
if ($EstopOnExit) { $RunnerArgs += "--estop-on-exit" }

Push-Location $PSScriptRoot
try {
  & $Python @RunnerArgs
} finally {
  Pop-Location
}
