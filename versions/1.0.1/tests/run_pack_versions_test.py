import json
import pathlib
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
source_dir = pathlib.Path(sys.argv[2]).resolve()
result = subprocess.run([str(binary), "versions"], cwd=source_dir, capture_output=True, text=True, timeout=10)
if result.returncode != 0:
    raise SystemExit("astra versions failed:\n" + result.stdout + result.stderr)
alias_result = subprocess.run([str(binary), "v"], cwd=source_dir, capture_output=True, text=True, timeout=10)
if alias_result.returncode != 0 or alias_result.stdout != result.stdout:
    raise SystemExit("astra v alias did not match astra versions:\n" + alias_result.stdout + alias_result.stderr)
latest = json.loads((source_dir.parents[1] / "pack.json").read_text(encoding="utf-8"))["new"]
required = (f"Astra versions (new: {latest})", "1.0.0", "stable")
if not all(value in result.stdout for value in required):
    raise SystemExit("pack.json was not parsed as expected:\n" + result.stdout + result.stderr)
print("local pack manifest CLI: OK")
