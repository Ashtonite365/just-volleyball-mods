#!/usr/bin/env python3
"""Rebuild mods/models/plump_bean/aero_bean.glb for the "arms" follow-hands rig.

The Plump Bean's arms used to be skinned tubes on the game's arm/forearm/hand bones.
The game draws its hand spheres (Visual/HandL|HandR) at hand_local() = hand bone + a
constant offset in idle/animated poses, and at fixed procedural targets (POSE_BUMP_*,
POSE_SET_*, POSE_SERVE_*, block, windup...) in the others, so a skinned arm never reaches
them. The arms are now drawn by jvm_models ("arms" in skin.json): shoulder -> elbow -> the
game's own hand sphere every frame. This script writes the same head/torso/legs as the
original generator (build_aero_models.py in the folder you pass, imported read-only,
byte-identical output checked) without the two skinned arm tubes.

usage: python3 tools/build_bean_follow_arms.py [generator_dir] [out_glb]
"""
import os
import sys

if len(sys.argv) < 2:
    sys.exit("usage: build_bean_follow_arms.py <model-source-folder>")
gen = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "dist", "game_dir", "mods", "models", "plump_bean", "aero_bean.glb")
sys.path.insert(0, gen)
import json
import build_aero_models as B

d = json.load(open(B.SKEL_JSON))
bones = d["bones"]
bmap = {b["name"]: b["idx"] for b in bones}
parts = []
hv = B.build_skinned_sphere(0.402, (0.0, 3.508, 0.0), bmap["head"], rings=22, segs=36)
parts.append(("head",) + tuple(hv))
tv = B.build_skinned_torso(1.465, 2.985, bmap["spine"], bmap["spine1"], body_rings=36, cap_rings=9, segs=36)
parts.append(("torso",) + tuple(tv))
for side, sx in (("l", 1.0), ("r", -1.0)):
    leg_pts = [(sx * 0.250, 1.200, 0.000), (sx * 0.250, 0.701, 0.006), (sx * 0.250, 0.254, -0.015), (sx * 0.250, 0.256, 0.095)]
    leg_r = [0.184, 0.214, 0.222, 0.212]
    leg_bones = [bmap[f"thigh.{side}"], bmap[f"calf.{side}"], bmap[f"foot.{side}"]]
    lv = B.build_skinned_tube(leg_pts, leg_r, leg_bones, blend_half_widths=[0.18, 0.16], samples_per_seg=24, cap_rings=8, segs=28)
    parts.append((f"leg.{side}",) + tuple(lv))
B.write_glb(os.path.abspath(out), bones, parts, os.path.join(B.TEX_DIR, "aero_cherry.png"), material_name="aero_bean_mat")
