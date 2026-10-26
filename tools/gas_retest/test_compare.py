"""Offline baseline lifecycle, regression/recovery, and evidence comparison."""
import copy
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from .compare import compare_reports, load_report, save_baseline, validate_report
from .runner import main, Runner, RunError
from .test_runner import FakeClient


class ComparisonTests(unittest.TestCase):
    def report(self, event="Committed"):
        return {"SchemaVersion": 1, "Name": "Skill", "Mode": "Replay", "ExitCode": 0,
                "CaseSignature": "a" * 64,
                "Endpoints": {"Server": {"Url": "http://localhost:45000/stream", "bAuthority": True}},
                "Contexts": {"Server": {"ProcessId": 1, "WorldId": 2, "bAuthority": True}},
                "Records": [
                    {"Id": "authority", "Endpoint": "Server", "Status": "Passed", "Result": {"Payload": {
                        "CaptureId": "original", "Dropped": 0, "Events": [{"Sequence": 1,
                            "Event": event, "AbilityPath": "/Script/Test.Ability", "SpecHandle": "1",
                            "PredictionKey": "1", "Time": 1.2, "bIdentityKnown": True, "FailureTags": []}]}}},
                    {"Id": "effect", "Status": "Passed"},
                    {"Id": "cleanup", "Stage": "Teardown", "Status": "Passed"}]}

    def test_passed_negative_baseline_is_immutable_and_preserves_evidence(self):
        report = self.report("Failed")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baselines/rejection.json"
            save_baseline(report, path)
            self.assertEqual(load_report(path), report)
            before = path.read_bytes()
            with self.assertRaises(FileExistsError):
                save_baseline(report, path)
            self.assertEqual(before, path.read_bytes())
            for field, value in (("ExitCode", 1), ("Mode", "Preflight")):
                invalid = {**report, field: value}
                with self.assertRaises(RunError):
                    save_baseline(invalid, Path(directory) / "invalid.json")
            report["Records"][0]["Result"]["Payload"]["Dropped"] = 1
            with self.assertRaises(RunError):
                save_baseline(report, Path(directory) / "invalid.json")

    def test_regression_recovery_missing_and_new_failure(self):
        before = self.report()
        broken = copy.deepcopy(before)
        broken["ExitCode"] = 1
        broken["Records"][1].update(Status="Failed", Message="effect missing")
        broken["Records"].pop()
        result = compare_reports(before, broken)
        self.assertEqual(result["Counts"]["NewFailure"], 1)
        self.assertEqual(result["Counts"]["Missing"], 1)
        self.assertEqual(result["ExitCode"], 1)
        recovered = compare_reports(broken, before)
        self.assertEqual(recovered["Counts"]["Recovered"], 1)
        self.assertEqual(recovered["Counts"]["Added"], 1)
        self.assertEqual(recovered["ExitCode"], 0)
        broken["Records"].append({"Id": "new", "Status": "Error", "Message": "error"})
        broken["ExitCode"] = 2
        self.assertEqual(compare_reports(before, broken)["Counts"]["NewFailure"], 2)
        # 当前报告即使声称通过，也不能忽略基线中曾执行的检查。
        missing = self.report()
        missing["Records"].pop()
        self.assertEqual(compare_reports(before, missing)["ExitCode"], 1)

    def test_session_noise_ignored_but_failure_tags_order_and_counts_preserved(self):
        before = self.report()
        after = copy.deepcopy(before)
        after["Contexts"]["Server"].update(ProcessId=99, WorldId=100)
        after["Endpoints"]["Server"]["Url"] = "http://localhost:45020/stream"
        payload = after["Records"][0]["Result"]["Payload"]
        payload["CaptureId"] = "new-capture"
        payload["Events"][0].update(SpecHandle="222", PredictionKey="333", Time=99.9, Sequence=200)
        after["Records"][0].update(DurationMs=500, StartedAt="later")
        self.assertFalse(compare_reports(before, after)["bEvidenceChanged"])
        snapshot = copy.deepcopy(after["Records"][0])
        snapshot["Id"] = "pollAgain"
        after["Records"].append(snapshot)
        self.assertFalse(compare_reports(before, after)["bEvidenceChanged"])
        after["Records"].pop()
        payload["Events"][0]["FailureTags"] = ["Cost"]
        self.assertTrue(compare_reports(before, after)["bEvidenceChanged"])
        payload["Events"].append({**payload["Events"][0], "Sequence": 201, "Event": "Ended"})
        normal = compare_reports(after, after)["AfterEvidence"]["Events"]
        payload["Events"].reverse()
        self.assertEqual(normal, compare_reports(after, after)["AfterEvidence"]["Events"])
        payload["Events"][0]["Sequence"], payload["Events"][1]["Sequence"] = 200, 201
        self.assertNotEqual(normal, compare_reports(after, after)["AfterEvidence"]["Events"])

    def test_incompatible_or_malformed_reports_rejected(self):
        before = self.report()
        for key, value in (("Name", "Other"), ("Mode", "Preflight"), ("CaseSignature", "b" * 64),
                           ("Endpoints", {})):
            with self.assertRaises(RunError):
                compare_reports(before, {**before, key: value})
        for value in ([], {**before, "SchemaVersion": True}, {**before, "Records": []}, {**before, "ExitCode": True},
                      {**before, "Records": [before["Records"][0]] * 2},
                      {**before, "Records": [{"Id": "bad", "Status": "Passed", "Result": []}]}):
            with self.assertRaises(RunError):
                validate_report(value)
        invalid = self.report()
        invalid["Records"][0]["Status"] = "Failed"
        invalid["Records"][0]["Message"] = "failure"
        with self.assertRaises(RunError):
            validate_report(invalid)
        legacy = self.report()
        legacy.pop("CaseSignature")
        self.assertFalse(compare_reports(legacy, before)["bCaseVerified"])

    def test_case_signature_tracks_assertions_but_not_deployment_urls(self):
        case = {"SchemaVersion": 1, "Name": "read", "Endpoints": {"server": {"Role": "server", "Url": "one"}},
                "Steps": [{"Id": "check", "Type": "Assert", "Actual": 1, "Value": 1}]}
        before = Runner(case, FakeClient).run()
        case["Endpoints"]["server"].update(Url="two", TokenEnv="OTHER_TOKEN")
        after = Runner(case, FakeClient).run()
        self.assertEqual(before["CaseSignature"], after["CaseSignature"])
        case["Steps"][0]["Value"] = 2
        changed = Runner(case, FakeClient).run()
        self.assertNotEqual(before["CaseSignature"], changed["CaseSignature"])

    def test_cli_offline_bundle_escaping_and_source_protection(self):
        with tempfile.TemporaryDirectory() as directory, patch("gas_retest.runner.Client", side_effect=AssertionError("offline")), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            root = Path(directory)
            source, current, baseline = root / "previous.json", root / "current.json", root / "baseline.json"
            before = self.report("Failed")
            before["Name"] = '<script>alert("x")</script>'
            source.write_text(json.dumps(before), encoding="utf-8")
            self.assertEqual(main([str(source), "--save-baseline", str(baseline)]), 0)
            self.assertEqual(main([str(source), "--save-baseline", str(baseline)]), 2)
            after = copy.deepcopy(before)
            after["ExitCode"] = 1
            after["Records"][1].update(Status="Failed", Message='<img src=x onerror="x">')
            current.write_text(json.dumps(after), encoding="utf-8")
            original = current.read_bytes()
            output = root / "comparison"
            args = [str(current), "--compare", str(baseline), "--output", str(output)]
            self.assertEqual(main(args), 1)
            result = json.loads((output / "comparison.json").read_text(encoding="utf-8"))
            self.assertEqual(result["Counts"]["NewFailure"], 1)
            html = (output / "comparison.html").read_text(encoding="utf-8")
            self.assertNotIn("<script>", html)
            self.assertNotIn("<img", html)
            self.assertIn("&lt;script&gt;", html)
            self.assertIn("Content-Security-Policy", html)
            self.assertIn("权威端观察到技能激活失败", html)
            for side, report in (("before", before), ("after", after)):
                self.assertEqual(load_report(output / side / "results.json"), report)
                self.assertTrue((output / side / "report.html").exists())
                self.assertTrue((output / side / "junit.xml").exists())
            self.assertEqual(main(args), 2)
            self.assertEqual(original, current.read_bytes())
            self.assertEqual(main([str(source), "--compare", str(current), "--output", str(root / "recovered")]), 0)


if __name__ == "__main__":
    unittest.main()
