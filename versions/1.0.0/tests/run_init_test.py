import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="astra-init-") as temp:
    project = pathlib.Path(temp) / "demo"
    result = subprocess.run([str(binary), "init", str(project)], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout)
    files = sorted(p.name for p in project.iterdir())
    if files != ["Astra.toml", "lib", "main.astra", "test.astra"]:
        raise SystemExit(f"unexpected project files: {files}")
    run = subprocess.run([str(binary), "run"], cwd=project, capture_output=True, text=True)
    if run.returncode != 0 or "Hello from Astra!" not in run.stdout:
        raise SystemExit(f"fresh project did not run: {run.stdout}\n{run.stderr}")
    test = subprocess.run([str(binary), "test"], cwd=project, capture_output=True, text=True)
    if test.returncode != 0 or "Tests passed" not in test.stdout:
        raise SystemExit(f"devDependencies task did not run: {test.stdout}\n{test.stderr}")
    test_ext = subprocess.run([str(binary), "test.astra"], cwd=project, capture_output=True, text=True)
    if test_ext.returncode != 0 or "test.astra executed" not in test_ext.stdout:
        raise SystemExit(f"direct source shorthand did not run: {test_ext.stdout}\n{test_ext.stderr}")
print("minimal project init: OK")
