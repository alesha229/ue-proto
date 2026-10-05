"""Read-only VaM Timeline extractor; developer preparation, never runtime code.

Writes raw controller/scalar curves in neutral gzip JSON plus small metadata and
samples. It does not load Blender, solve VaM physics, or produce a bone animation.
Usage: python extract_vam_timeline.py PACKAGE_DIR --output OUTPUT_DIR
The optimized decoder is specified by Timeline's public SerializeVersion 283.
"""

from __future__ import annotations

import argparse
import bisect
from collections import Counter
from datetime import datetime, timezone
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Any


SCHEMA = "gratia.vam_timeline_raw.v1"
CHANNELS = ("X", "Y", "Z", "RotX", "RotY", "RotZ", "RotW")
REFERENCES = {
    "serialization": "https://github.com/acidbubbles/vam-timeline/blob/master/src/AtomAnimations/Serialization/AtomAnimationSerializer.cs",
    "controller_space": "https://github.com/acidbubbles/vam-timeline/blob/master/src/AtomAnimations/Animatables/FreeControllerV3s/FreeControllerV3AnimationTarget.cs",
    "interpolation": "https://github.com/acidbubbles/vam-timeline/blob/master/src/AtomAnimations/BezierCurves/BezierAnimationCurve.cs",
    "curve_types": "https://github.com/acidbubbles/vam-timeline/blob/master/src/AtomAnimations/BezierCurves/CurveTypeValues.cs",
}
LIMITATIONS = [
    "Controller transforms are targets, not evaluated VaM skeleton bone transforms; VaM physics/IK/soft bodies/cloth are not reproduced.",
    "Without an explicit Timeline Parent, values are controller.transform.localPosition/localRotation. The implicit Unity parent hierarchy is unavailable from this JSON alone; no world-space claim is made.",
    "Explicit Parent values, if present, are parent-rigidbody-relative. Extracted metadata preserves that reference; this tool does not resolve it.",
    "No coordinate/unit/axis conversion or anatomical controller-to-bone orientation calibration is applied. Rotations are raw quaternion components in x,y,z,w order.",
    "Atom/container transforms and worldScale are preserved separately; this tool does not assume that worldScale modifies animation targets.",
    "Scalar finger controls and Genesis 2 female facial morph names require explicit adaptation to the target rig/morphs. Missing target capabilities cannot be treated as success.",
    "Samples use decoded keys and Timeline local Bezier interpolation in Python double precision. Unity float32 intermediate rounding and runtime quaternion normalization are not reproduced.",
    "Curve type 0 (external custom handles) and type 10 (global spline solve) are preserved but cannot be sampled by this extractor.",
    "Trigger actions, plugin behavior, source materials, clothing, audio playback, and dependencies are inventory only and are not executed or converted.",
    "A successful extraction is not evaluated-model, exported-FBX, Unreal-package, or real-VR validation.",
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def decode_curve(encoded_keys: list[Any], version: int) -> dict[str, list]:
    """Decode fields losslessly; reject corruption instead of guessing a repair."""
    if version != 283:
        raise ValueError(f"Only verified serialization version 283 is supported, got {version}")
    times, values, curve_types = [], [], []
    prior_value, prior_curve = 0.0, 3
    for key_index, encoded in enumerate(encoded_keys):
        if not isinstance(encoded, str) or not encoded or encoded[0] not in "ABCD":
            raise ValueError(f"Invalid optimized key {key_index}: expected A-D prefix")
        flags = ord(encoded[0]) - ord("A")
        expected_length = 9 + (8 if flags & 1 else 0) + (2 if flags & 2 else 0)
        if len(encoded) != expected_length:
            raise ValueError(f"Invalid optimized key length at index {key_index}")
        try:
            time = struct.unpack("<f", bytes.fromhex(encoded[1:9]))[0]
            value = struct.unpack("<f", bytes.fromhex(encoded[9:17]))[0] if flags & 1 else prior_value
            curve_type = int(encoded[-2:], 16) if flags & 2 else prior_curve
        except (ValueError, struct.error) as error:
            raise ValueError(f"Invalid optimized key bytes at index {key_index}") from error
        if not math.isfinite(time) or not math.isfinite(value) or time < 0:
            raise ValueError(f"Nonfinite/negative-time key at index {key_index}")
        if times and time == times[-1]:
            # Timeline ignores identical-time keys before updating inherited state.
            continue
        if times and time < times[-1]:
            raise ValueError(f"Unordered key at index {key_index}")
        times.append(time)
        values.append(value)
        curve_types.append(curve_type)
        prior_value, prior_curve = value, curve_type
    return {"time_seconds": times, "value": values, "curve_type": curve_types}


def handles(curve: dict[str, list], index: int) -> tuple[float, float]:
    """Compute local Bezier scalar handles; source algorithm uses each key's type."""
    times, values, types = curve["time_seconds"], curve["value"], curve["curve_type"]
    value, kind = values[index], types[index]
    if len(times) <= 2:
        # Timeline's two-key special case initializes handles without CopyPrevious.
        return value, value
    if kind == 7:
        if not index:
            raise ValueError("CopyPrevious on the first key has no previous key")
        if value != values[index - 1]:
            raise ValueError("CopyPrevious requires runtime value mutation; raw values differ")
        kind = 3 if types[index - 1] == 7 else types[index - 1]
    before, after = index > 0, index + 1 < len(times)
    incoming = (value - values[index - 1]) / 3 if before else 0.0
    outgoing = (values[index + 1] - value) / 3 if after else 0.0
    if kind == 3:
        if before and after:
            span = times[index + 1] - times[index - 1]
            in_ratio = (times[index] - times[index - 1]) / span
            out_ratio = (times[index + 1] - times[index]) / span
            average = incoming * in_ratio + outgoing * out_ratio
            incoming = average * min(1.0, in_ratio / out_ratio)
            outgoing = average * min(1.0, out_ratio / in_ratio)
        return value - incoming, value + outgoing
    if kind == 2:
        return value - incoming, value + outgoing
    if kind == 5:
        return value - incoming, value
    if kind in (1, 6, 8, 9):
        return value, value
    if kind == 4:
        if before and after:
            return value - (value - values[index + 1]) / 1.4, value + (values[index - 1] - value) / 1.8
        return value, value
    raise ValueError(f"Sampling curve type {kind} is unsupported; raw data is preserved")


def sample_curve(curve: dict[str, list], time: float) -> dict[str, Any]:
    times, values, types = curve["time_seconds"], curve["value"], curve["curve_type"]
    if not times:
        return {"status": "empty"}
    nearest = bisect.bisect_left(times, time)
    if nearest < len(times) and times[nearest] == time:
        return {"status": "exact_key", "value": values[nearest], "key_index": nearest}
    if nearest == 0 or nearest == len(times):
        index = 0 if nearest == 0 else len(times) - 1
        return {"status": "held_outside_key_range", "value": values[index], "key_index": index}
    left, right = nearest - 1, nearest
    factor = (time - times[left]) / (times[right] - times[left])
    if types[left] == 8:
        value = values[left]
    elif types[left] in (2, 6):
        value = values[left] + factor * (values[right] - values[left])
    else:
        try:
            out_handle = handles(curve, left)[1]
            in_handle = handles(curve, right)[0]
        except ValueError as error:
            return {"status": "unsupported_interpolation", "reason": str(error), "key_indices": [left, right]}
        inverse = 1.0 - factor
        value = inverse**3 * values[left] + 3 * inverse**2 * factor * out_handle + 3 * inverse * factor**2 * in_handle + factor**3 * values[right]
    return {"status": "interpolated_local_curve", "value": value, "key_indices": [left, right], "bracket_seconds": [times[left], times[right]]}


def track_stats(curve: dict[str, list], original_count: int) -> dict[str, Any]:
    times, values = curve["time_seconds"], curve["value"]
    deltas = sorted(b - a for a, b in zip(times, times[1:]))
    return {
        "serialized_key_count": original_count,
        "decoded_key_count": len(times),
        "duplicate_time_keys_ignored": original_count - len(times),
        "time_range_seconds": [times[0], times[-1]] if times else None,
        "value_range": [min(values), max(values)] if values else None,
        "curve_types": dict(Counter(curve["curve_type"])),
        "median_interval_seconds": deltas[len(deltas) // 2] if deltas else None,
        "first_key": {name: column[0] for name, column in curve.items()} if times else None,
        "last_key": {name: column[-1] for name, column in curve.items()} if times else None,
    }


def static_atom_metadata(atom: dict[str, Any], controller_names: list[str]) -> dict[str, Any]:
    wanted = {"control", *controller_names}
    return {
        "atom": {key: value for key, value in atom.items() if key != "storables"},
        "controller_storables": [storable for storable in atom.get("storables", []) if storable.get("id") in wanted],
    }


def extract(package: Path, output: Path, sample_times: list[float], atom_id: str, scene_path: Path | None = None) -> dict[str, Any]:
    package, output = package.resolve(), output.resolve()
    if output == package or package in output.parents:
        raise ValueError("Derived output must be outside the source package")
    scene_paths = [scene_path.resolve()] if scene_path else list((package / "Saves" / "scene").rglob("*.json"))
    if len(scene_paths) != 1:
        raise ValueError("Select --scene explicitly when package does not contain exactly one scene JSON")
    scene_path = scene_paths[0]
    source_hash = sha256(scene_path)
    with scene_path.open(encoding="utf-8-sig") as stream:
        scene = json.load(stream)
    with (package / "meta.json").open(encoding="utf-8-sig") as stream:
        package_meta = json.load(stream)
    candidates = [atom for atom in scene.get("atoms", []) if atom.get("id") == atom_id]
    if len(candidates) != 1:
        raise ValueError(f"Expected one explicitly selected atom {atom_id!r}")
    atom = candidates[0]
    timelines = [storable for storable in atom.get("storables", []) if isinstance(storable.get("Animation"), dict)]
    if len(timelines) != 1:
        raise ValueError("Expected exactly one Timeline animation on selected atom")
    timeline = timelines[0]
    animation = timeline["Animation"]
    version = int(animation.get("SerializeVersion", 0))
    if int(animation.get("SerializeMode", -1)) != 2:
        raise ValueError("Extractor supports verified optimized SerializeMode 2 only")
    controller_names = [controller["Controller"] for clip in animation["Clips"] for controller in clip.get("Controllers", [])]
    metadata = {
        "schema": SCHEMA,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source": {"path": str(scene_path), "sha256": source_hash, "size_bytes": scene_path.stat().st_size},
        "package": {key: package_meta.get(key) for key in ("creatorName", "packageName", "programVersion", "licenseType")},
        "dependency_names": list(package_meta.get("dependencies", {})),
        "source_inventory": [{"path": str(path.relative_to(package)), "size_bytes": path.stat().st_size} for path in sorted(package.rglob("*")) if path.is_file()],
        "scene_fields": {key: value for key, value in scene.items() if key != "atoms"},
        "selected_atom": static_atom_metadata(atom, controller_names),
        "timeline_storable_id": timeline.get("id"),
        "animation_fields": {key: value for key, value in animation.items() if key != "Clips"},
        "clips": [],
        "track_metadata": [],
        "references": REFERENCES,
        "limitations": LIMITATIONS,
    }
    samples = {"schema": SCHEMA, "source_sha256": source_hash, "times_seconds": sample_times, "clips": []}
    output.mkdir(parents=True, exist_ok=True)
    tracks_path = output / "source_tracks.json.gz"
    temporary = tracks_path.with_suffix(".gz.tmp")
    total_keys, total_tracks = 0, 0
    with temporary.open("wb") as raw_stream:
        with gzip.GzipFile(filename="", fileobj=raw_stream, mode="wb", compresslevel=6, mtime=0) as compressed:
            def write(value: str) -> None:
                compressed.write(value.encode("utf-8"))

            write('{"schema":' + json.dumps(SCHEMA) + ',"source_sha256":' + json.dumps(source_hash) + ',"tracks":[')
            first = True
            for clip_index, clip in enumerate(animation["Clips"]):
                clip_meta = {key: value for key, value in clip.items() if key not in ("Controllers", "FloatParams", "Triggers")}
                clip_meta["clip_index"] = clip_index
                clip_meta["controller_count"] = len(clip.get("Controllers", []))
                clip_meta["float_parameter_count"] = len(clip.get("FloatParams", []))
                clip_meta["triggers_inventory"] = clip.get("Triggers", [])
                metadata["clips"].append(clip_meta)
                clip_samples = {"clip_index": clip_index, "layer": clip.get("AnimationLayer"), "samples": [{"time_seconds": time, "controllers": {}, "float_parameters": {}} for time in sample_times]}
                samples["clips"].append(clip_samples)
                for kind, targets in (("controller", clip.get("Controllers", [])), ("float_parameter", clip.get("FloatParams", []))):
                    for target_index, target in enumerate(targets):
                        channels = CHANNELS if kind == "controller" else ("Value",)
                        target_ref = {key: value for key, value in target.items() if key not in channels}
                        for channel in channels:
                            if channel not in target:
                                raise ValueError(f"Missing {channel} in clip {clip_index} {kind} {target_index}")
                            curve = decode_curve(target[channel], version)
                            track_id = f"clip{clip_index}/{kind}{target_index}/{channel}"
                            record = {"id": track_id, "clip_index": clip_index, "kind": kind, "target": target_ref, "channel": channel, "keys": curve}
                            if not first:
                                write(",")
                            first = False
                            write(json.dumps(record, separators=(",", ":"), allow_nan=False))
                            metadata["track_metadata"].append({key: value for key, value in record.items() if key != "keys"} | track_stats(curve, len(target[channel])))
                            total_keys += len(curve["time_seconds"])
                            total_tracks += 1
                            for sample in clip_samples["samples"]:
                                result = sample_curve(curve, sample["time_seconds"])
                                if kind == "controller":
                                    controller = sample["controllers"].setdefault(target["Controller"], {"target": target_ref, "channels": {}})
                                    controller["channels"][channel] = result
                                else:
                                    name = target["Storable"] + "/" + target["Name"]
                                    sample["float_parameters"][name] = result
                for sample in clip_samples["samples"]:
                    for controller in sample["controllers"].values():
                        channel_values = controller["channels"]
                        if all("value" in channel_values[name] for name in CHANNELS):
                            controller["position_xyz"] = [channel_values[name]["value"] for name in CHANNELS[:3]]
                            rotation = [channel_values[name]["value"] for name in CHANNELS[3:]]
                            controller["quaternion_xyzw_raw"] = rotation
                            controller["quaternion_raw_norm"] = math.sqrt(sum(value * value for value in rotation))
            write("]}")
    if sha256(scene_path) != source_hash:
        temporary.unlink()
        raise RuntimeError("Source changed during extraction; derived tracks discarded")
    temporary.replace(tracks_path)
    metadata["summary"] = {"track_count": total_tracks, "decoded_scalar_key_count": total_keys}
    metadata["derived_tracks"] = {"path": str(tracks_path), "sha256": sha256(tracks_path), "size_bytes": tracks_path.stat().st_size}
    for name, data in (("source_metadata.json", metadata), ("source_samples.json", samples)):
        (output / name).write_text(json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    return metadata


def create_trial_samples(output: Path, duration: float, rate: int) -> dict[str, Any]:
    """Resample preserved controller targets, scalar fingers and face curves."""
    metadata = json.loads((output / "source_metadata.json").read_text(encoding="utf-8"))
    if sha256(Path(metadata["source"]["path"])) != metadata["source"]["sha256"]:
        raise RuntimeError("Source fingerprint differs from the extraction manifest")
    if sha256(output / "source_tracks.json.gz") != metadata["derived_tracks"]["sha256"]:
        raise RuntimeError("Derived track fingerprint differs from the extraction manifest")
    if any(duration > float(clip["AnimationLength"]) for clip in metadata["clips"]):
        raise ValueError("Trial duration exceeds a source clip")
    count = round(duration * rate)
    if count / rate != duration:
        raise ValueError("Trial duration must lie on the requested sample grid")
    frames = [{"time_seconds": index / rate, "controllers": {}, "float_parameters": {}, "interpolation_failures": []} for index in range(count + 1)]
    statuses, failures = Counter(), []
    with gzip.open(output / "source_tracks.json.gz", "rt", encoding="utf-8") as stream:
        tracks = json.load(stream)["tracks"]
    for track in tracks:
        target, channel, curve = track["target"], track["channel"], track["keys"]
        for frame in frames:
            sample = sample_curve(curve, frame["time_seconds"])
            statuses[sample["status"]] += 1
            value = sample.get("value")
            if value is None:
                failure = {"track": track["id"], "time_seconds": frame["time_seconds"], "status": sample["status"], "reason": sample.get("reason")}
                frame["interpolation_failures"].append(failure)
                failures.append(failure)
            if track["kind"] == "controller":
                controller = frame["controllers"].setdefault(target["Controller"], {"position_xyz": [None] * 3, "quaternion_xyzw": [None] * 4})
                if channel in CHANNELS[:3]:
                    controller["position_xyz"][CHANNELS[:3].index(channel)] = value
                else:
                    controller["quaternion_xyzw"][CHANNELS[3:].index(channel)] = value
            else:
                frame["float_parameters"][target["Storable"] + "/" + target["Name"]] = value
    previous, minimum_norm, maximum_norm, sign_flips = {}, math.inf, 0.0, 0
    for frame in frames:
        for name, controller in frame["controllers"].items():
            quaternion = controller["quaternion_xyzw"]
            if any(value is None for value in quaternion):
                continue
            norm = math.sqrt(sum(value * value for value in quaternion))
            if not math.isfinite(norm) or norm < 1e-8:
                raise ValueError(f"Invalid quaternion norm for {name} at {frame['time_seconds']}")
            minimum_norm, maximum_norm = min(minimum_norm, norm), max(maximum_norm, norm)
            normalized = [value / norm for value in quaternion]
            if name in previous and sum(a * b for a, b in zip(previous[name], normalized)) < 0:
                normalized = [-value for value in normalized]
                sign_flips += 1
            controller["quaternion_xyzw"] = normalized
            previous[name] = normalized
    trial = {
        "schema": "gratia.vam_controller_trial.v1",
        "source_sha256": metadata["source"]["sha256"],
        "source_track_sha256": metadata["derived_tracks"]["sha256"],
        "duration_seconds": duration,
        "sample_rate_hz": rate,
        "frame_count": len(frames),
        "target_atom": metadata["selected_atom"]["atom"]["id"],
        "coordinate_space": "VaM controller local transforms; no axis/unit/parent conversion applied",
        "quaternion_convention": "x,y,z,w; unit normalized and sign continuous per controller",
        "source_quaternion_norm_range": [minimum_norm, maximum_norm],
        "quaternion_sign_flips": sign_flips,
        "interpolation_status": {"counts": dict(statuses), "failure_count": len(failures), "failures": failures},
        "limitations": LIMITATIONS,
        "frames": frames,
    }
    trial_path = output / f"trial{duration:g}s_samples.json"
    trial_path.write_text(json.dumps(trial, separators=(",", ":"), ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    return {"path": str(trial_path.resolve()), "sha256": sha256(trial_path), "frame_count": len(frames), "failure_count": len(failures), "source_quaternion_norm_range": [minimum_norm, maximum_norm]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scene", type=Path)
    parser.add_argument("--atom", default="Girl", help="Explicit source atom ID")
    parser.add_argument("--samples", nargs="+", type=float, default=[0.0, 1.0, 10.0])
    parser.add_argument("--trial-duration", type=float, help="Write a short controller/scalar trial on an exact sample grid")
    parser.add_argument("--trial-rate", type=int, default=60)
    parser.add_argument("--trial-only", action="store_true", help="Reuse fingerprinted raw extraction")
    args = parser.parse_args()
    if any(not math.isfinite(time) or time < 0 for time in args.samples):
        parser.error("Sample times must be finite and nonnegative")
    if not args.trial_only:
        metadata = extract(args.package, args.output, args.samples, args.atom, args.scene)
        print(json.dumps({"source_sha256": metadata["source"]["sha256"], **metadata["summary"], "output": str(args.output.resolve())}, indent=2))
    if args.trial_duration is not None:
        if not math.isfinite(args.trial_duration) or args.trial_duration <= 0 or args.trial_rate <= 0:
            parser.error("Trial duration and sample rate must be positive and finite")
        print(json.dumps(create_trial_samples(args.output, args.trial_duration, args.trial_rate), indent=2))
    elif args.trial_only:
        parser.error("--trial-only requires --trial-duration")


if __name__ == "__main__":
    main()
