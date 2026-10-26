"""Generic GAS replay: explicit non-player identity, readiness and real evidence."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from .template import generic_activation
from .runner import Runner, RunError, main
from .report import next_checks, outcome, render_report


def config():
    return {"Endpoints": {"Server": {"Url": "http://127.0.0.1:45010/stream", "bAuthority": True}},
            "Actors": {"Object": {"Bindings": {"Server": "PublicActor"}}},
            "AbilityPath": "/Script/Public.Ability", "ActivationEndpoint": "Server", "AuthorityEndpoint": "Server",
            "ExpectedEvent": "Failed", "FailureTagHints": {"Project.Failure": "Inspect project condition"}}


class FixtureClient:
    def __init__(self, _):
        self.calls = []
        self.events = []
        self.identity = {"ProcessId": 1, "WorldId": 2, "WorldIdentity": "2:3", "ActorId": "3:4", "AscId": "4:5",
                         "OwnerId": "3:4", "AvatarId": "3:4", "bActorInfoReady": True, "bAuthority": True, "PlayerId": -1}

    def close(self): pass

    def call(self, cap, args):
        self.calls.append((cap, args))
        payload = {}
        if args.get("Sections") == ["Context"]:
            payload = {"Context": copy.deepcopy(self.identity)}
        elif args.get("Sections") == ["Abilities"]:
            payload = {"Abilities": [{"AbilityPath": "/Script/Public.Ability", "SpecHandle": "1", "InstancingPolicy": 1}]}
        elif args.get("Action") == "TryActivateGrantedAbility":
            self.events = [{"Event": "Failed", "AbilityPath": "/Script/Public.Ability", "SpecHandle": "1",
                            "Sequence": 1, "bIdentityKnown": True, "FailureTags": ["Project.Failure"]}]
        elif args.get("Sections") == ["ActivationEvents"] or args.get("Action") == "StopActivationCapture":
            payload = {"CaptureId": "1", "Events": self.events, "EventTypes": [e["Event"] for e in self.events]}
        elif args.get("Action") == "StartActivationCapture":
            payload = {"CaptureId": "1"}
        return {"IsError": False, "Payload": payload}


class GenericTests(unittest.TestCase):
    def test_config_is_offline_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "config.json").write_text(json.dumps(config()), encoding="utf-8")
            args = [str(root / "case.json"), "--template", "GenericActivation", "--config", str(root / "config.json"), "--output", str(root / "errors")]
            self.assertEqual(main(args), 0)
            before = (root / "case.json").read_bytes()
            self.assertEqual(main(args), 2)
            self.assertEqual((root / "case.json").read_bytes(), before)
            self.assertNotIn("NexusDiagnostic", before.decode())

    def test_explicit_actor_without_player_state_and_actual_tags(self):
        report = Runner(generic_activation(config()), FixtureClient).run()
        self.assertEqual(report["ExitCode"], 0, report)
        self.assertIn("失败", outcome(report)[0])
        self.assertIn("Inspect project condition", " ".join(next_checks(report)))
        self.assertIn("用户声明", render_report(report))
        report["FailureTagHints"] = {"Unseen.Tag": "must not show"}
        self.assertNotIn("must not show", " ".join(next_checks(report)))

    def test_preflight_only_reads(self):
        runner = Runner(generic_activation(config()), FixtureClient)
        self.assertEqual(runner.run(preflight=True)["ExitCode"], 0)
        self.assertTrue(all(cap == "GetRuntimeActorAbilitySystem" for cap, _ in runner.clients["Server"].calls))

    def test_identity_replacement_stops_next_write(self):
        for field, changed in (("ProcessId", 99), ("WorldId", 99), ("ActorId", "3:99"), ("AscId", "4:99"),
                               ("OwnerId", "5:99"), ("AvatarId", "6:99"), ("WorldIdentity", "2:99")):
            class Replaced(FixtureClient):
                def call(self, cap, args):
                    result = super().call(cap, args)
                    if args.get("Action") == "StartActivationCapture": self.identity[field] = changed
                    return result
            runner = Runner(generic_activation(config()), Replaced)
            self.assertEqual(runner.run()["ExitCode"], 2, field)
            self.assertFalse(any(args.get("Action") == "TryActivateGrantedAbility" for _, args in runner.clients["Server"].calls))

    def test_unready_missing_ambiguous_or_wrong_policy_is_not_activated(self):
        for change in ("unready", "missing", "ambiguous", "policy", "authority"):
            class Invalid(FixtureClient):
                def call(self, cap, args):
                    result = super().call(cap, args)
                    p = result["Payload"]
                    if "Context" in p:
                        if change == "unready": p["Context"]["bActorInfoReady"] = False
                        if change == "authority": p["Context"]["bAuthority"] = False
                    if "Abilities" in p:
                        if change == "missing": p["Abilities"] = []
                        if change == "ambiguous": p["Abilities"] *= 2
                        if change == "policy": p["Abilities"][0]["InstancingPolicy"] = 2
                    return result
            runner = Runner(generic_activation(config()), Invalid)
            self.assertEqual(runner.run()["ExitCode"], 2, change)
            self.assertFalse(any(cap.startswith("Interact") for cap, _ in runner.clients["Server"].calls))

    def test_timeout_retains_snapshot_and_unconfirmed_outcome(self):
        class Missing(FixtureClient):
            def call(self, cap, args):
                result = super().call(cap, args)
                self.events = []
                return result
        case = generic_activation(config())
        next(s for s in case["Steps"] if s["Id"] == "authority").update(Timeout=0.01, Interval=0.01)
        report = Runner(case, Missing).run()
        self.assertEqual(report["ExitCode"], 1)
        self.assertIn("未确认", outcome(report)[0])
        self.assertEqual(next(r for r in report["Records"] if r["Id"] == "authority")["Result"]["Payload"]["Events"], [])

    def test_config_requires_explicit_valid_endpoints_and_bindings(self):
        for key, value in (("ExpectedEvent", "Anything"), ("AuthorityEndpoint", "missing"), ("AbilityPath", "short"),
                           ("FailureTagHints", []), ("Actors", {})):
            c = config(); c[key] = value
            with self.assertRaises(RunError): generic_activation(c)

    def test_captured_other_ability_cannot_confirm_requested_one(self):
        class Wrong(FixtureClient):
            def call(self, cap, args):
                result = super().call(cap, args)
                for event in self.events: event["AbilityPath"] = "/Script/Other.Ability"
                return result
        report = Runner(generic_activation(config()), Wrong).run()
        self.assertEqual(report["ExitCode"], 2)
        self.assertIn("未确认", outcome(report)[0])
