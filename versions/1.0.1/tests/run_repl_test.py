import pathlib
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
source = "\n".join([
    'let label = "Astra";',
    'label',
    '2 + 3',
    'sync fn twice(value) {',
    '    return value * 2;',
    '}',
    'twice(6)',
    '.help',
    '.exit',
    ''
])
result = subprocess.run([str(binary)], input=source, capture_output=True, text=True, timeout=10)
if result.returncode != 0:
    raise SystemExit("REPL exited with an error:\n" + result.stdout + result.stderr)
expected = ["Welcome to Astra v1.0.1.", "Type \".help\" for more information.", "Astra", "5", "12", ".exit"]
for value in expected[:-1]:
    if value not in result.stdout:
        raise SystemExit(f"REPL output did not contain {value!r}:\n{result.stdout}\n{result.stderr}")
print("interactive REPL: OK")
