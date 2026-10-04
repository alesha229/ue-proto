"""Stamp a build from exact source/config/asset bytes; no game-time Python dependency."""
import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "GratiaVR"


def fingerprint(paths):
    digest = hashlib.sha256()
    count = 0
    for path in sorted(paths, key=lambda p: p.as_posix().lower()):
        if not path.is_file() or "Generated" in path.parts or "__pycache__" in path.parts:
            continue
        relative = path.relative_to(ROOT).as_posix()
        digest.update(relative.encode("utf-8") + b"\0")
        with path.open("rb") as file:
            file_digest = hashlib.file_digest(file, "sha256").digest()
        digest.update(file_digest)
        count += 1
    return digest.hexdigest(), count


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    source_paths = [p for folder in ("Source", "Config", "Scripts") for p in (PROJECT / folder).rglob("*")]
    source_paths.append(PROJECT / "GratiaVR.uproject")
    source_hash, source_count = fingerprint(source_paths)
    asset_hash, asset_count = fingerprint((PROJECT / "Content").rglob("*"))
    output = Path(args.output)
    if args.verify:
        previous = json.loads(output.read_text(encoding="utf-8-sig"))
        if (source_hash, asset_hash) != (previous["source_sha256"], previous["assets_sha256"]):
            raise SystemExit("Source or assets changed during build; rebuild with a new stamp")
        print("BUILD_FINGERPRINT_VERIFIED " + previous["build_id"])
        return
    engine = json.loads((Path(args.engine) / "Engine/Build/Build.version").read_text(encoding="utf-8-sig"))
    engine_version = ".".join(str(engine[k]) for k in ("MajorVersion", "MinorVersion", "PatchVersion"))
    now = datetime.now(timezone.utc)
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    build_id = f"gratia-{now:%Y%m%dT%H%M%SZ}-{source_hash[:8]}-{asset_hash[:8]}"
    manifest = dict(schema=1, build_id=build_id, stamped_utc=now.isoformat(), git_commit=commit, git_dirty=dirty,
                    engine_version=engine_version, source_sha256=source_hash, assets_sha256=asset_hash,
                    source_file_count=source_count, asset_file_count=asset_count)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    header = PROJECT / "Source/GratiaVR/Generated/GratiaBuildStamp.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('#pragma once\n#define GRATIA_BUILD_ID ' + json.dumps(build_id) + '\n#define GRATIA_BUILD_COMMIT ' + json.dumps(commit[:12]) + '\n', encoding="ascii")
    print("BUILD_STAMPED " + build_id)


if __name__ == "__main__":
    main()
