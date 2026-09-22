from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pyarrow.csv as pacsv
import pyarrow.feather as feather

MAGIC = b"FARV2\0\0\0"
HEADER_STRUCT = struct.Struct("<8sIIII11Q16s")
HEADER_SIZE = 128

NT_CODES = {
    "acetylcholine": 1,
    "dopamine": 2,
    "gaba": 3,
    "glutamate": 4,
    "histamine": 5,
    "octopamine": 6,
    "serotonin": 7,
    "tyramine": 8,
}

REGION_CODES = {
    "central_brain": 1,
    "optic_lobe": 2,
    "ventral_nerve_cord": 3,
    "cervical_connective": 4,
}

REGION_ALIASES = {
    "central_brain": "central_brain",
    "brain": "central_brain",
    "optic_lobe": "optic_lobe",
    "optic": "optic_lobe",
    "ventral_nerve_cord": "ventral_nerve_cord",
    "vnc": "ventral_nerve_cord",
    "cervical_connective": "cervical_connective",
    "neck_connective": "cervical_connective",
    "neck": "cervical_connective",
}


def sha256_file(path: Path, chunk_size: int = 8 * 1024 * 1024) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def arr_u64(col) -> np.ndarray:
    return np.asarray(col.combine_chunks().to_numpy(zero_copy_only=False), dtype="<u8")


def arr_u32(col) -> np.ndarray:
    return np.asarray(col.combine_chunks().to_numpy(zero_copy_only=False), dtype="<u4")


def arr_f32(col) -> np.ndarray:
    return np.asarray(col.combine_chunks().to_numpy(zero_copy_only=False), dtype="<f4")


def values(col) -> list:
    return col.combine_chunks().to_pylist()


def normalize_token(value) -> str:
    if value is None:
        return ""
    s = str(value).strip().lower()
    s = re.sub(r"[\s\-]+", "_", s)
    s = re.sub(r"_+", "_", s)
    return s.strip("_")


def truthy(value) -> bool:
    if value is None:
        return False
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    if isinstance(value, (int, np.integer)):
        return int(value) != 0
    if isinstance(value, (float, np.floating)):
        return np.isfinite(value) and float(value) != 0.0
    return normalize_token(value) in {
        "1", "true", "t", "yes", "y",
        "proofread", "roughly_proofread", "roughlyproofread"
    }


def exact_index(canonical: np.ndarray, ids: np.ndarray, label: str) -> np.ndarray:
    idx = np.searchsorted(canonical, ids)
    valid = idx < len(canonical)
    match = np.zeros(len(ids), dtype=bool)
    p = np.flatnonzero(valid)
    if len(p):
        match[p] = canonical[idx[p]] == ids[p]
    if not np.all(match):
        bad = ids[~match]
        preview = ", ".join(str(int(x)) for x in bad[:10])
        raise RuntimeError(
            f"{label}: {len(bad):,} IDs do not exist in latest metrics universe. "
            f"Examples: {preview}"
        )
    return idx.astype("<u4", copy=False)



def compatible_index(canonical: np.ndarray, ids: np.ndarray, label: str):
    """
    Map an auxiliary table onto the canonical neuron universe without
    pretending version-mismatched orphan rows belong to the latest graph.

    Returns:
        matched_compact_indices: compact uint32 indices for matched input rows
        matched_input_mask: bool mask over the original input rows
        orphan_ids: input IDs not present in canonical
    """
    idx = np.searchsorted(canonical, ids)
    valid = idx < len(canonical)

    matched = np.zeros(len(ids), dtype=bool)
    pos = np.flatnonzero(valid)
    if len(pos):
        matched[pos] = canonical[idx[pos]] == ids[pos]

    orphan_ids = ids[~matched]
    if len(orphan_ids):
        preview = ", ".join(str(int(x)) for x in orphan_ids[:10])
        print(
            f"      [COMPAT] {label}: ignoring {len(orphan_ids):,} orphan rows "
            f"not present in latest metrics."
        )
        print(f"               examples: {preview}")

    return idx[matched].astype("<u4", copy=False), matched, orphan_ids


def align64(f) -> int:
    pad = (-f.tell()) % 64
    if pad:
        f.write(b"\0" * pad)
    return f.tell()


def write_array(f, arr: np.ndarray) -> int:
    off = align64(f)
    arr.tofile(f)
    return off


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Build FlyArena latest-BANC cache: v888 metadata/metrics + v3 topology + v2-derived NT."
    )
    ap.add_argument("--metrics", required=True, type=Path)
    ap.add_argument("--meta", required=True, type=Path)
    ap.add_argument("--edges", required=True, type=Path)
    ap.add_argument("--nt", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--source-manifest", type=Path)
    args = ap.parse_args()

    for p in (args.metrics, args.meta, args.edges, args.nt):
        if not p.exists():
            raise RuntimeError(f"Missing input: {p}")

    t0 = time.time()

    print("[1/8] Reading latest v888 metrics (canonical neuron universe)...")
    metrics = feather.read_table(args.metrics, columns=["banc_888_id"], memory_map=True)
    root_ids = arr_u64(metrics["banc_888_id"])
    if len(root_ids) < 100_000:
        raise RuntimeError(f"Suspicious neuron count: {len(root_ids):,}")
    root_ids.sort()
    if np.any(root_ids[1:] == root_ids[:-1]):
        raise RuntimeError("Duplicate banc_888_id values in metrics.")
    neuron_count = len(root_ids)
    del metrics
    print(f"      neurons: {neuron_count:,}")

    print("[2/8] Reading latest v3 neuron-to-neuron connectivity...")
    edges = feather.read_table(
        args.edges,
        columns=["pre", "post", "count", "norm"],
        memory_map=True,
    )
    pre = arr_u64(edges["pre"])
    post = arr_u64(edges["post"])
    count = arr_u32(edges["count"])
    norm = arr_f32(edges["norm"])
    edge_count = len(pre)
    del edges

    if edge_count < 1_000_000:
        raise RuntimeError(f"Suspiciously small v3 edge list: {edge_count:,}")
    if np.any(count == 0):
        raise RuntimeError("v3 edge list contains zero-count connections.")
    if not np.all(np.isfinite(norm)) or np.any(norm < 0):
        raise RuntimeError("v3 norm contains invalid values.")

    synapse_sum = int(count.astype(np.uint64).sum())
    if synapse_sum <= edge_count:
        raise RuntimeError("Synapse sum is not larger than edge-pair count; input appears invalid.")

    print(f"      directed pairs : {edge_count:,}")
    print(f"      synapse sum    : {synapse_sum:,}")

    print("[3/8] Mapping all v3 edge endpoints into canonical metrics IDs...")
    src = exact_index(root_ids, pre, "edge.pre")
    dst = exact_index(root_ids, post, "edge.post")
    del pre, post

    print("[4/8] Constructing CSR...")
    order = np.argsort(src, kind="stable")
    src = src[order]
    dst = dst[order]
    count = count[order]
    norm = norm[order]

    degrees = np.bincount(src, minlength=neuron_count).astype("<u8")
    offsets = np.empty(neuron_count + 1, dtype="<u8")
    offsets[0] = 0
    np.cumsum(degrees, out=offsets[1:])
    del degrees, src, order

    print("[5/8] Joining latest metadata where available...")
    meta = feather.read_table(args.meta, memory_map=True)
    if "banc_888_id" not in meta.column_names:
        raise RuntimeError("Latest metadata has no banc_888_id column.")
    meta_ids = arr_u64(meta["banc_888_id"])
    meta_idx = exact_index(root_ids, meta_ids, "meta.banc_888_id")
    metadata_coverage = len(np.unique(meta_idx)) / neuron_count

    region = np.zeros(neuron_count, dtype="u1")
    flags = np.zeros(neuron_count, dtype="u1")
    observed_regions: dict[str, int] = {}

    if "region" in meta.column_names:
        for j, compact in enumerate(meta_idx):
            token = normalize_token(meta["region"][j].as_py())
            token = REGION_ALIASES.get(token, token)
            observed_regions[token or "<empty>"] = observed_regions.get(token or "<empty>", 0) + 1
            region[compact] = REGION_CODES.get(token, 0)

    if "proofread" in meta.column_names:
        for j, compact in enumerate(meta_idx):
            if truthy(meta["proofread"][j].as_py()):
                flags[compact] |= 1

    if "roughly_proofread" in meta.column_names:
        for j, compact in enumerate(meta_idx):
            if truthy(meta["roughly_proofread"][j].as_py()):
                flags[compact] |= 2

    proofread_count = int(np.count_nonzero(flags & 1))
    roughly_count = int(np.count_nonzero(flags & 2))
    print(f"      metadata rows/coverage : {len(meta_idx):,} / {metadata_coverage*100:.2f}%")
    print(f"      proofread             : {proofread_count:,}")
    print(f"      roughly proofread     : {roughly_count:,}")
    print(f"      known region          : {int(np.count_nonzero(region)):,}")

    if metadata_coverage < 0.90:
        raise RuntimeError(f"Metadata coverage too low: {metadata_coverage*100:.2f}%")

    print("[6/8] Joining v2-derived neuron neurotransmitter predictions...")
    nt = pacsv.read_csv(
        args.nt,
        convert_options=pacsv.ConvertOptions(
            include_columns=[
                "root_id",
                "neurotransmitter_predicted",
                "neurotransmitter_score",
            ]
        ),
    )
    nt_ids = arr_u64(nt["root_id"])
    nt_idx, nt_match_mask, nt_orphan_ids = compatible_index(
        root_ids, nt_ids, "nt.root_id"
    )
    nt_class = np.zeros(neuron_count, dtype="u1")
    nt_score = np.zeros(neuron_count, dtype="<f4")

    names_all = values(nt["neurotransmitter_predicted"])
    scores_all = values(nt["neurotransmitter_score"])

    matched_rows = np.flatnonzero(nt_match_mask)
    for k, compact in enumerate(nt_idx):
        j = int(matched_rows[k])
        nt_class[compact] = NT_CODES.get(normalize_token(names_all[j]), 0)
        if scores_all[j] is not None:
            try:
                x = float(scores_all[j])
                if math.isfinite(x):
                    nt_score[compact] = x
            except (TypeError, ValueError):
                pass

    nt_unique_matched = len(np.unique(nt_idx))
    nt_coverage = nt_unique_matched / neuron_count
    nt_source_match_ratio = len(nt_idx) / max(1, len(nt_ids))
    classified = int(np.count_nonzero(nt_class))
    unknown_neurons = neuron_count - classified

    print(
        f"      NT matched rows       : {len(nt_idx):,} / {len(nt_ids):,}"
    )
    print(f"      NT orphan rows        : {len(nt_orphan_ids):,}")
    print(f"      NT source match ratio : {nt_source_match_ratio*100:.4f}%")
    print(f"      NT neuron coverage    : {nt_coverage*100:.2f}%  (informational)")
    print(f"      classified            : {classified:,}")
    print(f"      unknown/unclassified  : {unknown_neurons:,}")

    # The v2-derived NT table is an auxiliary annotation table, not the
    # canonical neuron universe. Neurons can legitimately have no NT call
    # (for example, no classified presynapses). Therefore total-CNS coverage
    # is informational only.
    #
    # What *is* an integrity check is whether nearly all rows that the NT
    # source actually contains still map into the latest metrics universe.
    if nt_source_match_ratio < 0.995:
        raise RuntimeError(
            f"NT source compatibility too low: {nt_source_match_ratio*100:.4f}% "
            f"({len(nt_orphan_ids):,} orphan rows out of {len(nt_ids):,})."
        )

    print("[7/8] Writing FlyArena cache...")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as f:
        f.write(b"\0" * HEADER_SIZE)
        offsets_offset = write_array(f, offsets)
        targets_offset = write_array(f, dst)
        synapse_count_offset = write_array(f, count)
        norm_offset = write_array(f, norm)
        root_ids_offset = write_array(f, root_ids)
        nt_class_offset = write_array(f, nt_class)
        nt_score_offset = write_array(f, nt_score)
        region_offset = write_array(f, region)
        neuron_flags_offset = write_array(f, flags)
        file_size = f.tell()

        header = HEADER_STRUCT.pack(
            MAGIC, 2, HEADER_SIZE, neuron_count, 1, edge_count,
            offsets_offset, targets_offset, synapse_count_offset, norm_offset,
            root_ids_offset, nt_class_offset, nt_score_offset,
            region_offset, neuron_flags_offset, file_size, b"\0"*16
        )
        f.seek(0)
        f.write(header)

    print("[8/8] Writing provenance manifest...")
    source_manifest = {}
    if args.source_manifest and args.source_manifest.exists():
        source_manifest = json.loads(args.source_manifest.read_text(encoding="utf-8-sig"))

    nt_counts = {"unknown": int(np.count_nonzero(nt_class == 0))}
    for name, code in NT_CODES.items():
        nt_counts[name] = int(np.count_nonzero(nt_class == code))

    region_counts = {"unknown": int(np.count_nonzero(region == 0))}
    for name, code in REGION_CODES.items():
        region_counts[name] = int(np.count_nonzero(region == code))

    manifest = {
        "format": "FlyArena V2 topology cache",
        "flyarena_release": "0.5.2c-bom-fix-pipeline",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "anatomy": {
            "dataset": "BANC",
            "materialization": 888,
            "metrics": "latest public GCS compiled_data/banc_888",
            "metadata": "latest public GCS compiled_data/banc_888",
            "connectivity": "synapses v3 / simple edgelist",
            "neurotransmitter": "v2-derived per-neuron prediction summary; missing calls remain Unknown; orphan IDs not present in latest metrics are excluded and recorded",
        },
        "observed": {
            "neurons": neuron_count,
            "directed_edge_pairs": edge_count,
            "synapse_count_sum": synapse_sum,
            "metadata_rows": len(meta_idx),
            "metadata_coverage": metadata_coverage,
            "nt_rows_source": len(nt_ids),
            "nt_rows_matched": len(nt_idx),
            "nt_orphan_rows": len(nt_orphan_ids),
            "nt_orphan_root_ids": [int(x) for x in nt_orphan_ids.tolist()],
            "nt_coverage": nt_coverage,
            "nt_source_match_ratio": nt_source_match_ratio,
            "nt_unknown_or_unclassified_neurons": unknown_neurons,
            "proofread_count": proofread_count,
            "roughly_proofread_count": roughly_count,
        },
        "structural_validation": {
            "unique_canonical_neuron_ids": True,
            "all_v3_edge_endpoints_in_metrics": True,
            "positive_edge_counts": True,
            "finite_nonnegative_norm": True,
            "metadata_coverage_at_least_90pct": True,
            "nt_source_match_ratio_at_least_99_5pct": True,
            "nt_total_neuron_coverage_is_informational": True,
            "nt_orphans_excluded_from_latest_graph": True,
        },
        "neurotransmitter_counts": nt_counts,
        "region_counts": region_counts,
        "observed_region_tokens": observed_regions,
        "source_download": source_manifest,
        "input_sha256": {
            "metrics": sha256_file(args.metrics),
            "meta": sha256_file(args.meta),
            "edges_v3": sha256_file(args.edges),
            "nt_v2": sha256_file(args.nt),
        },
        "cache": {
            "name": args.out.name,
            "bytes": args.out.stat().st_size,
            "sha256": sha256_file(args.out),
        },
        "conversion_seconds": round(time.time() - t0, 3),
    }

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print()
    print("============================================================")
    print(" FlyArena v0.5.2 latest-BANC cache READY")
    print("============================================================")
    print(f"neurons          : {neuron_count:,}")
    print(f"directed pairs   : {edge_count:,}")
    print(f"synapse sum      : {synapse_sum:,}")
    print(f"metadata coverage: {metadata_coverage*100:.2f}%")
    print(f"NT coverage      : {nt_coverage*100:.2f}%")
    print(f"cache            : {args.out}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"\n[FATAL] {exc}", file=sys.stderr)
        raise
