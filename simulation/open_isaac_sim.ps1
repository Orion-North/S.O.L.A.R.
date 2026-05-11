$ErrorActionPreference = "Stop"

. "$PSScriptRoot\isaac_lab_env.ps1"

$KitApp = Join-Path $IsaacLabRoot ".venv\Lib\site-packages\isaacsim\kit\apps\omni.app.hydra.kit"

.\.venv\Scripts\isaacsim.exe $KitApp --reset-user
