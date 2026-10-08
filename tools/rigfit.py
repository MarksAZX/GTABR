#!/usr/bin/env python3
"""Fits one canonical humanoid skeleton to every character mesh and paints real skin weights.

Input : .gmesh files from tools/meshconv (mesh + the auto-rigger's skeleton, whose joints do not lie inside the body).
Output: the same .gmesh files rewritten with
  * bind joints placed from the mesh geometry itself (limb slices, hand tips, head/neck/spine centroids),
  * a rest (hanging, clip compatible) chain that is the bind chain with every bone's own corrective rotation removed,
  * skin weights from bone-segment distances smoothed over the mesh surface (no limb bleeding),
  * header flag kRigFitted, so the runtime stops patching the skeleton at load time.

The auto-rigger's rest ROTATIONS are kept untouched: the animation clips (Meshy library) drive rotations only, so every
character shares exactly the same clips and joint names.

usage: rigfit.py <work_dir> <reference gmesh (protagonist, unfitted copy)> name [name ...]
"""
import struct, sys, os
import numpy as np
from scipy.spatial import cKDTree
import scipy.sparse as sp

NAME = 48
HDR = struct.Struct("<5I3f3f3f192s" + "12f" + "2f" + "6f6f" + "f3f")  # see src/gfx/gmesh.h
BONE = struct.Struct("<48si16f3f4f3f")
VERT = struct.Struct("<3f4b4b2f4B4B")
FLAG_FITTED = 4


# ---------------------------------------------------------------------------------------------------------------- io
def read_gmesh(path):
    b = open(path, "rb").read()
    h = list(HDR.unpack_from(b, 0))
    off = HDR.size
    bones = []
    for _ in range(h[3]):
        v = BONE.unpack_from(b, off)
        off += BONE.size
        bones.append(dict(name=v[0].split(b"\0")[0].decode(), parent=v[1], inv=np.array(v[2:18], np.float64).reshape(4, 4).T,
                          t=np.array(v[18:21]), r=np.array(v[21:25]), s=np.array(v[25:28])))
    lods = []
    for _ in range(h[4]):
        vc, ic = struct.unpack_from("<2I", b, off)
        off += 8
        verts = [VERT.unpack_from(b, off + i * VERT.size) for i in range(vc)]
        off += vc * VERT.size
        idx = np.frombuffer(b, np.uint32, ic, off).copy()
        off += ic * 4
        lods.append(dict(verts=verts, idx=idx))
    return h, bones, lods


def write_gmesh(path, h, bones, lods):
    out = bytearray(HDR.pack(*h))
    for bn in bones:
        out += BONE.pack(bn["name"].encode().ljust(NAME, b"\0"), bn["parent"], *bn["inv"].T.reshape(16), *bn["t"], *bn["r"], *bn["s"])
    for lod in lods:
        out += struct.pack("<2I", len(lod["verts"]), len(lod["idx"]))
        for v in lod["verts"]:
            out += VERT.pack(*v)
        out += lod["idx"].astype(np.uint32).tobytes()
    open(path, "wb").write(bytes(out))


# ---------------------------------------------------------------------------------------------------------------- math
def quat_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_mat(r) * np.asarray(s)[None, :]
    m[:3, 3] = t
    return m


def rot_y(a):
    c, s = np.cos(a), np.sin(a)
    m = np.eye(4)
    m[0, 0], m[0, 2], m[2, 0], m[2, 2] = c, s, -s, c
    return m


def tr(p):
    m = np.eye(4)
    m[:3, 3] = p
    return m


def arc(a, b):
    """shortest rotation (4x4) taking direction a onto b"""
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(np.dot(a, b))
    m = np.eye(4)
    if c < -0.9999:
        ax = np.cross(a, [1, 0, 0])
        if np.linalg.norm(ax) < 1e-3:
            ax = np.cross(a, [0, 1, 0])
        ax /= np.linalg.norm(ax)
        K = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
        m[:3, :3] = np.eye(3) + 2 * K @ K
        return m
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    m[:3, :3] = np.eye(3) + K + K @ K / (1 + c)
    return m


def mat_quat(R):
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t + 1) * 2
        return np.array([(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s])
    i = int(np.argmax(np.diag(R)))
    j, k = (i + 1) % 3, (i + 2) % 3
    s = np.sqrt(R[i, i] - R[j, j] - R[k, k] + 1) * 2
    q = np.zeros(4)
    q[i] = 0.25 * s
    q[j] = (R[j, i] + R[i, j]) / s
    q[k] = (R[k, i] + R[i, k]) / s
    q[3] = (R[k, j] - R[j, k]) / s
    return q


# ---------------------------------------------------------------------------------------------------------------- fit
def fit_joints(V, front):
    """Joint positions (mesh space, A-pose) from geometry. front = +1/-1: which way the face looks along z."""
    H = V[:, 1].max()
    f = np.array([0, 0, front], float)
    right_x = -front          # facing +z -> the character's right hand is on -x
    left_sign = -right_x      # x sign of the left side
    mid = (np.abs(V[:, 0]) < 0.25 * H) & (V[:, 1] > 0.45 * H) & (V[:, 1] < 0.62 * H)
    xc = float(np.median(V[mid, 0]))

    def band(y0, y1, extra=None):
        m = (V[:, 1] >= y0 * H) & (V[:, 1] <= y1 * H)
        if extra is not None:
            m &= extra
        return V[m]

    def centre(pts):
        return pts.mean(0) if len(pts) else np.array([xc, 0, 0.0])

    J = {}
    torso = lambda y, hw=0.13: centre(band(y - 0.02, y + 0.02, np.abs(V[:, 0] - xc) < hw * H))
    for name, y in (("Hips", 0.551), ("Spine02", 0.62), ("Spine01", 0.693), ("Spine", 0.767), ("neck", 0.805)):
        c = torso(y)
        J[name] = np.array([xc, y * H, c[2]])
    hc = centre(band(0.84, 0.95, np.abs(V[:, 0] - xc) < 0.1 * H))
    J["Head"] = np.array([xc, 0.865 * H, hc[2]])
    J["head_end"] = np.array([xc, 0.995 * H, hc[2]])
    J["headfront"] = J["Head"] + f * 0.05 * H
    for side, sx in (("Left", left_sign), ("Right", -left_sign)):
        sm = ((V[:, 0] - xc) * sx > 0.012 * H)
        # legs
        thigh = centre(band(0.43, 0.47, sm & (np.abs(V[:, 0] - xc) < 0.2 * H)))
        knee = centre(band(0.27, 0.31, sm & (np.abs(V[:, 0] - xc) < 0.2 * H)))
        ank = centre(band(0.05, 0.10, sm & (np.abs(V[:, 0] - xc) < 0.2 * H)))
        hip = np.array([xc + sx * 0.055 * H, 0.497 * H, thigh[2]])
        hip[0] = xc + sx * max(0.045 * H, min(0.075 * H, abs(thigh[0] - xc) * 0.85))
        J[side + "UpLeg"] = hip
        J[side + "Leg"] = np.array([knee[0], 0.294 * H, knee[2]])
        J[side + "Foot"] = np.array([ank[0], 0.04 * H, ank[2] - front * 0.0 * H])
        foot_pts = band(0.0, 0.04, sm & (np.abs(V[:, 0] - xc) < 0.2 * H))
        toe_z = (foot_pts[:, 2] * front).max() * front if len(foot_pts) else ank[2] + front * 0.12 * H
        J[side + "ToeBase"] = np.array([ank[0], 0.02 * H, ank[2] + front * 0.6 * (toe_z - ank[2]) if abs(toe_z - ank[2]) > 0.02 * H else ank[2] + front * 0.07 * H])
        # arms: shoulder from the deltoid edge, hand tip from the most lateral vertices, elbow from the limb slice
        sh_band = band(0.75, 0.80, sm)
        outer = np.percentile(np.abs(sh_band[:, 0] - xc), 96) if len(sh_band) else 0.2 * H
        sh_x = xc + sx * max(0.09 * H, outer - 0.05 * H)
        sh_c = torso(0.775, 0.2)
        S = np.array([sh_x, 0.778 * H, sh_c[2]])
        arm = V[sm & (V[:, 1] < 0.7 * H) & (V[:, 1] > 0.3 * H) & (np.abs(V[:, 0] - xc) > 0.17 * H)]
        d = np.linalg.norm(arm - S, axis=1)
        tip = arm[np.argsort(-d)[: max(8, len(arm) // 150)]].mean(0)
        axis = (tip - S) / np.linalg.norm(tip - S)
        handlen = 0.105 * H
        W = tip - axis * handlen
        L = np.linalg.norm(W - S)
        E = S + axis * L * 0.56
        # snap the elbow and wrist to the real limb centre
        for key, base, lo, hi in (("E", None, 0.50, 0.62), ("W", None, 0.88, 0.99)):
            t = ((arm - S) @ axis) / max(L, 1e-6)
            r = np.linalg.norm((arm - S) - np.outer((arm - S) @ axis, axis), axis=1)
            sel = (t > lo) & (t < hi) & (r < 0.06 * H)
            if sel.sum() > 6:
                cc = arm[sel].mean(0)
                if key == "E":
                    E = cc
                else:
                    W = cc
        J[side + "Shoulder"] = np.array([xc + sx * 0.035 * H, 0.79 * H, sh_c[2]])
        J[side + "Arm"] = S
        J[side + "ForeArm"] = E
        J[side + "Hand"] = W
        J[side + "_tip"] = tip
    return J, xc, H


FOOT_ORDER = None
SEGMENTS = [  # bone, child joint (or special), radius (fraction of H), x-scale for anisotropic torso distance
    ("Hips", "Spine02", 0.11, 0.8), ("Spine02", "Spine01", 0.11, 0.8), ("Spine01", "Spine", 0.115, 0.8), ("Spine", "neck", 0.12, 0.8),
    ("neck", "Head", 0.045, 1.0), ("Head", "head_end", 0.10, 1.0),
    ("LeftShoulder", "LeftArm", 0.05, 1.0), ("LeftArm", "LeftForeArm", 0.05, 1.0), ("LeftForeArm", "LeftHand", 0.042, 1.0), ("LeftHand", "Left_tip", 0.04, 1.0),
    ("RightShoulder", "RightArm", 0.05, 1.0), ("RightArm", "RightForeArm", 0.05, 1.0), ("RightForeArm", "RightHand", 0.042, 1.0), ("RightHand", "Right_tip", 0.04, 1.0),
    ("LeftUpLeg", "LeftLeg", 0.085, 1.0), ("LeftLeg", "LeftFoot", 0.06, 1.0), ("LeftFoot", "LeftToeBase", 0.05, 1.0), ("LeftToeBase", "Left_toeend", 0.04, 1.0),
    ("RightUpLeg", "RightLeg", 0.085, 1.0), ("RightLeg", "RightFoot", 0.06, 1.0), ("RightFoot", "RightToeBase", 0.05, 1.0), ("RightToeBase", "Right_toeend", 0.04, 1.0),
]


def skin_weights(V, idx, J, names, xc, H, front, left_sign):
    J = dict(J)
    for side in ("Left", "Right"):
        J[side + "_toeend"] = J[side + "ToeBase"] + np.array([0, -0.01 * H, front * 0.05 * H])
    nb = len(names)
    bi = {n: i for i, n in enumerate(names)}
    N = len(V)
    D = np.full((N, nb), 1e9)
    for bone, child, rad, xs in SEGMENTS:
        if bone not in bi or bone not in J:
            continue
        a, b = J[bone], J[child]
        ab = b - a
        t = np.clip(((V - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        p = a + t[:, None] * ab
        d = V - p
        d[:, 0] *= xs
        dist = np.linalg.norm(d, axis=1) / (rad * H)
        # a limb never influences the other half of the body
        sx = left_sign if bone.startswith("Left") else (-left_sign if bone.startswith("Right") else 0)
        if sx != 0:
            wrong = ((V[:, 0] - xc) * sx) < -0.02 * H
            dist[wrong] = 1e9
        D[:, bi[bone]] = dist
    W = np.exp(-(np.minimum(D, 50) ** 2) * 1.6)
    W[D >= 1e8] = 0
    # keep the 4 best, then smooth across the surface so neighbouring vertices agree
    n_e = np.concatenate([idx.reshape(-1, 3)[:, [0, 1]], idx.reshape(-1, 3)[:, [1, 2]], idx.reshape(-1, 3)[:, [2, 0]]])
    A = sp.coo_matrix((np.ones(len(n_e)), (n_e[:, 0], n_e[:, 1])), shape=(N, N)).tocsr()
    A = ((A + A.T) > 0).astype(np.float64)
    deg = np.asarray(A.sum(1)).ravel()
    Dinv = sp.diags(1.0 / np.maximum(deg, 1))
    P = Dinv @ A
    for it in range(14):
        W = 0.5 * W + 0.5 * (P @ W)
        top = np.argsort(-W, axis=1)[:, :4]
        mask = np.zeros_like(W)
        np.put_along_axis(mask, top, 1.0, axis=1)
        W *= mask
        W /= np.maximum(W.sum(1, keepdims=True), 1e-9)
    return W


# ---------------------------------------------------------------------------------------------------------------- main
def process(work, ref, name):
    path = os.path.join(work, name + ".gmesh")
    h, bones, lods = read_gmesh(path)
    names = [b["name"] for b in bones]
    rh, rbones, _ = ref
    assert [b["name"] for b in rbones] == names, "skeleton mismatch with the reference"
    V0 = np.array([v[0:3] for v in lods[0]["verts"]], np.float64)
    H = V0[:, 1].max()
    # facing: the toes extend towards the front of the character
    xc0 = float(np.median(V0[(V0[:, 1] > 0.45 * H) & (V0[:, 1] < 0.62 * H) & (np.abs(V0[:, 0]) < 0.25 * H), 0]))
    fz = V0[(V0[:, 1] < 0.035 * H)][:, 2].mean() - V0[(V0[:, 1] > 0.07 * H) & (V0[:, 1] < 0.12 * H) & (np.abs(V0[:, 0] - xc0) < 0.25 * H)][:, 2].mean()
    front = 1.0 if fz > 0 else -1.0
    J, xc, H = fit_joints(V0, front)
    left_sign = front  # facing +z -> left = +x
    # reference rest chain in the rig frame
    g = []
    for i, b in enumerate(rbones):
        l = trs(b["t"], b["r"], b["s"])
        g.append(l if b["parent"] < 0 else g[b["parent"]] @ l)
    gp = np.array([m[:3, 3] for m in g])
    ni = {n: i for i, n in enumerate(names)}
    # Y turn taking the rig frame onto the mesh frame: rig "right" vector -> mesh right (-front along x)
    lr = gp[ni["RightArm"]] - gp[ni["LeftArm"]]
    lr[1] = 0
    lr /= np.linalg.norm(lr)
    want = np.array([-front, 0, 0.0])
    ang = np.arctan2(np.cross(lr, want)[1], np.dot(lr, want))
    ang = round(ang / (np.pi / 2)) * (np.pi / 2)   # the rig is only ever turned by quarter turns
    Rt = rot_y(ang)
    toe_dir = Rt[:3, :3] @ (gp[ni["LeftToeBase"]] - gp[ni["LeftFoot"]])
    print(f"{name}: H={H:.3f} front={front:+.0f} turn={np.degrees(ang):+.0f}deg toe.front={toe_dir[2] * front:+.3f}")
    # main child per bone (direction used for the corrective rotation)
    main = {"Hips": "Spine02", "Spine02": "Spine01", "Spine01": "Spine", "Spine": "neck", "neck": "Head", "Head": "head_end",
            "LeftShoulder": "LeftArm", "LeftArm": "LeftForeArm", "LeftForeArm": "LeftHand", "RightShoulder": "RightArm",
            "RightArm": "RightForeArm", "RightForeArm": "RightHand", "LeftUpLeg": "LeftLeg", "LeftLeg": "LeftFoot",
            "LeftFoot": "LeftToeBase", "RightUpLeg": "RightLeg", "RightLeg": "RightFoot", "RightFoot": "RightToeBase"}
    # metres per rig unit: the exporter works in centimetres, the unit change lives in rootFix = (l0 * invBind0)^-1
    rl0 = trs(rbones[0]["t"], rbones[0]["r"], rbones[0]["s"])
    rf_ref = np.linalg.inv(rl0 @ rbones[0]["inv"])
    S0 = float(np.linalg.norm(rf_ref[:3, 0]))
    Seff = S0 * float(bones[0]["s"][0])   # NPC skeletons are the reference one scaled by their height ratio (root scale)
    Rrot = []
    for i in range(len(bones)):
        lin = g[i][:3, :3]
        sc = np.linalg.norm(lin[:, 0])
        R = np.eye(4)
        R[:3, :3] = lin / sc
        Rrot.append(Rt @ R)
    P = {n: J[n] for n in names if n in J}
    Q = [np.eye(4) for _ in names]
    for i, n in enumerate(names):
        if n in main and main[n] in P and n in P:
            e_rest = Rt[:3, :3] @ (gp[ni[main[n]]] - gp[i])
            e_bind = P[main[n]] - P[n]
            Q[i] = arc(e_rest, e_bind)
        elif bones[i]["parent"] >= 0:
            Q[i] = Q[bones[i]["parent"]]
    Q[0] = np.eye(4)
    # new bones
    nb = []
    posRest = {}
    for i, b in enumerate(bones):
        nbn = dict(b)
        p = b["parent"]
        if p < 0:
            nbn["t"] = b["t"].copy()
        else:
            # local translation so that the rest chain equals the bind chain with each parent's own rotation removed
            v = (P[names[i]] - P[names[p]]) if names[i] in P and names[p] in P else (gp[i] - gp[p])
            loc = np.linalg.inv(Rrot[p][:3, :3]) @ (Q[p][:3, :3].T @ v) / Seff
            nbn["t"] = loc
        nb.append(nbn)
    # rest globals in mesh space (what the runtime will build), with rootFix chosen so that the hips sit on their bind joint
    root = nb[0]
    l0 = trs(root["t"], root["r"], root["s"])
    rootFix = tr(P["Hips"]) @ Rt @ np.diag([S0, S0, S0, 1.0]) @ tr(-root["t"])
    gr = []
    for i, b in enumerate(nb):
        l = trs(b["t"], b["r"], b["s"])
        gr.append(rootFix @ l if b["parent"] < 0 else gr[b["parent"]] @ l)
    for i, b in enumerate(nb):
        bind = tr(P[names[i]] if names[i] in P else gr[i][:3, 3]) @ Q[i] @ Rrot[i] @ np.diag([Seff, Seff, Seff, 1.0])
        b["inv"] = np.linalg.inv(bind)
    # hips: runtime derives rootFix = (l0 * invBind0)^-1, so invBind0 must be l0^-1 * rootFix^-1 with no bind rotation
    nb[0]["inv"] = np.linalg.inv(l0) @ np.linalg.inv(rootFix)
    # consistency: M_b = G_b * invBind_b must be a rigid move of the bind joint to its rest joint
    worst = 0
    for i in range(len(nb)):
        M = gr[i] @ nb[i]["inv"] if i else rootFix @ l0 @ nb[0]["inv"]
        R = M[:3, :3]
        worst = max(worst, abs(np.linalg.det(R) - 1.0))
    print(f"  rigid check max |det-1| = {worst:.4f}")
    # weights on LOD0, nearest-vertex transfer to the other LODs
    W = skin_weights(V0, lods[0]["idx"], J, names, xc, H, front, left_sign)
    top = np.argsort(-W, axis=1)[:, :4]
    tw = np.take_along_axis(W, top, axis=1)
    tw /= np.maximum(tw.sum(1, keepdims=True), 1e-9)

    # VERT layout: p3f n4b t4b uv2f j4B w4B  -> indices 0..2, 3..6, 7..10, 11..12, 13..16, 17..20
    def rebuild(verts, J4, W4):
        out = []
        for v, j4, w4 in zip(verts, J4, W4):
            wb = np.clip(np.round(w4 * 255), 0, 255).astype(int)
            wb[0] += 255 - wb.sum()
            out.append(tuple(v[:13]) + tuple(int(x) for x in j4) + tuple(int(x) for x in wb))
        return out

    lods[0]["verts"] = rebuild(lods[0]["verts"], top, tw)
    tree = cKDTree(V0)
    for l in (1, 2):
        Vl = np.array([v[0:3] for v in lods[l]["verts"]], np.float64)
        _, nn = tree.query(Vl)
        lods[l]["verts"] = rebuild(lods[l]["verts"], top[nn], tw[nn])
    h[2] |= FLAG_FITTED
    for i, b in enumerate(nb):
        b["r"] = bones[i]["r"]
        b["s"] = bones[i]["s"]
    write_gmesh(path, h, nb, lods)
    # report: per-bone vertex counts of the dominant influence
    dom = np.bincount(top[:, 0], minlength=len(names))
    print("  dominant-bone vertex counts:", {names[i]: int(dom[i]) for i in range(len(names)) if dom[i]})


if __name__ == "__main__":
    work, refp = sys.argv[1], sys.argv[2]
    ref = read_gmesh(refp)
    for n in sys.argv[3:]:
        process(work, ref, n)
