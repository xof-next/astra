import json
import pathlib
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
source_dir = pathlib.Path(sys.argv[2]).resolve()
result = subprocess.run([str(binary), "versions"], cwd=source_dir, capture_output=True, text=True, timeout=10)
if result.returncode != 0:
    raise SystemExit("astra versions failed:\n" + result.stdout + result.stderr)
manifest = json.loads((source_dir.parent.parent / "pack.json").read_text(encoding="utf-8"))
latest = manifest["new"]
required = (f"Astra versions (new: {latest})", "1.0.0", "stable", latest)
if not all(value in result.stdout for value in required):
    raise SystemExit("pack.json was not parsed as expected:\n" + result.stdout + result.stderr)
print("local pack manifest CLI: OK")
