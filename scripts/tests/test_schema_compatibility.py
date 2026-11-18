import copy
import importlib.util
import json
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("compat", Path(__file__).parents[1] / "check_schema_compatibility.py")
compat = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compat)


def test_existing_exports_remain_compatible():
    root = Path(__file__).parents[1]
    assert compat.compare(json.loads(compat.BASELINE.read_text(encoding="utf-8")),
                          json.loads((root / "generated/capability_schemas.json").read_text(encoding="utf-8-sig"))) == []


def test_released_navigation_contract_detects_removal_and_narrowing():
    baseline = json.loads(compat.BASELINE.read_text(encoding="utf-8"))
    current = json.loads((Path(__file__).parents[1] / "generated/capability_schemas.json").read_text(encoding="utf-8-sig"))
    narrowed = copy.deepcopy(current)
    narrowed["GetNavigationPath"]["properties"]["World"]["enum"].remove("Editor")
    assert "GetNavigationPath.World: enum narrowed" in compat.compare(baseline, narrowed)
    del current["GetNavigationPath"]
    assert "GetNavigationPath: enabled capability removed" in compat.compare(baseline, current)


def test_release_checks_new_exports_and_capabilities_missing_from_the_export():
    baseline = {"Old": {}}
    current = {"Old": {}, "New": {}}
    assert compat.compare(baseline, current) == []
    public = {"Old", "New", "Unexported", "GetAssetGeometryCollection"}
    assert compat.release_coverage_errors(baseline, current, public) == [
        "New: release export has no reviewed baseline", "Unexported: public capability missing from release export"]
    assert compat.release_coverage_errors(current, current, public - {"Unexported"}) == []


def test_additions_and_breaking_changes():
    before = {"Read": {"type": "object", "additionalProperties": False, "properties": {
        "Target": {"type": "string"}, "Values": {"type": "array", "items": {
            "type": "integer", "enum": [1, 2], "minimum": 1}}}, "required": ["Target"]}}
    after = copy.deepcopy(before)
    after["Read"]["properties"]["Optional"] = {"type": "boolean"}
    after["Read"]["properties"]["Values"]["items"].update(type="number", enum=[1, 2, 3], minimum=0)
    assert compat.compare(before, after) == []
    for mutation in (lambda s: s["properties"].pop("Target"),
                     lambda s: s["required"].append("Values"),
                     lambda s: s["properties"]["Values"]["items"].update(enum=[1]),
                     lambda s: s["properties"]["Target"].update(type="number"),
                     lambda s: s["properties"]["Values"]["items"].update(minimum=2)):
        changed = copy.deepcopy(before)
        mutation(changed["Read"])
        assert compat.compare(before, changed)
    assert compat.compare({**before, "OptionalPlugin": {}}, before, ["Read"]) == []
    assert compat.compare({**before, "OptionalPlugin": {}}, before)


def test_open_objects_cannot_be_silently_restricted():
    before = {"Read": {"type": "object"}}
    assert compat.compare(before, {"Read": {"type": "object", "properties": {"X": {"type": "integer"}}}})
    assert compat.compare(before, {"Read": {"type": "object", "additionalProperties": False}})


def test_boolean_is_not_a_numeric_enum_value():
    assert compat.schema_errors({"enum": [True]}, {"enum": [1]}, "Input")
    assert compat.schema_errors({"const": False}, {"const": 0}, "Input")
    assert not compat.schema_errors({"enum": [1]}, {"enum": [1.0, 2]}, "Input")
