import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="astra-cli-") as temp:
    project = pathlib.Path(temp)
    (project / "Astra.toml").write_text('''[project]
name = "cli-test"
version = "1.0.0"
entry = "main.astra"

[dependencies]

[devDependencies]

[scripts]
dev = "dev.astra"
''', encoding="utf-8")
    (project / "main.astra").write_text('console.log("project entry");\n', encoding="utf-8")
    (project / "dev.astra").write_text('console.log("dev script");\n', encoding="utf-8")

    commands = [
        (["run"], "project entry"),
        (["run", "dev"], "dev script"),
        (["r", "dev"], "dev script"),
        (["file", "dev.astra"], "dev script"),
        (["dev.astra"], "dev script"),
    ]
    for arguments, expected in commands:
        result = subprocess.run([str(binary), *arguments], cwd=project, capture_output=True, text=True, timeout=10)
        if result.returncode != 0 or expected not in result.stdout:
            raise SystemExit(f"command {arguments!r} failed:\n{result.stdout}\n{result.stderr}")

    check = subprocess.run([str(binary), "c", "dev.astra"], cwd=project, capture_output=True, text=True, timeout=10)
    if check.returncode != 0:
        raise SystemExit("check alias failed:\n" + check.stdout + check.stderr)
print("CLI project scripts, file runner, and aliases: OK")
