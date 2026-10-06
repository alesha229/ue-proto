"""Long-take corrective basis fit outside Blender (fast multithreaded numpy).

Reads the rest-space delta rows written by author_vam_mocap.py PHASE 'basis_rows'
(evidence/05/kitty_mocap_full/basis_parts) and writes the projector npz that the
Blender phases 'basis_restore' (shapes) and 'segment_coef' (coefficients) use.
Pure math: no model data is changed here.
"""
import json
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "evidence/05/kitty_mocap_full"
MAX_RANK = int(sys.argv[1]) if len(sys.argv) > 1 else 64
TARGET_MM = 1.0
started = time.time()
parts = [np.load(p) for p in sorted((EVIDENCE / "basis_parts").glob("rows_*.npz"))]
names = [str(n) for n in parts[0]["names"]]
assert all([str(n) for n in p["names"]] == names for p in parts), "row parts use different layouts"
D = np.concatenate([p["rows"] for p in parts]).astype(np.float32)
values, vectors = np.linalg.eigh((D @ D.T).astype(np.float64))
vectors = vectors[:, values.argsort()[::-1]]
chosen, residuals = MAX_RANK, []
for rank in range(8, MAX_RANK + 1, 8):
    U = vectors[:, :rank].astype(np.float32)
    rest = D - U @ (U.T @ D)
    residuals.append((rank, float(np.linalg.norm(rest.reshape(len(D), -1, 3), axis=2).max()) * 1000))
    print("rank %d residual %.2f mm" % residuals[-1], flush=True)
    if residuals[-1][1] <= TARGET_MM:
        chosen = rank
        break
U = vectors[:, :chosen]
scales = np.array([max(1e-9, float(np.abs(U[:, k]).max())) for k in range(chosen)])
basis = (D.T.astype(np.float64) @ U) * scales
projector = basis @ np.linalg.inv(basis.T @ basis)
np.savez(EVIDENCE / "km466full_basis.npz", projector=projector.astype(np.float32), names=np.array(names),
         counts=np.array([len(parts[0]["idx_%d" % n]) for n in range(len(names))]),
         **{"idx_%d" % n: parts[0]["idx_%d" % n] for n in range(len(names))})
result = {"phase": "basis_fit", "rank": chosen, "residual_by_rank_mm": residuals, "rows": int(len(D)),
          "active_vertices": {n: int(len(parts[0]["idx_%d" % i])) for i, n in enumerate(names)},
          "elapsed_seconds": time.time() - started}
(EVIDENCE / "km466full_basis_fit.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result))
