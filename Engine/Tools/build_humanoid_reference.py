#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Calibrates the UniVex humanoid reference's A-pose from a rigged glTF binary (.glb).

The reference keeps its own bone names, hierarchy, frames and lengths; only the limbs are turned
so their directions match the measured rig: the upper arm, forearm and hand, the thigh, shin and
foot, left and right. Corrective bones are marked optional (generated only on request).

    python3 Engine/Tools/build_humanoid_reference.py --glb rig.glb \
        --reference Engine/Runtime/Retarget/Runtime/Data/humanoid_reference.json

The measured rig is only read: nothing of it is copied, only the limb directions it rests in.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys

# ---------------------------------------------------------------------------------------- math

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def qinv(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qrot(q, v):
    x, y, z, w = q
    tx = 2.0 * (y * v[2] - z * v[1])
    ty = 2.0 * (z * v[0] - x * v[2])
    tz = 2.0 * (x * v[1] - y * v[0])
    return [v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx)]


def norm(v):
    length = math.sqrt(sum(c * c for c in v))
    return [c / length for c in v] if length > 1e-12 else [0.0, 0.0, 0.0]


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def add(a, b):
    return [a[i] + b[i] for i in range(3)]


def between(a, b):
    """The shortest rotation taking direction a onto direction b."""
    a, b = norm(a), norm(b)
    d = sum(a[i] * b[i] for i in range(3))
    if d > 1.0 - 1e-9:
        return [0.0, 0.0, 0.0, 1.0]
    if d < -1.0 + 1e-9:
        axis = norm([0.0, -a[2], a[1]] if abs(a[0]) < 0.9 else [-a[2], 0.0, a[0]])
        return axis + [0.0]
    c = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    q = c + [1.0 + d]
    length = math.sqrt(sum(x * x for x in q))
    return [x / length for x in q]


def m4(n):
    if "matrix" in n:
        m = n["matrix"]
        return [[m[0], m[4], m[8], m[12]], [m[1], m[5], m[9], m[13]], [m[2], m[6], m[10], m[14]], [0, 0, 0, 1]]
    t = n.get("translation", [0, 0, 0])
    x, y, z, w = n.get("rotation", [0, 0, 0, 1])
    s = n.get("scale", [1, 1, 1])
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def mmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

# ---------------------------------------------------------------------------------- the rig read

def read_glb_positions(path):
    """World rest positions of every node of a .glb, by its name."""
    data = open(path, "rb").read()
    if data[:4] != b"glTF":
        raise SystemExit(f"{path}: not a glTF binary")
    length = struct.unpack("<I", data[12:16])[0]
    doc = json.loads(data[20:20 + length])
    nodes = doc["nodes"]
    parent = {}
    for index, node in enumerate(nodes):
        for child in node.get("children", []):
            parent[child] = index
    world = {}

    def at(index):
        if index not in world:
            local = m4(nodes[index])
            world[index] = mmul(at(parent[index]), local) if index in parent else local
        return world[index]

    return {node.get("name", f"node{index}"): [at(index)[0][3], at(index)[1][3], at(index)[2][3]]
            for index, node in enumerate(nodes)}


def key(name):
    """A rig bone name reduced to its words: lower case, no export numbering."""
    name = name.lower()
    name = re.sub(r"_?\d+$", "", name)
    return re.sub(r"[^a-z0-9]+", "_", name).strip("_")


# The limb segments measured, as (our bone, our bone it points at) and the word patterns a rig's
# bones commonly use for each end. Left side; the right is the same with r for l.
SEGMENTS = [
    ("UpperArm_L", "ForeArm_L", [r"^upper_?arm_l$", r"^l_?upper_?arm$", r"^arm_l$", r"^left_?arm$"],
     [r"^(lower|fore)_?arm_l$", r"^l_?(lower|fore)_?arm$", r"^left_?fore_?arm$"]),
    ("ForeArm_L", "Hand_L", [r"^(lower|fore)_?arm_l$", r"^l_?(lower|fore)_?arm$", r"^left_?fore_?arm$"],
     [r"^hand_l$", r"^l_?hand$", r"^left_?hand$"]),
    ("Hand_L", "Middle1_L", [r"^hand_l$", r"^l_?hand$", r"^left_?hand$"],
     [r"^middle_?0?1_l$", r"^l_?middle_?0?1$", r"^left_?hand_?middle_?1$"]),
    ("Thigh_L", "Shin_L", [r"^thigh_l$", r"^l_?thigh$", r"^left_?up_?leg$", r"^upper_?leg_l$"],
     [r"^(calf|shin)_l$", r"^l_?(calf|shin)$", r"^left_?leg$", r"^lower_?leg_l$"]),
    ("Shin_L", "Foot_L", [r"^(calf|shin)_l$", r"^l_?(calf|shin)$", r"^left_?leg$", r"^lower_?leg_l$"],
     [r"^foot_l$", r"^l_?foot$", r"^left_?foot$"]),
    ("Foot_L", "Toe_L", [r"^foot_l$", r"^l_?foot$", r"^left_?foot$"],
     [r"^(ball|toe)_l$", r"^l_?(ball|toe)$", r"^left_?toe_?base$"]),
]


def find(positions, patterns):
    for name, position in positions.items():
        for pattern in patterns:
            if re.match(pattern, key(name)):
                return position
    return None


def mirrored(patterns):
    out = []
    for p in patterns:
        p = p.replace("_l$", "_r$").replace("^l_", "^r_").replace("left", "right")
        out.append(p)
    return out

# ------------------------------------------------------------------------------- the reference

def world_of(bones):
    index = {b["name"]: i for i, b in enumerate(bones)}
    world = []
    for bone in bones:
        if bone["parent"]:
            pp, pr = world[index[bone["parent"]]]
            world.append((add(pp, qrot(pr, bone["position"])), qmul(pr, bone["rotation"])))
        else:
            world.append((list(bone["position"]), list(bone["rotation"])))
    return world, index


def below(bones, index, bone, ancestor):
    while bone >= 0:
        if bone == ancestor:
            return True
        parent = bones[bone]["parent"]
        bone = index[parent] if parent else -1
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glb", required=True, help="a rigged .glb resting in the A-pose to measure")
    parser.add_argument("--reference", required=True, help="the humanoid reference JSON, rewritten in place")
    args = parser.parse_args()

    positions = read_glb_positions(args.glb)
    doc = json.load(open(args.reference))
    bones = doc["bones"]
    world, index = world_of(bones)
    lowest_before = min(world[index["Foot_L"]][0][1], world[index["Foot_R"]][0][1])

    turned = 0
    for side in ("L", "R"):
        for ours, aim, start, end in SEGMENTS:
            ours = ours.replace("_L", "_" + side)
            aim = aim.replace("_L", "_" + side)
            if side == "R":
                start, end = mirrored(start), mirrored(end)
            a, b = find(positions, start), find(positions, end)
            if a is None or b is None or ours not in index or aim not in index:
                print(f"  {ours}: not measured (the rig has no such segment)")
                continue
            measured = norm(sub(b, a))
            bone, tip = index[ours], index[aim]
            current = sub(world[tip][0], world[bone][0])
            turn = between(current, measured)
            pivot = world[bone][0]
            for other in range(len(bones)):
                if below(bones, index, other, bone):
                    p, r = world[other]
                    world[other] = (add(pivot, qrot(turn, sub(p, pivot))), qmul(turn, r))
            turned += 1
            print(f"  {ours} -> {aim}: {[round(c, 3) for c in measured]}")

    # An IK target rests exactly on the bone it follows (it is not under it).
    for i, bone in enumerate(bones):
        follows = bone.get("follows")
        if follows and follows in index:
            world[i] = (list(world[index[follows]][0]), list(world[index[follows]][1]))

    # Back to locals, and the hips' height over the new ground.
    for i, bone in enumerate(bones):
        if bone["parent"]:
            pp, pr = world[index[bone["parent"]]]
            inverse = qinv(pr)
            bone["position"] = qrot(inverse, sub(world[i][0], pp))
            bone["rotation"] = norm4(qmul(inverse, world[i][1]))
        if bone.get("kind") == "corrective":
            bone["optional"] = True
        else:
            bone.pop("optional", None)
    lowest_after = min(world[index["Foot_L"]][0][1], world[index["Foot_R"]][0][1])
    doc["hipsHeight"] = doc["hipsHeight"] + (lowest_before - lowest_after)
    doc["units"] = "metres, +Y up, facing +Z, left toward +X, A-pose (measured)"
    with open(args.reference, "w") as out:
        json.dump(doc, out, indent="\t")
        out.write("\n")
    print(f"{turned} limb segments calibrated, {sum(1 for b in bones if b.get('optional'))} optional bones")
    return 0


def norm4(q):
    length = math.sqrt(sum(c * c for c in q))
    return [c / length for c in q]


if __name__ == "__main__":
    sys.exit(main())
