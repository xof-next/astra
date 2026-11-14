#!/usr/bin/env python3
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACK = ROOT / "pack.json"
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


def main() -> int:
    try:
        data = json.loads(PACK.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"pack.json: {exc}", file=sys.stderr)
        return 1
    if data.get("name") != "astra":
        print("pack.json: name must be 'astra'", file=sys.stderr)
        return 1
    latest = data.get("new")
    versions = data.get("versions")
    if not isinstance(latest, str) or not SEMVER.fullmatch(latest):
        print("pack.json: 'new' must be a semantic version", file=sys.stderr)
        return 1
    if not isinstance(versions, dict) or latest not in versions:
        print("pack.json: 'new' must exist in 'versions'", file=sys.stderr)
        return 1
    for version, metadata in versions.items():
        if not SEMVER.fullmatch(version) or not isinstance(metadata, dict):
            print(f"pack.json: invalid version entry: {version!r}", file=sys.stderr)
            return 1
        path = metadata.get("path")
        if not isinstance(path, str) or not path or pathlib.PurePosixPath(path).is_absolute() or ".." in pathlib.PurePosixPath(path).parts:
            print(f"pack.json: unsafe or missing source path for {version}", file=sys.stderr)
            return 1
        source = (ROOT / path).resolve()
        if ROOT.resolve() not in source.parents or not (source / "CMakeLists.txt").is_file():
            print(f"pack.json: source directory missing for {version}: {path}", file=sys.stderr)
            return 1
        tag = metadata.get("tag")
        if not isinstance(tag, str) or not re.fullmatch(r"v?" + re.escape(version), tag):
            print(f"pack.json: tag must match v{version} for {version}", file=sys.stderr)
            return 1
        if metadata.get("status") not in {"stable", "preview", "deprecated", "yanked"}:
            print(f"pack.json: invalid status for {version}", file=sys.stderr)
            return 1
        assets = metadata.get("assets")
        expected_platforms = {
            "linux-x86_64", "linux-aarch64", "macos-x86_64", "macos-aarch64", "windows-x86_64", "windows-aarch64"
        }
        if version == latest:
            expected_platforms.add("android-aarch64")
        if not isinstance(assets, dict) or set(assets) != expected_platforms or any(
            not isinstance(asset, str) or not asset or "/" in asset or "\\" in asset for asset in assets.values()
        ):
            print(f"pack.json: platform asset matrix incomplete or unsafe for {version}", file=sys.stderr)
            return 1
        cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
        if not re.search(r"project\(Astra VERSION " + re.escape(version) + r"(?:\s|$)", cmake):
            print(f"pack.json: CMake project version does not match {version}", file=sys.stderr)
            return 1
    print(f"pack.json: OK ({len(versions)} version(s), new={latest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
