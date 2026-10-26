import copy
import json
from pathlib import Path
import tempfile
import unittest
from .runner import compare, export_snapshot, main, render_report, RunError, validate_snapshot


def snapshot(value=1, build="before"):
    return {"SchemaVersion": 1, "Kind": "TraceSnapshot", "Conditions": {
        "Scenario": "Fixed route", "BuildId": build, "Engine": "5.7", "Platform": "Win64", "Hardware": {"CPU": "test", "GPU": "test"},
        "RenderConfig": {"RHI": "test"}, "QualityConfig": {"Preset": "Epic"}, "WarmupSeconds": 1,
        "Window": {"StartTime": 1, "EndTime": 11}, "FrameType": "Game"},
        "Data": {"bAnalysisComplete": True, "DurationSeconds": 12, "StartTime": 1, "EndTime": 11, "FrameType": "Game",
                 "FrameCount": 100, "AverageSeconds": value, "MedianSeconds": value, "P95Seconds": value * 2, "MaxSeconds": value * 3,
                 "bHasCpuTiming": True, "TotalTimerCount": 1, "bTimersTruncated": False,
                 "Timers": [{"Name": "Tick", "Count": 100, "TotalInclusiveSeconds": value, "TotalExclusiveSeconds": value,
                             "MaxInclusiveSeconds": value / 10, "MaxExclusiveSeconds": value / 10}]}}


class FakeClient:
    def __init__(self, config): self.calls = []; self.closed = False; self.data = snapshot()["Data"]
    def call(self, cap, args):
        self.calls.append((cap, args)); return {"IsError": False, "Payload": copy.deepcopy(self.data)}
    def close(self): self.closed = True


class TraceCompareTests(unittest.TestCase):
    def test_deterministic_deltas_and_multiple_sample_medians(self):
        result = compare([snapshot(1), snapshot(3), snapshot(100)], [snapshot(2, "new"), snapshot(4, "new"), snapshot(50, "new")])
        self.assertTrue(result["bComparable"]); self.assertFalse(result["bSingleSample"])
        metric = result["Frames"]["MedianSeconds"]
        self.assertEqual((metric["Before"], metric["After"], metric["AbsoluteChange"]), (3, 4, 1))
        self.assertAlmostEqual(metric["PercentChange"], 100 / 3)
        self.assertEqual(result["Cpu"][0]["BeforeSamples"], [1, 3, 100])
        self.assertEqual(result["Frames"]["P95Seconds"]["After"], 8)
        self.assertEqual(result["ExitCode"], 0)

    def test_conditions_mismatch_never_generates_deltas(self):
        for field, value in (("Scenario", "Other"), ("Hardware", "Other"), ("Engine", "5.8"), ("RenderConfig", "Other"),
                             ("QualityConfig", "Other"), ("Platform", "Linux"), ("WarmupSeconds", .5),
                             ("Window", {"StartTime": 2, "EndTime": 11}), ("FrameType", "Render")):
            other = snapshot(2); other["Conditions"][field] = value
            if field == "Window": other["Data"]["StartTime"] = 2
            if field == "FrameType": other["Data"]["FrameType"] = "Render"
            result = compare([snapshot()], [other])
            self.assertFalse(result["bComparable"], field)
            self.assertIsNone(result["Frames"]["MedianSeconds"]["AbsoluteChange"])
            self.assertIsNone(result["Cpu"][0]["PercentChange"])

    def test_zero_baseline_and_single_sample_are_explicit(self):
        result = compare([snapshot(0)], [snapshot(4)])
        self.assertEqual(result["Frames"]["MedianSeconds"]["AbsoluteChange"], 4)
        self.assertIsNone(result["Frames"]["MedianSeconds"]["PercentChange"])
        self.assertTrue(result["bSingleSample"])
        self.assertIn("单次采样", render_report(result))

    def test_condition_boolean_is_not_an_integer_and_invalid_shapes_fail(self):
        before, after = snapshot(), snapshot()
        before["Conditions"]["RenderConfig"] = {"VSync": False}
        after["Conditions"]["RenderConfig"] = {"VSync": 0}
        self.assertFalse(compare([before], [after])["bComparable"])
        for invalid in ([], None, True):
            bad = snapshot(); bad["Conditions"]["Window"] = invalid
            with self.assertRaises(RunError): validate_snapshot(bad)
        bad = snapshot(); bad["SchemaVersion"] = True
        with self.assertRaises(RunError): validate_snapshot(bad)

    def test_truncated_and_duplicate_cpu_names_never_become_zero(self):
        other = snapshot(2); other["Data"]["Timers"][0]["Name"] = "Other"
        other["Data"].update(TotalTimerCount=3, bTimersTruncated=True)
        result = compare([snapshot()], [other])
        self.assertTrue(all(r["AbsoluteChange"] is None for r in result["Cpu"]))
        self.assertTrue(any(r["After"] is None for r in result["Cpu"]))
        duplicate = snapshot(); duplicate["Data"]["Timers"] *= 2; duplicate["Data"]["TotalTimerCount"] = 2
        self.assertIsNone(compare([duplicate], [snapshot()])["Cpu"][0]["Before"])

    def test_invalid_or_incomplete_evidence_is_an_execution_error(self):
        for key, value in (("bAnalysisComplete", False), ("SectionErrors", ["incomplete"]), ("MedianSeconds", float("nan")),
                           ("P95Seconds", float("inf")), ("FrameCount", 0), ("FrameCount", True), ("Timers", []),
                           ("bHasCpuTiming", False), ("bTimersTruncated", True), ("TotalTimerCount", 0),
                           ("EndTime", 12), ("DurationSeconds", 10), ("FrameType", "Render")):
            bad = snapshot(); bad["Data"][key] = value
            with self.assertRaises(RunError, msg=key): validate_snapshot(bad)
        with self.assertRaises(RunError): compare([], [snapshot()])

    def test_export_uses_explicit_endpoint_and_shared_window(self):
        client = FakeClient({})
        exported = export_snapshot("http://127.0.0.1:45000/stream", "Stopped.utrace", snapshot()["Conditions"], 1, 11, factory=lambda cfg: client)
        self.assertEqual(exported["Data"], snapshot()["Data"])
        self.assertEqual(client.calls[-1][1]["Sections"], ["Summary", "Frames", "Timers"])
        self.assertEqual(client.calls[-1][1]["StartTime"], 1); self.assertTrue(client.closed)
        with self.assertRaises(RunError): export_snapshot("http://user:secret@host:45000/stream", "Stopped.utrace", snapshot()["Conditions"], 1, 11)
        pending = FakeClient({}); pending.data["bAnalysisComplete"] = False
        with self.assertRaisesRegex(RunError, "incomplete"):
            export_snapshot("http://localhost:45000/stream", "Stopped.utrace", snapshot()["Conditions"], 1, 11, timeout=.01, factory=lambda cfg: pending)
        self.assertTrue(pending.closed)

    def test_offline_cli_preserves_evidence_escapes_html_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); old, new = root / "old.json", root / "new.json"
            evidence = snapshot(); evidence["Conditions"]["Scenario"] = "<script>bad()</script>"
            old.write_text(json.dumps(evidence)); new.write_text(json.dumps(evidence))
            output = root / "report"
            args = ["compare", "--before", str(old), "--after", str(new), "--output", str(output)]
            self.assertEqual(main(args), 0)
            report = (output / "report.html").read_text(encoding="utf-8")
            self.assertIn("&lt;script&gt;", report); self.assertNotIn("<script>bad()", report)
            self.assertEqual(json.loads((output / "before-1.json").read_text()), evidence)
            self.assertEqual(main(args), 2)


if __name__ == "__main__": unittest.main()
