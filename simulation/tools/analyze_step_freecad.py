import json
import os
import sys
from pathlib import Path

import FreeCAD
import Import


def main() -> int:
    args = [arg for arg in sys.argv[1:] if not arg.startswith("-")]
    args = [arg for arg in args if Path(arg).suffix.lower() in {".step", ".stp", ".json"}]
    if len(args) < 2:
        env_input = os.environ.get("SOLAR_STEP_INPUT")
        env_output = os.environ.get("SOLAR_STEP_ANALYSIS_OUTPUT")
        if env_input and env_output:
            args = [env_input, env_output]

    if len(args) < 2:
        print("usage: analyze_step_freecad.py input.step output.json", file=sys.stderr)
        print(f"argv={sys.argv}", file=sys.stderr)
        return 2

    input_path = Path(args[-2]).resolve()
    output_path = Path(args[-1]).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    opened = Import.open(str(input_path))
    doc = opened if hasattr(opened, "Objects") else FreeCAD.ActiveDocument
    if doc is None:
        raise RuntimeError(f"FreeCAD did not create a document for {input_path}")
    rows = []
    for obj in doc.Objects:
        shape = getattr(obj, "Shape", None)
        if shape is None or shape.isNull():
            continue
        box = shape.BoundBox
        try:
            volume = float(shape.Volume)
        except Exception:
            volume = 0.0
        try:
            center = shape.CenterOfMass
            com = [float(center.x), float(center.y), float(center.z)]
        except Exception:
            com = [float((box.XMin + box.XMax) / 2), float((box.YMin + box.YMax) / 2), float((box.ZMin + box.ZMax) / 2)]
        rows.append(
            {
                "name": obj.Name,
                "label": obj.Label,
                "type": obj.TypeId,
                "volume_mm3": volume,
                "bbox_min_mm": [float(box.XMin), float(box.YMin), float(box.ZMin)],
                "bbox_max_mm": [float(box.XMax), float(box.YMax), float(box.ZMax)],
                "bbox_size_mm": [float(box.XLength), float(box.YLength), float(box.ZLength)],
                "center_mm": com,
            }
        )

    rows.sort(key=lambda r: r["volume_mm3"], reverse=True)
    output_path.write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(f"objects={len(rows)}")
    for row in rows[:80]:
        print(
            f"{row['label'][:50]:50s} volume={row['volume_mm3']:.1f} "
            f"size={row['bbox_size_mm']} center={row['center_mm']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
