"""One fixed blocked-condition, change, observe, restore workflow."""
import math
from retest.runner import RunError, direct_url


def value_string(kind, value):
    valid = ((kind == "Bool" and type(value) is bool)
             or (kind in ("Int", "Enum") and type(value) is int and (0 <= value <= 255 if kind == "Enum" else -(2**31) <= value < 2**31))
             or (kind == "Float" and type(value) in (float, int) and math.isfinite(value) and abs(value) <= 3.4028234e38)
             or (kind in ("Name", "String") and isinstance(value, str)))
    if not valid: raise RunError("Blackboard value does not match declared scalar Type: " + str(kind))
    return str(value).lower() if kind == "Bool" else str(value)


def validate_expected(expected):
    if not isinstance(expected, dict) or not expected or set(expected) - {"Node", "Blackboard"}:
        raise RunError("Expected state requires Node and/or Blackboard")
    if "Node" in expected and (not isinstance(expected["Node"], str) or not expected["Node"]):
        raise RunError("Expected Node must be nonempty text")
    if "Blackboard" in expected:
        entry = expected["Blackboard"]
        if not isinstance(entry, dict) or set(entry) != {"Key", "Type", "Value"} or not isinstance(entry["Key"], str) or not entry["Key"]:
            raise RunError("Expected Blackboard requires Key, Type and Value")
        value_string(entry["Type"], entry["Value"])


def blackboard_condition(config):
    fields = {"Name", "Endpoint", "ActorName", "TreePath", "Condition", "Expected", "Before", "Timeout", "Interval"}
    if not isinstance(config, dict) or set(config) - fields or not {"Name", "Endpoint", "ActorName", "TreePath", "Condition", "Expected"} <= config.keys():
        raise RunError("BlackboardCondition requires Name, Endpoint, ActorName, TreePath, Condition and Expected")
    for field in ("Name", "ActorName", "TreePath"):
        if not isinstance(config[field], str) or not config[field]: raise RunError(field + " must be nonempty")
    if not config["TreePath"].startswith("/"): raise RunError("TreePath must be the full observed tree path")
    endpoint = config["Endpoint"]
    if not isinstance(endpoint, dict) or set(endpoint) - {"Url", "TokenEnv", "bAuthority"}: raise RunError("Invalid Endpoint")
    endpoint = {**endpoint, "Url": direct_url(endpoint.get("Url")), "bAuthority": True}
    if config["Endpoint"].get("bAuthority", True) is not True: raise RunError("Blackboard replay requires authority")
    condition = config["Condition"]
    if not isinstance(condition, dict) or set(condition) != {"Key", "Type", "BlockedValue", "ChangedValue"} or not isinstance(condition["Key"], str) or not condition["Key"]:
        raise RunError("Condition requires Key, Type, BlockedValue and ChangedValue")
    blocked = value_string(condition["Type"], condition["BlockedValue"])
    changed = value_string(condition["Type"], condition["ChangedValue"])
    if blocked == changed: raise RunError("BlockedValue and ChangedValue must differ")
    validate_expected(config["Expected"])
    initial = {"Blackboard": {"Key": condition["Key"], "Type": condition["Type"], "Value": condition["BlockedValue"]}}
    before = config.get("Before", initial); validate_expected(before)
    actor = {"Ref": "/Actors/Target/UE"}
    def call(name, value):
        return {"Id": name, "Type": "Call", "Endpoint": "UE", "Capability": "InteractRuntimeActorBehaviorTree",
                "Arguments": {"ActorName": actor, "Action": "SetBlackboard", "KeyName": condition["Key"], "Value": value}}
    def wait(name, expected):
        return {"Id": name, "Type": "Await", "Endpoint": "UE", "Capability": "GetRuntimeActorBehaviorTree",
                "Arguments": {"ActorName": actor}, "Timeout": config.get("Timeout", 10), "Interval": config.get("Interval", .1),
                "ExpectedState": expected, "Check": {"Path": "/Payload/MatchesExpected", "Value": True}}
    restored = {"Blackboard": {"Key": condition["Key"], "Type": condition["Type"], "Value": {"Ref": "/InitialValue"}}}
    return {"SchemaVersion": 1, "Domain": "BT", "Name": config["Name"], "Endpoints": {"UE": endpoint},
            "Actors": {"Target": {"Bindings": {"UE": config["ActorName"]}}}, "TreePath": config["TreePath"], "Condition": condition,
            "Steps": [call("block", blocked), wait("blocked", before), call("change", changed), wait("expected", config["Expected"])],
            "Teardown": [call("restore", {"Ref": "/RestoreValue"}), wait("restored", restored)]}
