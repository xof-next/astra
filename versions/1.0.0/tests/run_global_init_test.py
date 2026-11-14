import os
import pathlib
import subprocess
import sys
import tempfile

if os.name == "nt":
    print("global init install-path test: skipped on Windows")
    raise SystemExit(0)
binary = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix="astra-global-") as temp:
    home = pathlib.Path(temp) / "home"
    work = pathlib.Path(temp) / "outside-repo"
    home.mkdir(); work.mkdir()
    env = os.environ.copy(); env["HOME"] = str(home)
    result = subprocess.run([str(binary), "init"], cwd=repo, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit("global init failed:\n" + result.stdout + result.stderr)
    installed = home / ".local" / "bin" / "astra"
    if not installed.is_file() or not os.access(installed, os.X_OK):
        raise SystemExit("global init did not create an executable in ~/.local/bin")
    stored = home / ".astra" / "versions" / "1.0.0" / "astra"
    if not stored.is_file() or not os.access(stored, os.X_OK):
        raise SystemExit("global init did not preserve the versioned runtime")
    env["PATH"] = str(installed.parent) + os.pathsep + env.get("PATH", "")
    check = subprocess.run(["astra", "--version"], cwd=work, env=env, capture_output=True, text=True)
    if check.returncode != 0 or "Astra Script 1.0.0" not in check.stdout:
        raise SystemExit("global astra command is not usable outside repository:\n" + check.stdout + check.stderr)
    use = subprocess.run(["astra", "use", "1.0.0"], cwd=work, env=env, capture_output=True, text=True)
    if use.returncode != 0 or "Now using Astra 1.0.0" not in use.stdout:
        raise SystemExit("version activation failed:\n" + use.stdout + use.stderr)
    after = subprocess.run(["astra", "--version"], cwd=work, env=env, capture_output=True, text=True)
    if after.returncode != 0 or "Astra Script 1.0.0" not in after.stdout:
        raise SystemExit("active runtime stopped working after astra use:\n" + after.stdout + after.stderr)
print("global init and version activation: OK")
