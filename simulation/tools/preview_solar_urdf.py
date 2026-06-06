from __future__ import annotations

import math
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


ROOT = Path(__file__).resolve().parents[2]
URDF_PATH = ROOT / "simulation" / "assets" / "solar" / "solar.urdf"
OUT_PATH = ROOT / "simulation" / "assets" / "solar" / "solar_proxy_preview.png"
OBJ_PATH = ROOT / "simulation" / "assets" / "solar" / "solar_proxy.obj"


@dataclass
class Box:
    link: str
    size: np.ndarray
    transform: np.ndarray


@dataclass
class Joint:
    name: str
    parent: str
    child: str
    origin: np.ndarray
    rpy: np.ndarray


def parse_xyz(value: str | None) -> np.ndarray:
    if not value:
        return np.zeros(3)
    return np.array([float(v) for v in value.split()], dtype=float)


def transform_from_xyz_rpy(xyz: np.ndarray, rpy: np.ndarray) -> np.ndarray:
    cr, sr = math.cos(rpy[0]), math.sin(rpy[0])
    cp, sp = math.cos(rpy[1]), math.sin(rpy[1])
    cy, sy = math.cos(rpy[2]), math.sin(rpy[2])
    rx = np.array([[1, 0, 0], [0, cr, -sr], [0, sr, cr]])
    ry = np.array([[cp, 0, sp], [0, 1, 0], [-sp, 0, cp]])
    rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    mat = np.eye(4)
    mat[:3, :3] = rz @ ry @ rx
    mat[:3, 3] = xyz
    return mat


def box_vertices(size: np.ndarray) -> np.ndarray:
    x, y, z = size / 2.0
    return np.array(
        [
            [-x, -y, -z],
            [x, -y, -z],
            [x, y, -z],
            [-x, y, -z],
            [-x, -y, z],
            [x, -y, z],
            [x, y, z],
            [-x, y, z],
        ]
    )


def transformed_vertices(box: Box) -> np.ndarray:
    verts = box_vertices(box.size)
    homog = np.c_[verts, np.ones(len(verts))]
    return (box.transform @ homog.T).T[:, :3]


def draw_box(ax, box: Box, color: str) -> None:
    verts = transformed_vertices(box)
    faces = [
        [verts[i] for i in [0, 1, 2, 3]],
        [verts[i] for i in [4, 5, 6, 7]],
        [verts[i] for i in [0, 1, 5, 4]],
        [verts[i] for i in [2, 3, 7, 6]],
        [verts[i] for i in [1, 2, 6, 5]],
        [verts[i] for i in [0, 3, 7, 4]],
    ]
    poly = Poly3DCollection(faces, alpha=0.58, facecolor=color, edgecolor="#d8e3ee", linewidth=0.45)
    ax.add_collection3d(poly)


def write_obj(boxes: list[Box]) -> None:
    face_indices = [
        [1, 2, 3, 4],
        [5, 8, 7, 6],
        [1, 5, 6, 2],
        [3, 7, 8, 4],
        [2, 6, 7, 3],
        [1, 4, 8, 5],
    ]
    lines = ["# S.O.L.A.R. simplified URDF proxy"]
    vertex_offset = 0
    for box in boxes:
        lines.append(f"o {box.link}")
        verts = transformed_vertices(box)
        for vert in verts:
            lines.append(f"v {vert[0]:.8f} {vert[1]:.8f} {vert[2]:.8f}")
        for face in face_indices:
            shifted = [str(idx + vertex_offset) for idx in face]
            lines.append("f " + " ".join(shifted))
        vertex_offset += 8
    OBJ_PATH.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    tree = ET.parse(URDF_PATH)
    root = tree.getroot()

    link_visuals: dict[str, list[tuple[np.ndarray, np.ndarray, np.ndarray]]] = {}
    for link in root.findall("link"):
        name = link.attrib["name"]
        for visual in link.findall("visual"):
            origin = visual.find("origin")
            xyz = parse_xyz(origin.attrib.get("xyz") if origin is not None else None)
            rpy = parse_xyz(origin.attrib.get("rpy") if origin is not None else None)
            box = visual.find("geometry/box")
            if box is None:
                continue
            size = parse_xyz(box.attrib["size"])
            link_visuals.setdefault(name, []).append((size, xyz, rpy))

    joints = []
    children = set()
    by_parent: dict[str, list[Joint]] = {}
    for joint in root.findall("joint"):
        origin = joint.find("origin")
        parent = joint.find("parent").attrib["link"]
        child = joint.find("child").attrib["link"]
        item = Joint(
            name=joint.attrib["name"],
            parent=parent,
            child=child,
            origin=parse_xyz(origin.attrib.get("xyz") if origin is not None else None),
            rpy=parse_xyz(origin.attrib.get("rpy") if origin is not None else None),
        )
        joints.append(item)
        children.add(child)
        by_parent.setdefault(parent, []).append(item)

    root_link = next(link.attrib["name"] for link in root.findall("link") if link.attrib["name"] not in children)
    link_tf = {root_link: np.eye(4)}
    stack = [root_link]
    while stack:
        parent = stack.pop()
        for joint in by_parent.get(parent, []):
            link_tf[joint.child] = link_tf[parent] @ transform_from_xyz_rpy(joint.origin, joint.rpy)
            stack.append(joint.child)

    boxes: list[Box] = []
    for link, visuals in link_visuals.items():
        for size, xyz, rpy in visuals:
            boxes.append(Box(link=link, size=size, transform=link_tf[link] @ transform_from_xyz_rpy(xyz, rpy)))

    all_verts = np.vstack([transformed_vertices(box) for box in boxes])
    mins = all_verts.min(axis=0)
    maxs = all_verts.max(axis=0)
    center = (mins + maxs) / 2
    radius = float(np.max(maxs - mins) * 0.65)

    fig = plt.figure(figsize=(14, 10), dpi=150)
    views = [
        ("isometric", 26, -45),
        ("top: x forward, y left", 90, -90),
        ("front", 0, -90),
        ("side", 0, 0),
    ]
    for idx, (title, elev, azim) in enumerate(views, start=1):
        ax = fig.add_subplot(2, 2, idx, projection="3d")
        for box in boxes:
            if box.link == "base_link":
                color = "#1f2933"
            elif box.link.endswith("_foot"):
                color = "#2f855a"
            else:
                color = "#111827"
            draw_box(ax, box, color)
        ax.scatter([center[0]], [center[1]], [center[2]], color="#ef4444", s=14)
        ax.set_title(title)
        ax.set_xlabel("x forward (m)")
        ax.set_ylabel("y left (m)")
        ax.set_zlabel("z up (m)")
        ax.set_xlim(center[0] - radius, center[0] + radius)
        ax.set_ylim(center[1] - radius, center[1] + radius)
        ax.set_zlim(center[2] - radius, center[2] + radius)
        ax.view_init(elev=elev, azim=azim)
        ax.set_box_aspect([1, 1, 1])

    fig.suptitle("S.O.L.A.R. simplified URDF proxy", fontsize=16)
    fig.tight_layout()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT_PATH)
    write_obj(boxes)
    print(f"Wrote {OUT_PATH}")
    print(f"Wrote {OBJ_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
