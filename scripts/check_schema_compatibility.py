#!/usr/bin/env python3
"""Compare exported input schemas with a reviewed compatibility baseline (never rewrites it)."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ANNOTATIONS = {"description", "title", "examples", "$comment"}
BASELINE = Path(__file__).with_name("contracts") / "input-schemas.json"


def json_equal(left, right):
    # Python 的 True == 1；JSON Schema 中布尔和数字是不同值。
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(json_equal(left[k], right[k]) for k in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(json_equal(a, b) for a, b in zip(left, right))
    return left == right


def normalize(value):
    if isinstance(value, dict):
        return {key: normalize(item) for key, item in sorted(value.items()) if key not in ANNOTATIONS}
    if isinstance(value, list):
        return [normalize(item) for item in value]
    return value


def schema_errors(before, after, path):
    errors = []
    def reject(message):
        errors.append(f"{path}: {message}")
    if json_equal(before, after) or after is True:
        return errors
    if before is False:
        return errors
    if not isinstance(before, dict) or not isinstance(after, dict):
        return [f"{path}: schema narrowed or changed"]
    old_types = before.get("type")
    new_types = after.get("type")
    if new_types is not None:
        old_types = set(old_types if isinstance(old_types, list) else [old_types])
        new_types = set(new_types if isinstance(new_types, list) else [new_types])
        if "number" in new_types:
            new_types.add("integer")
        if not old_types <= new_types:
            reject("accepted type narrowed")
    for key in ("enum", "const"):
        if key in after:
            old = before.get(key)
            new = after[key]
            if key == "enum":
                if old is None or any(not any(json_equal(item, candidate) for candidate in new) for item in old):
                    reject("enum narrowed")
            elif key not in before or not json_equal(old, new):
                reject("const changed")
    added_required = set(after.get("required", [])) - set(before.get("required", []))
    if added_required:
        reject("new required fields: " + ", ".join(sorted(added_required)))
    old_props, new_props = before.get("properties", {}), after.get("properties", {})
    for name, schema in old_props.items():
        if name not in new_props:
            reject(f"field removed: {name}")
        else:
            errors += schema_errors(schema, new_props[name], f"{path}.{name}")
    # 原来允许任意额外字段时，为其增加受约束的定义也可能缩小合法输入。
    old_extra = before.get("additionalProperties", True)
    new_extra = after.get("additionalProperties", True)
    if old_extra is not False:
        errors += schema_errors(old_extra, new_extra, path + ".additionalProperties")
        for name in new_props.keys() - old_props.keys():
            errors += schema_errors(old_extra, new_props[name], f"{path}.{name}")
    if "items" in after:
        errors += schema_errors(before.get("items", True), after["items"], path + "[]")
    for key in ("minimum", "exclusiveMinimum", "minLength", "minItems", "minProperties"):
        if key in after and (key not in before or after[key] > before[key]):
            reject(f"{key} increased")
    for key in ("maximum", "exclusiveMaximum", "maxLength", "maxItems", "maxProperties"):
        if key in after and (key not in before or after[key] < before[key]):
            reject(f"{key} decreased")
    handled = ANNOTATIONS | {"type", "enum", "const", "required", "properties", "additionalProperties", "items",
        "minimum", "exclusiveMinimum", "minLength", "minItems", "minProperties", "maximum", "exclusiveMaximum",
        "maxLength", "maxItems", "maxProperties"}
    # 未实现包含关系证明的组合约束保守拒绝变化，避免误报兼容。
    for key in (before.keys() | after.keys()) - handled:
        if before.get(key) != after.get(key):
            reject(f"constraint/default changed: {key}")
    return errors


def compare(baseline, current, enabled=None):
    if not isinstance(baseline, dict) or not baseline or not isinstance(current, dict) or not current:
        raise ValueError("Expected nonempty capability-to-schema objects")
    names = set(baseline) if enabled is None else set(enabled)
    if enabled is not None and (not isinstance(enabled, list) or not names or any(not isinstance(n, str) for n in enabled)):
        raise ValueError("Enabled capability manifest must be a nonempty list of names")
    errors = []
    for name in sorted(names):
        if name not in current:
            errors.append(f"{name}: enabled capability removed")
        elif name in baseline:
            errors += schema_errors(normalize(baseline[name]), normalize(current[name]), name)
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("current", type=Path)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--enabled", type=Path, help="Reviewed enabled-capability manifest for a specific host/plugin set")
    args = parser.parse_args(argv)
    try:
        read = lambda path: json.loads(path.read_text(encoding="utf-8-sig"))
        errors = compare(read(args.baseline), read(args.current), read(args.enabled) if args.enabled else None)
    except (OSError, ValueError, TypeError) as exc:
        print(f"Invalid schema input: {exc}")
        return 2
    for error in errors:
        print(error)
    print(f"Schema compatibility: {len(errors)} breaking changes")
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
