from __future__ import annotations

import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ANALYSIS_PATH = ROOT / "simulation" / "source_model" / "step_analysis.json"
ASSET_DIR = ROOT / "simulation" / "assets" / "solar"
URDF_PATH = ASSET_DIR / "solar.urdf"
DIMENSIONS_PATH = ASSET_DIR / "solar_proxy_dimensions.json"


def box_inertia(mass: float, size: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = size
    return (
        mass * (y * y + z * z) / 12.0,
        mass * (x * x + z * z) / 12.0,
        mass * (x * x + y * y) / 12.0,
    )


def add_inertial(link: ET.Element, mass: float, size: tuple[float, float, float], origin=(0.0, 0.0, 0.0)) -> None:
    inertial = ET.SubElement(link, "inertial")
    ET.SubElement(inertial, "origin", xyz=fmt_xyz(origin), rpy="0 0 0")
    ET.SubElement(inertial, "mass", value=f"{mass:.6g}")
    ixx, iyy, izz = box_inertia(mass, size)
    ET.SubElement(
        inertial,
        "inertia",
        ixx=f"{ixx:.8g}",
        ixy="0",
        ixz="0",
        iyy=f"{iyy:.8g}",
        iyz="0",
        izz=f"{izz:.8g}",
    )


def add_box_visual_collision(
    link: ET.Element,
    size: tuple[float, float, float],
    origin=(0.0, 0.0, 0.0),
    material: str | None = None,
) -> None:
    for tag in ("visual", "collision"):
        elem = ET.SubElement(link, tag)
        ET.SubElement(elem, "origin", xyz=fmt_xyz(origin), rpy="0 0 0")
        geom = ET.SubElement(elem, "geometry")
        ET.SubElement(geom, "box", size=fmt_xyz(size))
        if tag == "visual" and material is not None:
            ET.SubElement(elem, "material", name=material)


def add_joint(
    robot: ET.Element,
    name: str,
    joint_type: str,
    parent: str,
    child: str,
    origin=(0.0, 0.0, 0.0),
    axis=(0.0, 1.0, 0.0),
    lower: float | None = None,
    upper: float | None = None,
    effort: float = 0.25,
    velocity: float = 8.0,
) -> None:
    joint = ET.SubElement(robot, "joint", name=name, type=joint_type)
    ET.SubElement(joint, "parent", link=parent)
    ET.SubElement(joint, "child", link=child)
    ET.SubElement(joint, "origin", xyz=fmt_xyz(origin), rpy="0 0 0")
    if joint_type != "fixed":
        ET.SubElement(joint, "axis", xyz=fmt_xyz(axis))
        ET.SubElement(
            joint,
            "limit",
            lower=f"{lower if lower is not None else -math.pi:.6g}",
            upper=f"{upper if upper is not None else math.pi:.6g}",
            effort=f"{effort:.6g}",
            velocity=f"{velocity:.6g}",
        )
        ET.SubElement(joint, "dynamics", damping="0.02", friction="0.01")


def fmt_xyz(values) -> str:
    return " ".join(f"{float(v):.6g}" for v in values)


def indent(elem: ET.Element, level: int = 0) -> None:
    i = "\n" + level * "  "
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        for child in elem:
            indent(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = i
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = i


def find_row(rows: list[dict], label: str) -> dict:
    for row in rows:
        if row["label"] == label:
            return row
    raise KeyError(label)


def main() -> int:
    rows = json.loads(ANALYSIS_PATH.read_text(encoding="utf-8"))
    assembly = find_row(rows, "assembly v8")
    base = find_row(rows, "base_link v7")

    # STEP axes are CAD-local: X/Y horizontal and Z up. URDF uses X forward, Y left, Z up.
    # This proxy assumes CAD +Y is robot forward and CAD -X is robot left.
    base_size_m = (
        base["bbox_size_mm"][1] / 1000.0,
        base["bbox_size_mm"][0] / 1000.0,
        base["bbox_size_mm"][2] / 1000.0,
    )
    assembly_size_m = (
        assembly["bbox_size_mm"][1] / 1000.0,
        assembly["bbox_size_mm"][0] / 1000.0,
        assembly["bbox_size_mm"][2] / 1000.0,
    )

    dimensions = {
        "source_step": str(ROOT / "simulation" / "source_model" / "assembly.step"),
        "coordinate_assumption": "URDF x = CAD +Y forward, URDF y = CAD -X left, URDF z = CAD +Z up",
        "assembly_size_m": assembly_size_m,
        "base_box_size_m": base_size_m,
        "hip_station_m": {
            "x_front_back": 0.0573,
            "y_left_right": 0.0482,
            "z": -0.025,
        },
        "link_lengths_m": {
            "hip_lateral": 0.032,
            "thigh": 0.058,
            "calf": 0.064,
        },
        "masses_kg": {
            "base_link": 0.55,
            "hip_link_each": 0.035,
            "thigh_link_each": 0.04,
            "calf_link_each": 0.03,
            "foot_each": 0.012,
            "total_proxy": 0.55 + 4 * (0.035 + 0.04 + 0.03 + 0.012),
        },
        "actuator_limits": {
            "effort_nm": 0.25,
            "velocity_rad_s": 8.0,
            "hip_range_rad": [-0.785398, 0.785398],
            "thigh_range_rad": [-0.523599, 1.0472],
            "calf_range_rad": [-1.5708, 0.523599],
        },
    }

    robot = ET.Element("robot", name="solar_quadruped_proxy")
    for name, rgba in {
        "base_dark": "0.02 0.02 0.025 1",
        "leg_black": "0.01 0.01 0.012 1",
        "foot_tpu": "0.05 0.05 0.05 1",
    }.items():
        mat = ET.SubElement(robot, "material", name=name)
        ET.SubElement(mat, "color", rgba=rgba)

    base_link = ET.SubElement(robot, "link", name="base_link")
    add_inertial(base_link, dimensions["masses_kg"]["base_link"], base_size_m)
    add_box_visual_collision(base_link, base_size_m, material="base_dark")

    hip_size = (0.034, 0.018, 0.026)
    thigh_size = (0.018, 0.014, dimensions["link_lengths_m"]["thigh"])
    calf_size = (0.016, 0.014, dimensions["link_lengths_m"]["calf"])
    foot_size = (0.028, 0.020, 0.012)

    legs = {
        "FL": (1.0, 1.0),
        "FR": (1.0, -1.0),
        "RL": (-1.0, 1.0),
        "RR": (-1.0, -1.0),
    }
    hip_x = dimensions["hip_station_m"]["x_front_back"]
    hip_y = dimensions["hip_station_m"]["y_left_right"]
    hip_z = dimensions["hip_station_m"]["z"]
    hip_lateral = dimensions["link_lengths_m"]["hip_lateral"]
    thigh_len = dimensions["link_lengths_m"]["thigh"]
    calf_len = dimensions["link_lengths_m"]["calf"]

    for leg, (sx, sy) in legs.items():
        hip_link = f"{leg}_hip_link"
        thigh_link = f"{leg}_thigh_link"
        calf_link = f"{leg}_calf_link"
        foot_link = f"{leg}_foot"

        link = ET.SubElement(robot, "link", name=hip_link)
        add_inertial(link, dimensions["masses_kg"]["hip_link_each"], hip_size, origin=(0.0, sy * hip_lateral / 2.0, 0.0))
        add_box_visual_collision(link, hip_size, origin=(0.0, sy * hip_lateral / 2.0, 0.0), material="leg_black")

        link = ET.SubElement(robot, "link", name=thigh_link)
        add_inertial(link, dimensions["masses_kg"]["thigh_link_each"], thigh_size, origin=(0.0, 0.0, -thigh_len / 2.0))
        add_box_visual_collision(link, thigh_size, origin=(0.0, 0.0, -thigh_len / 2.0), material="leg_black")

        link = ET.SubElement(robot, "link", name=calf_link)
        add_inertial(link, dimensions["masses_kg"]["calf_link_each"], calf_size, origin=(0.0, 0.0, -calf_len / 2.0))
        add_box_visual_collision(link, calf_size, origin=(0.0, 0.0, -calf_len / 2.0), material="leg_black")

        link = ET.SubElement(robot, "link", name=foot_link)
        add_inertial(link, dimensions["masses_kg"]["foot_each"], foot_size)
        add_box_visual_collision(link, foot_size, material="foot_tpu")

        add_joint(
            robot,
            f"{leg}_hip_joint",
            "revolute",
            "base_link",
            hip_link,
            origin=(sx * hip_x, sy * hip_y, hip_z),
            axis=(0.0, 0.0, 1.0),
            lower=-0.785398,
            upper=0.785398,
        )
        add_joint(
            robot,
            f"{leg}_thigh_joint",
            "revolute",
            hip_link,
            thigh_link,
            origin=(0.0, sy * hip_lateral, 0.0),
            axis=(0.0, 1.0, 0.0),
            lower=-0.523599,
            upper=1.0472,
        )
        add_joint(
            robot,
            f"{leg}_calf_joint",
            "revolute",
            thigh_link,
            calf_link,
            origin=(0.0, 0.0, -thigh_len),
            axis=(0.0, 1.0, 0.0),
            lower=-1.5708,
            upper=0.523599,
        )
        add_joint(
            robot,
            f"{leg}_foot_fixed",
            "fixed",
            calf_link,
            foot_link,
            origin=(0.0, 0.0, -calf_len - foot_size[2] / 2.0),
        )

    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    DIMENSIONS_PATH.write_text(json.dumps(dimensions, indent=2), encoding="utf-8")
    indent(robot)
    ET.ElementTree(robot).write(URDF_PATH, encoding="utf-8", xml_declaration=True)
    print(f"Wrote {URDF_PATH}")
    print(f"Wrote {DIMENSIONS_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
