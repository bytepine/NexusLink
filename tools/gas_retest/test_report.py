"""报告只描述已有证据，并安全展示项目提供的文本。"""
import copy
import unittest

from .report import collect_evidence, outcome, render_report


class ReportTests(unittest.TestCase):
    def report(self):
        return {"Name": "PredictionRejected", "ExitCode": 0,
                "Contexts": {"server": {"bAuthority": True}, "owner": {"bAuthority": False}},
                "Records": [self.event("server", "Failed"), self.event("owner", "Activated")]}

    def event(self, endpoint, event):
        return {"Id": endpoint, "Endpoint": endpoint, "Status": "Passed", "Result": {"Payload": {
            "CaptureId": endpoint, "Dropped": 0, "Events": [{"Event": event, "Sequence": 1,
            "AbilityPath": "/Script/Test.Ability", "SpecHandle": "1", "bIdentityKnown": True,
            "FailureTags": ["Example.Rejected"] if event == "Failed" else []}]}}}

    def test_expected_rejection_is_distinct_from_case_pass(self):
        report = self.report()
        html = render_report(report)
        self.assertIn("用例通过", html)
        self.assertIn("客户端已预测激活，权威端观察到失败", html)
        self.assertNotIn("权威端已提交技能", html)
        self.assertIn("Example.Rejected", html)
        report["Records"][0] = self.event("server", "Committed")
        self.assertEqual(outcome(report)[0], "权威端已提交技能")

    def test_missing_overflow_and_unknown_identity_are_unconfirmed(self):
        report = self.report()
        report["Records"].pop(0)
        self.assertIn("未确认", outcome(report)[0])
        for field, value in (("Dropped", 1), ("UnknownIdentity", True)):
            report = self.report()
            payload = report["Records"][0]["Result"]["Payload"]
            if field == "Dropped":
                payload[field] = value
            else:
                payload["Events"][0]["bIdentityKnown"] = False
            self.assertIn("未确认", outcome(report)[0])

    def test_repeated_snapshots_are_deduplicated_and_html_escaped(self):
        report = self.report()
        report["Records"].append(copy.deepcopy(report["Records"][0]))
        self.assertEqual(len(collect_evidence(report)[1]), 2)
        report["Name"] = '<script>alert("x")</script>'
        report["Records"][0]["Message"] = '<img src=x onerror="alert(1)">'
        html = render_report(report)
        self.assertNotIn("<script>", html)
        self.assertNotIn("<img", html)
        self.assertIn("&lt;script&gt;", html)
        self.assertIn("Content-Security-Policy", html)

    def test_configuration_and_preflight_reports_do_not_claim_gameplay(self):
        report = {"Name": "Configuration", "ExitCode": 2,
                  "Records": [{"Id": "Initialize", "Status": "Error", "Message": "Actor missing"}]}
        self.assertIn("运行错误", render_report(report))
        self.assertIn("未确认", outcome(report)[0])
        report["Mode"] = "Preflight"
        self.assertEqual(outcome(report)[0], "仅预检，未激活技能")


if __name__ == "__main__":
    unittest.main()
