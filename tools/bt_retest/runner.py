"""Fixed Blackboard-condition replay with explicit targets and snapshot evidence."""
import argparse
import copy
import json
from pathlib import Path
import sys
from retest.runner import BaseRunner, Client, RunError, CheckFailed, check, resolve, write_reports
from .template import blackboard_condition, value_string


def blackboard_value(payload, key, kind):
    rows = [entry for entry in payload.get("BlackboardKeys", []) if entry.get("name") == key]
    if len(rows) != 1 or rows[0].get("TypeName") != kind or rows[0].get("bTypedValueAvailable") is not True:
        raise RunError("Blackboard key missing, ambiguous or wrong type: " + key)
    value = rows[0].get("TypedValue")
    value_string(kind, value)
    return value


def tree_identity(payload):
    # GetCurrentTree changes during ordinary subtree execution; bind the root tree.
    return (payload.get("RootTreeId", payload.get("TreeId")), payload.get("BlackboardId"),
            payload.get("RootTreePath", payload.get("TreePath")))


class Runner(BaseRunner):
    DOMAIN = "BT"
    READ_CAPS = {"GetRuntimeActorBehaviorTree", "GetRuntimeActorProperty"}

    def validate_case(self):
        if set(self.case["Endpoints"]) != {"UE"} or set(self.case.get("Actors", {})) != {"Target"}:
            raise RunError("BT replay requires one explicit UE endpoint and Target binding")
        binding = self.case["Actors"]["Target"]
        if set(binding) != {"Bindings"} or set(binding["Bindings"]) != {"UE"} or not isinstance(binding["Bindings"]["UE"], str) or not binding["Bindings"]["UE"]:
            raise RunError("Target requires an explicit ActorName; automatic AI selection is unsupported")
        if not isinstance(self.case.get("TreePath"), str) or not self.case["TreePath"].startswith("/"):
            raise RunError("Full TreePath is required")
        condition = self.case.get("Condition", {})
        if not isinstance(condition.get("Key"), str) or not condition["Key"]: raise RunError("Condition Key is required")
        for field in ("BlockedValue", "ChangedValue"): value_string(condition.get("Type"), condition.get(field))
        for section in ("Setup", "Steps", "Teardown"):
            for step in self.case.get(section, []):
                if step.get("Type") != "Assert" and step.get("Capability") not in self.READ_CAPS | {"InteractRuntimeActorBehaviorTree"}:
                    raise RunError("Unsupported BT workflow operation")
                arguments = step.get("Arguments", {})
                if step.get("Type") != "Assert" and arguments.get("ActorName") != {"Ref": "/Actors/Target/UE"}:
                    raise RunError("Every BT operation must use the explicit Target binding")
                if step.get("Capability") == "InteractRuntimeActorBehaviorTree" and arguments.get("Action") != "SetBlackboard":
                    raise RunError("Fixed BT replay only changes Blackboard conditions")
                if step.get("Capability") == "InteractRuntimeActorBehaviorTree" and arguments.get("KeyName") != condition["Key"]:
                    raise RunError("Fixed BT replay only changes the declared Condition key")

    def context(self, endpoint, actor):
        response = self.clients[endpoint].call("GetRuntimeActorProperty", {"ActorName": actor, "View": "Context"})
        context = response["Payload"].get("Context", {})
        if response["IsError"] or context.get("ActorName") != actor or not (context.get("bIsPawn") or context.get("bIsController")):
            raise RunError("Explicit Pawn/AIController is missing or was destroyed")
        identity = (context.get("ProcessId"), context.get("WorldId"))
        objects = tuple(context.get(key) for key in ("WorldIdentity", "ActorId", "ControllerId", "PawnId"))
        if any(type(value) is not int or value <= 0 for value in identity) or not all(isinstance(v, str) and v for v in objects[:3]):
            raise RunError("Missing process, World, Actor or Controller identity")
        if context.get("bAuthority") is not True: raise RunError("BT replay requires authority")
        if endpoint in self.identities and (self.identities[endpoint] != identity or self.object_identities[(endpoint, actor)] != objects):
            raise RunError("Process, World, Actor, Pawn or Controller changed; replay stopped")
        result = self.clients[endpoint].call("GetRuntimeActorBehaviorTree", {"ActorName": actor})
        payload = result["Payload"]
        tree_ids = tree_identity(payload)
        if result["IsError"] or not all(isinstance(value, str) and value for value in tree_ids) or tree_ids[2] != self.case["TreePath"]:
            raise RunError("Expected behavior tree/Blackboard is unavailable")
        if "TreeIdentity" in self.state and self.state["TreeIdentity"] != tree_ids:
            raise RunError("Tree or Blackboard was replaced; replay stopped")
        self.state["TreeIdentity"] = tree_ids
        self.state["Snapshot"] = result
        self.identities[endpoint], self.object_identities[(endpoint, actor)], self.contexts[endpoint] = identity, objects, context
        return context

    def bind_actors(self):
        actor = self.case["Actors"]["Target"]["Bindings"]["UE"]
        self.context("UE", actor)
        self.state["Actors"] = {"Target": {"UE": actor}}

    def preflight_targets(self):
        snapshot = self.state["Snapshot"]
        if snapshot["Payload"].get("bRunning") is not True: raise RunError("Behavior tree is not running")
        condition = self.case["Condition"]
        value = blackboard_value(snapshot["Payload"], condition["Key"], condition["Type"])
        self.state["InitialValue"] = value
        self.state["RestoreValue"] = value_string(condition["Type"], value)
        for section in ("Setup", "Steps", "Teardown"):
            for step in self.case.get(section, []):
                expected = resolve(step.get("ExpectedState", {}), self.state).get("Blackboard")
                if expected: blackboard_value(snapshot["Payload"], expected["Key"], expected["Type"])
        self.records.append({"Id": "BlackboardPreflight", "Stage": "Preflight", "Status": "Passed", "Endpoint": "UE", "Result": copy.deepcopy(snapshot)})

    def invoke(self, step):
        result = super().invoke(step)
        if step["Capability"] != "GetRuntimeActorBehaviorTree": return result
        payload = result["Payload"]
        if tree_identity(payload) != self.state["TreeIdentity"]:
            raise RunError("Tree or Blackboard changed during observation")
        if payload.get("bRunning") is not True: raise RunError("Behavior tree stopped during replay")
        expected = resolve(step.get("ExpectedState", {}), self.state)
        matches = True
        if "Node" in expected: matches &= payload.get("ActiveNode") == expected["Node"]
        if "Blackboard" in expected:
            entry = expected["Blackboard"]
            actual = blackboard_value(payload, entry["Key"], entry["Type"])
            try:
                check(actual, {"Value": entry["Value"], "Op": "Approx" if entry["Type"] == "Float" else "Equal", "Tolerance": 1e-5})
            except CheckFailed: matches = False
        payload["MatchesExpected"] = matches
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--output", type=Path, default=Path("bt-retest-results"))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--preflight", action="store_true")
    mode.add_argument("--template", choices=("BlackboardCondition",))
    mode.add_argument("--save-baseline", type=Path)
    mode.add_argument("--compare", type=Path)
    parser.add_argument("--config", type=Path)
    args = parser.parse_args(argv)
    read = lambda path: json.loads(path.read_text(encoding="utf-8-sig"))
    try:
        if args.config and not args.template: raise RunError("--config requires --template")
        if args.template:
            if not args.config: raise RunError("Template requires --config")
            case = blackboard_condition(read(args.config))
            with args.case.open("x", encoding="utf-8") as destination: json.dump(case, destination, indent=2, ensure_ascii=False)
            return 0
        if args.save_baseline or args.compare:
            from retest.compare import load_report, save_baseline, compare_reports, write_comparison
            report = load_report(args.case)
            if report.get("Domain", "GAS") != "BT": raise RunError("BT entry point requires a BT report")
            if args.save_baseline: save_baseline(report, args.save_baseline); return 0
            previous = load_report(args.compare)
            comparison = compare_reports(previous, report)
            write_comparison(comparison, previous, report, args.output)
            return comparison["ExitCode"]
        report = Runner(read(args.case)).run(preflight=args.preflight)
        write_reports(report, args.output)
        print(args.output / "report.html")
        return report["ExitCode"]
    except (RunError, OSError, ValueError, TypeError, KeyError) as exc:
        print("BT retest error: " + (str(exc) if isinstance(exc, RunError) else type(exc).__name__), file=sys.stderr)
        return 2
