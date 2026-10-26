"""Deterministic runner checks, including real HTTP framing and write-once behavior."""
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from .runner import Client, Runner, RunError, check, direct_url, expand_defaults, pointer, resolve, write_reports, main


class FakeClient:
    def __init__(self, config):
        self.role = config["Role"]
        self.calls = []
        self.world = 8
        self.closed = False

    def call(self, capability, args):
        self.calls.append((capability, args))
        if capability == "ListRuntimeActors":
            return {"IsError": False, "Payload": {"Actors": [{"Name": self.role + "_hero"}], "TotalCount": 1}}
        if args.get("Sections") == ["Context"]:
            return {"IsError": False, "Payload": {"Context": {"ProcessId": 4 if self.role == "server" else 5, "WorldId": self.world,
                    "PlayerId": 17, "bLocallyControlled": self.role == "owner", "bAuthority": self.role == "server"}}}
        if args.get("Sections") == ["Abilities"]:
            return {"IsError": False, "Payload": {"Abilities": [{"AbilityPath": "/Script/Test.Ability", "SpecHandle": "1", "InstancingPolicy": 1}]}}
        return {"IsError": capability == "ExpectedFailure", "Payload": {"Value": 42}}

    def close(self):
        self.closed = True


class RunnerTests(unittest.TestCase):
    def case(self):
        return {"SchemaVersion": 1, "Endpoints": {"server": {"Role": "server"}, "owner": {"Role": "owner"}},
                "Actors": {"hero": {"ClassFilter": "Diagnostic", "OwnerEndpoint": "owner"}}, "Steps": []}

    def test_defaults_and_typed_references(self):
        value = expand_defaults({"Actors": [{"Name": "a"}], "Actors_defaults": {"bActive": True}})
        self.assertTrue(value["Actors"][0]["bActive"])
        self.assertIs(resolve({"Ref": "/Actors/0/bActive"}, value), True)
        self.assertEqual(pointer({"a/b": {"~x": 3}}, "/a~1b/~0x"), 3)
        with self.assertRaises(RunError):
            pointer([42], "/-1")

    def test_three_stage_execution_and_reports(self):
        case = self.case()
        case["Steps"] = [
            {"Id": "probe", "Type": "Call", "Endpoint": "server", "Capability": "Probe",
             "Arguments": {"ActorName": {"Ref": "/Actors/hero/server"}}},
            {"Id": "verify", "Type": "Assert", "Actual": {"Ref": "/Steps/probe/Payload/Value"}, "Value": 42}]
        runner = Runner(case, FakeClient)
        report = runner.run()
        self.assertEqual(report["ExitCode"], 0)
        self.assertNotEqual(report["Actors"]["hero"]["server"], report["Actors"]["hero"]["owner"])
        self.assertTrue(all(client.closed for client in runner.clients.values()))
        with tempfile.TemporaryDirectory() as directory:
            write_reports(report, directory)
            self.assertEqual(ET.parse(Path(directory) / "junit.xml").getroot().get("failures"), "0")
            self.assertEqual(json.loads((Path(directory) / "results.json").read_text())["ExitCode"], 0)

    def test_failure_preserves_evidence_and_runs_cleanup(self):
        case = self.case()
        case["Steps"] = [{"Id": "failed", "Type": "Call", "Endpoint": "server", "Capability": "ExpectedFailure"},
                         {"Id": "never", "Type": "Call", "Endpoint": "server", "Capability": "Probe"}]
        case["Teardown"] = [{"Id": "cleanup", "Type": "Call", "Endpoint": "server", "Capability": "Probe"}]
        report = Runner(case, FakeClient).run()
        self.assertEqual(report["ExitCode"], 2)
        steps = [r for r in report["Records"] if r.get("Stage") != "Preflight"]
        self.assertEqual([r["Id"] for r in steps], ["failed", "cleanup"])
        self.assertTrue(steps[0]["Result"]["IsError"])

    def test_expected_tool_error_is_assertable(self):
        case = self.case()
        case["Steps"] = [{"Id": "expected", "Type": "Call", "Endpoint": "server", "Capability": "ExpectedFailure", "ExpectError": True}]
        self.assertEqual(Runner(case, FakeClient).run()["ExitCode"], 0)

    def test_timeout_retains_last_observation(self):
        case = self.case()
        case["Steps"] = [{"Id": "wait", "Type": "Await", "Endpoint": "server", "Capability": "GetRuntimeActorProperty",
                         "Timeout": 0.01, "Interval": 0.01, "Check": {"Path": "/Payload/Value", "Value": 0}}]
        report = Runner(case, FakeClient).run()
        self.assertEqual(report["ExitCode"], 1)
        self.assertEqual(report["Records"][-1]["Result"]["Payload"]["Value"], 42)

    def test_world_change_stops_before_write(self):
        class ChangedClient(FakeClient):
            def call(self, cap, args):
                result = super().call(cap, args)
                if args.get("Sections") == ["Context"]:
                    self.world += 1
                return result
        case = self.case()
        case["Steps"] = [{"Id": "write", "Type": "Call", "Endpoint": "server", "Capability": "Write"}]
        runner = Runner(case, ChangedClient)
        self.assertEqual(runner.run()["ExitCode"], 2)
        self.assertFalse(any(cap == "Write" for client in runner.clients.values() for cap, _ in client.calls))

    def test_invalid_step_is_rejected_before_connecting(self):
        case = self.case()
        case["Steps"] = [{"Id": "bad", "Type": "Await", "Endpoint": "server", "Capability": "Write", "Check": {}}]
        with self.assertRaises(RunError):
            Runner(case, lambda _: self.fail("must not connect")).run()

    def test_ambiguous_actor_binding_fails(self):
        class Ambiguous(FakeClient):
            def call(self, cap, args):
                result = super().call(cap, args)
                if cap == "ListRuntimeActors":
                    result["Payload"]["Actors"] *= 2
                    result["Payload"]["TotalCount"] = 2
                return result
        self.assertEqual(Runner(self.case(), Ambiguous).run()["ExitCode"], 2)

    def test_wrong_authority_and_duplicate_processes_fail_before_writes(self):
        case = self.case()
        case["Endpoints"]["server"]["bAuthority"] = False
        self.assertEqual(Runner(case, FakeClient).run()["ExitCode"], 2)
        class DuplicateProcess(FakeClient):
            def call(self, cap, args):
                result = super().call(cap, args)
                if "Context" in result["Payload"]:
                    result["Payload"]["Context"]["ProcessId"] = 4
                return result
        self.assertEqual(Runner(self.case(), DuplicateProcess).run()["ExitCode"], 2)

    def test_main_writes_configuration_failure_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "invalid.json").write_text("{}", encoding="utf-8")
            self.assertEqual(main([str(path / "invalid.json"), "--output", str(path / "report")]), 2)
            report = json.loads((path / "report/results.json").read_text())
            self.assertEqual(report["ExitCode"], 2)
            self.assertEqual(ET.parse(path / "report/junit.xml").getroot().get("errors"), "1")

    def test_numeric_assertions(self):
        check(1.00001, {"Op": "Approx", "Value": 1, "Tolerance": 0.001})
        with self.assertRaises(RunError):
            check(float("nan"), {"Op": "Approx", "Value": 1})

    def test_unknown_identity_and_overflow_are_not_success(self):
        for payload in ({"Dropped": 1}, {"Events": [{"bIdentityKnown": False}]}):
            class Incomplete(FakeClient):
                def call(self, cap, args):
                    result = super().call(cap, args)
                    if cap == "GetEvents":
                        result["Payload"] = payload
                    return result
            case = self.case()
            case["Steps"] = [{"Id": "events", "Type": "Call", "Endpoint": "server", "Capability": "GetEvents"}]
            report = Runner(case, Incomplete).run()
            self.assertEqual(report["ExitCode"], 2)
            self.assertEqual(report["Records"][-1]["Result"]["Payload"], payload)

    def test_preflight_never_runs_scenario_or_cleanup(self):
        case = self.case()
        case["RequiredAbilities"] = [{"Actor": "hero", "Endpoint": "server", "AbilityPath": "/Script/Test.Ability"}]
        for stage in ("Setup", "Steps", "Teardown"):
            case[stage] = [{"Id": stage, "Type": "Call", "Endpoint": "server", "Capability": "Write"}]
        runner = Runner(case, FakeClient)
        report = runner.run(preflight=True)
        self.assertEqual((report["ExitCode"], report["Mode"]), (0, "Preflight"))
        self.assertTrue(report["Contexts"]["server"]["bAuthority"])
        self.assertTrue(all(r["Stage"] == "Preflight" for r in report["Records"]))
        self.assertFalse(any(cap == "Write" for client in runner.clients.values() for cap, _ in client.calls))

    def test_missing_ability_stops_before_setup_and_cleanup(self):
        for preflight in (False, True):
            case = self.case()
            case["RequiredAbilities"] = [{"Actor": "hero", "Endpoint": "server", "AbilityPath": "/Script/Missing.Ability"}]
            case["Setup"] = [{"Id": "setup", "Type": "Call", "Endpoint": "server", "Capability": "Write"}]
            case["Teardown"] = [{"Id": "cleanup", "Type": "Call", "Endpoint": "server", "Capability": "Write"}]
            runner = Runner(case, FakeClient)
            self.assertEqual(runner.run(preflight=preflight)["ExitCode"], 2)
            self.assertFalse(any(cap == "Write" for client in runner.clients.values() for cap, _ in client.calls))

    def test_template_creation_does_not_connect_or_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.json"
            args = [str(path), "--template", "PredictionRejected", "--output", str(Path(directory) / "errors")]
            for role, port in (("server", 45010), ("owner", 45011), ("observer", 45012)):
                args.extend(["--" + role, f"http://127.0.0.1:{port}/stream"])
            with patch("gas_retest.runner.Client", side_effect=AssertionError("must not connect")):
                self.assertEqual(main(args), 0)
                before = path.read_text(encoding="utf-8")
                self.assertEqual(main(args), 2)
                self.assertEqual(before, path.read_text(encoding="utf-8"))
            case = json.loads(before)
            self.assertEqual(case["Endpoints"]["Owner"]["Url"], "http://127.0.0.1:45011/stream")
            self.assertEqual(len(case["RequiredAbilities"]), 2)

    def test_credential_url_not_written_to_failure_report(self):
        case = {"SchemaVersion": 1, "Endpoints": {"bad": {"Url": "http://user:private-secret@localhost:45000/stream"}}, "Steps": []}
        report = Runner(case).run(preflight=True)
        self.assertEqual(report["ExitCode"], 2)
        self.assertNotIn("private-secret", json.dumps(report))

    def test_invalid_endpoint_ports_and_embedded_credentials_rejected(self):
        for url in ("http://localhost/stream", "http://localhost:0/stream", "http://localhost:65536/stream",
                    "http://localhost:abc/stream", "http://user:secret@localhost:45000/stream",
                    "http://localhost:45000/stream?token=secret", "http://localhost:45000/stream#secret"):
            with self.assertRaises(RunError):
                direct_url(url)
        self.assertEqual(direct_url("http://localhost:45000/stream/"), "http://localhost:45000/stream")

    def test_http_client_protocol_and_no_write_retry(self):
        writes = []
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_DELETE(self):
                self.send_response(204); self.end_headers()
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Mcp-Session-Id", "test-session")
                self.end_headers()
                if body["method"] == "tools/call":
                    writes.append(body)
                    result = {"isError": True, "structuredContent": {"Error": "expected"}}
                else:
                    result = {}
                if "id" in body:
                    self.wfile.write(json.dumps({"jsonrpc": "2.0", "id": body["id"], "result": result}).encode())
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with patch.dict(os.environ, {"NEXUS_MCP_TOKEN": "unit-test-token"}):
                client = Client({"Url": f"http://127.0.0.1:{server.server_port}/stream"})
                self.assertTrue(client.call("Write", {})["IsError"])
                client.close()
            self.assertEqual(len(writes), 1)
        finally:
            server.shutdown(); server.server_close(); thread.join()


if __name__ == "__main__":
    unittest.main()
