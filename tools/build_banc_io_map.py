from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import numpy as np
import pyarrow.feather as feather

TOPO_HEADER = struct.Struct("<8sIIII11Q16s")
IO_HEADER = struct.Struct("<8s8I3Q")
IO_HEADER_SIZE = 64

S_VIS_L = 1 << 0
S_VIS_R = 1 << 1
S_BODY_L = 1 << 2
S_BODY_R = 1 << 3
S_BODY_C = 1 << 4

R_DN_L = 1 << 0
R_DN_R = 1 << 1
R_DN_C = 1 << 2
R_MN_L = 1 << 3
R_MN_R = 1 << 4
R_MN_C = 1 << 5
R_AN_L = 1 << 6
R_AN_R = 1 << 7

SENSORY_NAMES = {
    S_VIS_L: "visual_left",
    S_VIS_R: "visual_right",
    S_BODY_L: "body_left",
    S_BODY_R: "body_right",
    S_BODY_C: "body_center",
}
READOUT_NAMES = {
    R_DN_L: "descending_left",
    R_DN_R: "descending_right",
    R_DN_C: "descending_center",
    R_MN_L: "motor_left",
    R_MN_R: "motor_right",
    R_MN_C: "motor_center",
    R_AN_L: "ascending_left",
    R_AN_R: "ascending_right",
}


def norm(x) -> str:
    if x is None:
        return ""
    return str(x).strip().lower().replace("-", "_").replace(" ", "_")


def read_topology_root_ids(path: Path) -> np.ndarray:
    with path.open("rb") as f:
        raw = f.read(TOPO_HEADER.size)
        if len(raw) != TOPO_HEADER.size:
            raise RuntimeError("Short topology header.")
        fields = TOPO_HEADER.unpack(raw)
        magic = fields[0]
        version = fields[1]
        header_size = fields[2]
        neuron_count = fields[3]
        root_ids_offset = fields[10]
        if magic != b"FARV2\0\0\0" or version != 2 or header_size != 128:
            raise RuntimeError("Unsupported topology file.")
        f.seek(root_ids_offset)
        roots = np.fromfile(f, dtype="<u8", count=neuron_count)
        if len(roots) != neuron_count:
            raise RuntimeError("Could not read topology root IDs.")
        return roots


def classify_side(side: str) -> str:
    s = norm(side)
    if s in {"left", "l"}:
        return "left"
    if s in {"right", "r"}:
        return "right"
    if s in {"center", "central", "midline", "mid"}:
        return "center"
    return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--topology", required=True, type=Path)
    ap.add_argument("--meta", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    args = ap.parse_args()

    roots = read_topology_root_ids(args.topology)
    n = len(roots)

    wanted = [
        "banc_888_id", "side", "region", "super_class", "flow",
        "cell_class", "proofread", "roughly_proofread"
    ]
    table = feather.read_table(args.meta, columns=wanted, memory_map=True)

    ids = np.asarray(
        table["banc_888_id"].combine_chunks().to_numpy(zero_copy_only=False),
        dtype=np.uint64,
    )
    order = np.argsort(ids)
    ids_sorted = ids[order]

    pos = np.searchsorted(ids_sorted, roots)
    valid = pos < len(ids_sorted)
    matched = np.zeros(n, dtype=bool)
    p = np.flatnonzero(valid)
    if len(p):
        matched[p] = ids_sorted[pos[p]] == roots[p]

    if matched.mean() < 0.99:
        raise RuntimeError(
            f"Metadata/topology join coverage too low: {matched.mean()*100:.3f}%"
        )

    sensory = np.zeros(n, dtype="<u4")
    readout = np.zeros(n, dtype="<u4")

    # Audit the actual current metadata taxonomy instead of assuming that the
    # published documentation and mutable GCS snapshot use identical tokens.
    visual_candidates = {
        "super_class_optic": 0,
        "super_class_visual_projection": 0,
        "cell_class_visual_projection": 0,
        "region_optic_lobe": 0,
    }

    cols = {
        name: table[name].combine_chunks().to_pylist()
        for name in wanted if name != "banc_888_id"
    }

    unmatched_roots = []
    for compact in range(n):
        if not matched[compact]:
            unmatched_roots.append(int(roots[compact]))
            continue

        row = int(order[pos[compact]])
        side = classify_side(cols["side"][row])
        region = norm(cols["region"][row])
        sc = norm(cols["super_class"][row])
        flow = norm(cols["flow"][row])
        cc = norm(cols["cell_class"][row])

        if sc == "optic":
            visual_candidates["super_class_optic"] += 1
        if sc in {"visual_projection", "visual_projection_neuron"}:
            visual_candidates["super_class_visual_projection"] += 1
        if cc in {"visual_projection", "visual_projection_neuron"}:
            visual_candidates["cell_class_visual_projection"] += 1
        if region == "optic_lobe":
            visual_candidates["region_optic_lobe"] += 1

        # Visual membership is assigned in a second pass after we know which
        # taxonomy tokens actually exist in this mutable metadata snapshot.

        # General body sensory afferent proxy. Modality-specific nerve/cell-class
        # routing will be refined in later releases.
        if sc == "sensory" and flow == "afferent":
            if side == "left":
                sensory[compact] |= S_BODY_L
            elif side == "right":
                sensory[compact] |= S_BODY_R
            else:
                sensory[compact] |= S_BODY_C

        if sc == "descending":
            if side == "left":
                readout[compact] |= R_DN_L
            elif side == "right":
                readout[compact] |= R_DN_R
            else:
                readout[compact] |= R_DN_C

        if sc == "motor":
            if side == "left":
                readout[compact] |= R_MN_L
            elif side == "right":
                readout[compact] |= R_MN_R
            else:
                readout[compact] |= R_MN_C

        if sc == "ascending":
            if side == "left":
                readout[compact] |= R_AN_L
            elif side == "right":
                readout[compact] |= R_AN_R


    explicit_visual_total = (
        visual_candidates["super_class_optic"]
        + visual_candidates["super_class_visual_projection"]
        + visual_candidates["cell_class_visual_projection"]
    )

    if explicit_visual_total > 0:
        visual_rule_used = (
            "explicit taxonomy: super_class in {optic, visual_projection} "
            "OR cell_class=visual_projection"
        )
        visual_rule_mode = "explicit_taxonomy"
    elif visual_candidates["region_optic_lobe"] > 0:
        visual_rule_used = "fallback: region=optic_lobe"
        visual_rule_mode = "optic_lobe_region_fallback"
    else:
        raise RuntimeError(
            "No visual population could be identified: neither explicit "
            "visual/optic taxonomy nor region=optic_lobe exists."
        )

    # Second pass: assign visual left/right with the selected rule.
    for compact in range(n):
        if not matched[compact]:
            continue

        row = int(order[pos[compact]])
        side = classify_side(cols["side"][row])
        region = norm(cols["region"][row])
        sc = norm(cols["super_class"][row])
        cc = norm(cols["cell_class"][row])

        if visual_rule_mode == "explicit_taxonomy":
            visual_proxy = (
                sc in {"optic", "visual_projection", "visual_projection_neuron"}
                or cc in {"visual_projection", "visual_projection_neuron"}
            )
        else:
            visual_proxy = (region == "optic_lobe")

        if visual_proxy:
            if side == "left":
                sensory[compact] |= S_VIS_L
            elif side == "right":
                sensory[compact] |= S_VIS_R

    print("")
    print("Visual taxonomy audit")
    for k, v in visual_candidates.items():
        print(f"  {k:34s}: {v:,}")
    print(f"  selected rule                     : {visual_rule_used}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    sensor_off = IO_HEADER_SIZE
    readout_off = sensor_off + sensory.nbytes
    file_size = readout_off + readout.nbytes

    header = IO_HEADER.pack(
        b"FAIO1\0\0\0",
        1, IO_HEADER_SIZE, n, 1, 1, 0, 0, 0,
        sensor_off, readout_off, file_size,
    )

    with args.out.open("wb") as f:
        f.write(header)
        sensory.tofile(f)
        readout.tofile(f)

    sensory_counts = {
        name: int(np.count_nonzero(sensory & bit))
        for bit, name in SENSORY_NAMES.items()
    }
    readout_counts = {
        name: int(np.count_nonzero(readout & bit))
        for bit, name in READOUT_NAMES.items()
    }

    manifest = {
        "release": "FlyArena v0.6.1a",
        "format": "FAIO1",
        "neuron_count": n,
        "join_coverage": float(matched.mean()),
        "unmatched_topology_root_ids": unmatched_roots,
        "sensory_groups": sensory_counts,
        "readout_groups": readout_counts,
        "rules": {
            "visual_proxy": visual_rule_used + ", split by side",
            "body_sensory_proxy": "super_class == sensory AND flow == afferent, split by side",
            "descending_readout": "super_class == descending, split by side",
            "motor_readout": "super_class == motor, split by side",
            "ascending_telemetry": "super_class == ascending, left/right",
        },
        "visual_taxonomy_audit": visual_candidates,
        "visual_rule_mode": visual_rule_mode,
        "scientific_notes": [
            "Visual proxy is not a photoreceptor model.",
            "BANC is missing lamina and ocellar ganglion; visual input is injected downstream.",
            "Body sensory proxy is modality-agnostic in v0.6.1.",
            "Readouts are neural activity measurements, not direct game actions."
        ]
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print("============================================================")
    print(" FlyArena v0.6.1 BANC IO map")
    print("============================================================")
    print(f"neurons          : {n:,}")
    print(f"join coverage    : {matched.mean()*100:.3f}%")
    print("")
    print("Sensory groups")
    for k, v in sensory_counts.items():
        print(f"  {k:18s}: {v:,}")
    print("")
    print("Readout groups")
    for k, v in readout_counts.items():
        print(f"  {k:18s}: {v:,}")
    print("")
    print(f"wrote: {args.out}")
    print(f"wrote: {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
