$ErrorActionPreference = "Stop"

$Blender = "C:\Program Files\Blender Foundation\Blender 5.0\blender.exe"
$ObjPath = Join-Path (Resolve-Path "$PSScriptRoot\..") "simulation\assets\solar\solar_proxy.obj"

if (-not (Test-Path $Blender)) {
  throw "Blender not found at $Blender"
}
if (-not (Test-Path $ObjPath)) {
  throw "Proxy OBJ not found at $ObjPath. Run simulation\tools\preview_solar_urdf.py first."
}

$Script = @"
import bpy
from pathlib import Path

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

obj_path = Path(r"$ObjPath")
bpy.ops.wm.obj_import(filepath=str(obj_path))

for obj in bpy.context.scene.objects:
    obj.select_set(True)
bpy.ops.view3d.camera_to_view_selected()
"@

$TempScript = Join-Path $env:TEMP "open_solar_proxy_blender.py"
Set-Content -Path $TempScript -Value $Script -Encoding UTF8

Start-Process -FilePath $Blender -ArgumentList @("--python", $TempScript)
