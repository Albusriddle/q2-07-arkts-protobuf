# Benchmark harness (planned)

- `data/` — 100 MB data cards: small, nested, bulk, wide; each with SHA-256 and distribution metadata.
- `run_all.sh` — host check / device run (device blocked until board arrives).
- `stats.py` — median, IQR, P95, bootstrap CI, CV.
- `report/` — generated markdown + CSV; no hand-edited numbers.

Do **not** put PC numbers in the device report. Host results are labeled `HOST-ONLY`.
