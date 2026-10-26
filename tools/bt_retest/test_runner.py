import copy
import json
from pathlib import Path
import tempfile
import unittest
from retest.runner import RunError, write_reports
from retest.compare import compare_reports, load_report, save_baseline, write_comparison
from .runner import Runner, main
from .template import blackboard_condition


def config():
    return {"Name": "AI condition", "Endpoint": {"Url": "http://127.0.0.1:45000/stream"}, "ActorName": "Pawn_0", "TreePath": "/Game/AI/Test.Test",
            "Condition": {"Key": "CanRun", "Type": "Bool", "BlockedValue": False, "ChangedValue": True},
            "Before": {"Node": "Blocked"}, "Expected": {"Node": "Allowed"}, "Timeout": .03, "Interval": .01}


class FakeClient:
    def __init__(self, config):
        self.calls = []; self.writes = []; self.closed = False; self.value = False; self.hold = False
        self.running = True; self.kind = "Bool"; self.change = None; self.context_reads = 0; self.fail_cleanup = False
        self.destroyed = False; self.replace_tree = False; self.subtree = False
    def call(self, cap, args):
        self.calls.append((cap, copy.deepcopy(args)))
        assert args["ActorName"] == "Pawn_0"
        if cap == "GetRuntimeActorProperty":
            self.context_reads += 1
            if self.destroyed and self.context_reads > 1: return {"IsError": True, "Payload": {"Error": "Actor not found"}}
            context = {"ActorName": "Pawn_0", "ProcessId": 10, "WorldId": 20, "WorldIdentity": "20:1", "ActorId": "30:1",
                       "ControllerId": "40:1", "PawnId": "30:1", "bAuthority": True, "bIsPawn": True}
            if self.change and self.context_reads > 1: context[self.change[0]] = self.change[1]
            payload = {"Context": context}
        elif cap == "GetRuntimeActorBehaviorTree":
            payload = {"TreeId": "50:2" if self.replace_tree and self.context_reads > 1 else "50:1", "BlackboardId": "60:1",
                       "TreePath": "/Game/AI/Test.Test", "bRunning": self.running, "ActiveNode": "Allowed" if self.value and not self.hold else "Blocked",
                       "BlackboardKeys": [{"name": "CanRun", "type": "BlackboardKeyType_" + self.kind, "TypeName": self.kind,
                                           "Value": str(self.value), "TypedValue": self.value, "bTypedValueAvailable": True, "bInherited": True}]}
            payload.update(RootTreeId=payload["TreeId"], RootTreePath=payload["TreePath"])
            if self.subtree and self.context_reads > 1: payload.update(TreeId="90:1", TreePath="/Game/AI/Sub.Sub")
        else:
            assert cap == "InteractRuntimeActorBehaviorTree"
            self.writes.append(args["Value"])
            if self.fail_cleanup and len(self.writes) == 3: return {"IsError": True, "Payload": {"Error": "cleanup failed"}}
            self.value = args["Value"] == "true"
            payload = {"bSuccess": True}
        return {"IsError": False, "Payload": payload}
    def close(self): self.closed = True


def run(fake=None, preflight=False):
    fake = fake or FakeClient({})
    return Runner(blackboard_condition(config()), lambda cfg: fake).run(preflight), fake


class BtTests(unittest.TestCase):
    def test_read_only_preflight_and_inherited_key(self):
        report, client = run(preflight=True)
        self.assertEqual(report["ExitCode"], 0); self.assertEqual(client.writes, []); self.assertTrue(client.closed)
        self.assertTrue(report["Records"][-1]["Result"]["Payload"]["BlackboardKeys"][0]["bInherited"])

    def test_change_observe_restore_original_condition(self):
        fake = FakeClient({}); fake.value = True
        report, client = run(fake)
        self.assertEqual(report["ExitCode"], 0); self.assertEqual(report["Domain"], "BT")
        self.assertEqual(client.writes, ["false", "true", "true"]); self.assertTrue(client.value)

    def test_timeout_retains_last_snapshot_and_cleans_up(self):
        fake = FakeClient({}); fake.hold = True
        report, client = run(fake)
        self.assertEqual(report["ExitCode"], 1)
        failed = next(record for record in report["Records"] if record["Id"] == "expected")
        self.assertEqual(failed["Status"], "Failed")
        self.assertEqual(failed["Result"]["Payload"]["ActiveNode"], "Blocked")
        self.assertFalse(client.value); self.assertEqual(client.writes, ["false", "true", "false"])

    def test_process_world_and_objects_replaced_abort_before_writes(self):
        for field, value in (("ProcessId", 11), ("WorldId", 21), ("WorldIdentity", "20:2"), ("ActorId", "30:2"),
                             ("ControllerId", "40:2"), ("PawnId", "31:1")):
            fake = FakeClient({}); fake.change = field, value
            report, client = run(fake)
            self.assertEqual(report["ExitCode"], 2, field); self.assertEqual(client.writes, [], field)

    def test_destroyed_actor_and_replaced_tree_stop_cleanup_writes(self):
        for field in ("destroyed", "replace_tree"):
            fake = FakeClient({}); setattr(fake, field, True)
            report, client = run(fake)
            self.assertEqual(report["ExitCode"], 2); self.assertEqual(client.writes, [])

    def test_normal_subtree_transition_preserves_root_binding(self):
        fake = FakeClient({}); fake.subtree = True
        report, client = run(fake)
        self.assertEqual(report["ExitCode"], 0)
        self.assertFalse(client.value)

    def test_wrong_type_and_stopped_tree_fail_preflight(self):
        for field, value in (("kind", "String"), ("running", False)):
            fake = FakeClient({}); setattr(fake, field, value)
            report, client = run(fake)
            self.assertEqual(report["ExitCode"], 2); self.assertEqual(client.writes, [])

    def test_cleanup_failure_is_error_and_write_is_not_retried(self):
        fake = FakeClient({}); fake.fail_cleanup = True
        report, client = run(fake)
        self.assertEqual(report["ExitCode"], 2); self.assertEqual(len(client.writes), 3)
        self.assertEqual(next(r for r in report["Records"] if r["Id"] == "restore")["Status"], "Error")

    def test_configuration_rejects_implicit_binding_and_wrong_scalar_type(self):
        for change in ({"ActorName": ""}, {"Condition": {"Key": "CanRun", "Type": "Bool", "BlockedValue": 0, "ChangedValue": 1}},
                       {"Endpoint": {"Url": "http://user:password@host:45000/stream"}}, {"TreePath": "Test"}):
            with self.assertRaises(RunError): blackboard_condition({**config(), **change})
        case = blackboard_condition(config()); case["Actors"]["Target"] = {"ClassFilter": "AIController"}
        with self.assertRaises(RunError): Runner(case, lambda cfg: self.fail("must not connect")).run()

    def test_bt_baseline_recovery_old_gas_default_and_cross_domain_rejection(self):
        fake = FakeClient({}); fake.hold = True
        before, _ = run(fake); after, _ = run()
        comparison = compare_reports(before, after)
        self.assertGreater(comparison["Counts"]["Recovered"], 0)
        self.assertEqual(comparison["Domain"], "BT")
        old = {"SchemaVersion": 1, "Name": after["Name"], "ExitCode": 0, "Records": [{"Id": "old", "Status": "Passed"}]}
        self.assertEqual(compare_reports(old, old)["Domain"], "GAS")
        with self.assertRaisesRegex(RunError, "domains"): compare_reports(old, after)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); baseline = root / "baseline.json"
            save_baseline(after, baseline); self.assertEqual(load_report(baseline), json.loads(json.dumps(after)))
            self.assertEqual(json.loads(baseline.read_text())["Kind"], "BtRetestBaseline")
            mismatch = root / "mismatch.json"
            mismatch.write_text(json.dumps({"SchemaVersion": 1, "Kind": "GasRetestBaseline", "Report": after}))
            with self.assertRaisesRegex(RunError, "domain"): load_report(mismatch)
            with self.assertRaises(FileExistsError): save_baseline(after, baseline)
            with self.assertRaises(RunError): save_baseline(before, root / "bad.json")
            write_comparison(comparison, before, after, root / "comparison")
            self.assertIn("完整节点执行历史", (root / "comparison/comparison.html").read_text(encoding="utf-8"))

    def test_cli_template_and_offline_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "config.json"; case = root / "case.json"
            source.write_text(json.dumps(config()))
            self.assertEqual(main([str(case), "--template", "BlackboardCondition", "--config", str(source)]), 0)
            self.assertEqual(main([str(case), "--template", "BlackboardCondition", "--config", str(source)]), 2)
            report, _ = run(); write_reports(report, root / "run")
            self.assertEqual(main([str(root / "run/results.json"), "--save-baseline", str(root / "baseline.json")]), 0)


if __name__ == "__main__": unittest.main()
