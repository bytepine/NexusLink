"""GAS replay entry point; historical imports remain available."""
import argparse
import copy
import json
from pathlib import Path
import sys
from retest.runner import (BaseRunner, Client, RunError, CheckFailed, expand_defaults, pointer, resolve, check,
                           machine_token, direct_url, case_signature, write_reports)


class Runner(BaseRunner):
    READ_CAPS = {"GetRuntimeActorAbilitySystem", "GetRuntimeActorProperty", "ListRuntimeActors"}

    def context(self, endpoint, actor):
        response = self.clients[endpoint].call("GetRuntimeActorAbilitySystem", {"ActorName": actor, "Sections": ["Context"]})
        if response["IsError"]:
            raise RunError("Cannot verify endpoint/Actor identity")
        context = response["Payload"].get("Context", {})
        identity = (context.get("ProcessId"), context.get("WorldId"))
        if None in identity or not all(type(item) is int and item > 0 for item in identity):
            raise RunError("Missing process/World identity")
        if endpoint in self.identities and self.identities[endpoint] != identity:
            raise RunError("Endpoint process or World changed; replay aborted")
        object_identity = tuple(context.get(key) for key in ("WorldIdentity", "ActorId", "AscId", "OwnerId", "AvatarId"))
        if self.case.get("bRequireStableIdentity") and (not all(isinstance(value, str) and value for value in object_identity)
                                                      or context.get("bActorInfoReady") is not True):
            raise RunError("ASC actor info or stable object identity unavailable")
        if context.get("bActorInfoReady") is False:
            raise RunError("ASC Owner/Avatar is not ready")
        key = (endpoint, actor)
        if key in self.object_identities and self.object_identities[key] != object_identity:
            raise RunError("Bound Actor/ASC/Owner/Avatar was replaced; replay aborted")
        self.object_identities[key] = object_identity
        expected_authority = self.case["Endpoints"][endpoint].get("bAuthority")
        if expected_authority is not None and context.get("bAuthority") is not expected_authority:
            raise RunError("Endpoint authority role differs from the configured expectation")
        if endpoint == self.case.get("ActivationEndpoint") and not context.get("bAuthority") and not context.get("bLocallyControlled"):
            raise RunError("Activation endpoint must have authority or local control of the Actor")
        self.identities[endpoint] = identity
        self.contexts[endpoint] = context
        return context


    def preflight_abilities(self):
        for required in self.case.get("RequiredAbilities", []):
            endpoint, alias = required["Endpoint"], required["Actor"]
            actor = self.state["Actors"][alias][endpoint]
            result = self.clients[endpoint].call("GetRuntimeActorAbilitySystem", {"ActorName": actor, "Sections": ["Abilities"]})
            record = {"Id": f"Ability:{alias}/{endpoint}", "Stage": "Preflight", "Endpoint": endpoint,
                      "Status": "Passed", "Result": result, "AbilityPath": required["AbilityPath"]}
            self.records.append(record)
            matches = [ability for ability in result["Payload"].get("Abilities", [])
                       if ability.get("AbilityPath") == required["AbilityPath"]]
            if result["IsError"] or len(matches) != 1 or not matches[0].get("SpecHandle"):
                record.update(Status="Error", Message="Required granted ability missing or ambiguous: " + required["AbilityPath"])
                raise RunError(record["Message"])
            if matches[0].get("InstancingPolicy") != 1:
                record.update(Status="Error", Message="Required ability must use InstancedPerActor for this replay format")
                raise RunError(record["Message"])
            if self.case.get("bRequireStableIdentity") and matches[0].get("bIsActive"):
                record.update(Status="Error", Message="Required ability is already active; serialize attempts after it ends")
                raise RunError(record["Message"])


    def bind_actors(self):
        # 先定位所属客户端，再以复制的 PlayerId 在其他进程中关联；Actor 名不跨进程复用。
        for alias, config in self.case.get("Actors", {}).items():
            if "Bindings" in config:
                bindings = config["Bindings"]
                if (not isinstance(bindings, dict) or set(bindings) != set(self.clients)
                        or any(not isinstance(name, str) or not name for name in bindings.values())):
                    raise RunError("Explicit Bindings must name one Actor on every endpoint")
                if "OwnerEndpoint" in config or "PlayerId" in config:
                    raise RunError("Explicit Bindings cannot be combined with PlayerId/OwnerEndpoint")
                for endpoint, name in bindings.items():
                    self.context(endpoint, name)
                self.state["Actors"][alias] = dict(bindings)
                continue
            owner_endpoint = config.get("OwnerEndpoint")
            player_id = config.get("PlayerId")
            endpoints = list(self.clients)
            if owner_endpoint:
                if owner_endpoint not in self.clients:
                    raise RunError("Unknown OwnerEndpoint")
                endpoints.remove(owner_endpoint)
                endpoints.insert(0, owner_endpoint)
            bound = {}
            for endpoint in endpoints:
                response = self.clients[endpoint].call("ListRuntimeActors", {"ClassFilter": config["ClassFilter"], "Limit": 500})
                if response["IsError"]:
                    raise RunError("Actor discovery failed")
                payload = response["Payload"]
                actors = payload.get("Actors", [])
                if payload.get("TotalCount", len(actors)) > len(actors):
                    raise RunError("Actor discovery truncated; narrow ClassFilter")
                matches = []
                for actor in actors:
                    name = actor.get("Name", actor.get("name"))
                    context = self.context(endpoint, name)
                    if endpoint == owner_endpoint and player_id is None:
                        if not context.get("bLocallyControlled"):
                            continue
                    elif player_id is None or context.get("PlayerId") != player_id:
                        continue
                    matches.append((name, context))
                if len(matches) != 1 or matches[0][1].get("PlayerId", -1) < 0:
                    raise RunError(f"Actor binding {alias}/{endpoint} is missing or ambiguous")
                name, context = matches[0]
                player_id = context["PlayerId"]
                bound[endpoint] = name
            self.state["Actors"][alias] = bound
        if self.state["Actors"] and len({identity[0] for identity in self.identities.values()}) != len(self.clients):
            raise RunError("Each endpoint must identify a distinct UE process")


    def preflight_targets(self):
        self.preflight_abilities()

    def validate_case(self):
        requirements = self.case.get("RequiredAbilities", [])
        if not isinstance(requirements, list):
            raise RunError("RequiredAbilities must be an array")
        for required in requirements:
            if (not isinstance(required, dict) or required.get("Endpoint") not in self.case["Endpoints"]
                    or required.get("Actor") not in self.case.get("Actors", {})
                    or not isinstance(required.get("AbilityPath"), str) or not required["AbilityPath"]):
                raise RunError("RequiredAbilities needs a known Actor, Endpoint and nonempty AbilityPath")

    def validate_result(self, result):
        if result["Payload"].get("Dropped", 0):
            raise RunError("Activation evidence overflow; outcome unconfirmed")
        events = result["Payload"].get("Events", [])
        if any(not event.get("bIdentityKnown") for event in events):
            raise RunError("Activation event identity unknown; outcome unconfirmed")
        identities = {(event.get("AbilityPath"), event.get("SpecHandle")) for event in events}
        if len(identities) > 1 or sum(event.get("Event") == "Activated" for event in events) > 1:
            raise RunError("Concurrent activation evidence ambiguous; use a fresh capture per attempt")
        expected_ability = self.case.get("EvidenceAbilityPath")
        if expected_ability and any(event.get("AbilityPath") != expected_ability for event in events):
            raise RunError("Capture observed a different ability; outcome unconfirmed")


def create_template(path, name, endpoints):
    if not all(endpoints.values()):
        raise RunError("Template requires --server, --owner and --observer direct UE URLs")
    endpoints = {role: direct_url(url) for role, url in endpoints.items()}
    case = json.loads((Path(__file__).parent / "examples" / (name + ".json")).read_text(encoding="utf-8"))
    for role, url in endpoints.items():
        case["Endpoints"][role]["Url"] = url
    with path.open("x", encoding="utf-8") as destination:
        destination.write(json.dumps(case, ensure_ascii=False, indent=2) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path, help="Case JSON; for offline baseline/compare modes, a recorded results.json")
    parser.add_argument("--output", default="gas-retest-results")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--preflight", action="store_true", help="Read-only endpoint, Actor and RequiredAbilities checks; never runs Setup/Steps/Teardown")
    mode.add_argument("--template", choices=("PredictionRejected", "Success", "GenericActivation"), help="Create a case without connecting or overwriting a file")
    parser.add_argument("--config", type=Path, help="Project-neutral configuration for GenericActivation")
    mode.add_argument("--save-baseline", type=Path, help="Offline: save a passed replay to a new baseline JSON file")
    mode.add_argument("--compare", type=Path, help="Offline: compare recorded results against a baseline or an earlier results.json")
    for role in ("server", "owner", "observer"):
        parser.add_argument("--" + role, help="Direct UE /stream URL for template generation")
    args = parser.parse_args(argv)
    try:
        if args.config and args.template != "GenericActivation":
            raise RunError("--config requires --template GenericActivation")
        if args.save_baseline or args.compare:
            if any(getattr(args, role) for role in ("server", "owner", "observer")):
                raise RunError("Offline comparison does not accept endpoint URLs")
            from .compare import load_report, save_baseline, compare_reports, write_comparison
            current = load_report(args.case)
            if args.save_baseline:
                save_baseline(current, args.save_baseline)
                print(f"Baseline saved: {args.save_baseline}")
                return 0
            previous = load_report(args.compare)
            comparison = compare_reports(previous, current)
            write_comparison(comparison, previous, current, args.output)
            print(f"Comparison: {Path(args.output) / 'comparison.html'}; " +
                  ", ".join(f"{key}={value}" for key, value in comparison["Counts"].items()))
            return comparison["ExitCode"]
        if args.template:
            if args.template == "GenericActivation":
                if not args.config or any(getattr(args, role) for role in ("server", "owner", "observer")):
                    raise RunError("GenericActivation requires --config, with endpoints supplied only by that file")
                from .template import generic_activation
                case = generic_activation(json.loads(args.config.read_text(encoding="utf-8-sig")))
                with args.case.open("x", encoding="utf-8") as output:
                    output.write(json.dumps(case, ensure_ascii=False, indent=2) + "\n")
            else:
                create_template(args.case, args.template, {role.title(): getattr(args, role) for role in ("server", "owner", "observer")})
            print(f"Created {args.case}; check with: python -m gas_retest \"{args.case}\" --preflight")
            return 0
        if any(getattr(args, role) for role in ("server", "owner", "observer")):
            raise RunError("--server/--owner/--observer require --template")
        case = json.loads(args.case.read_text(encoding="utf-8"))
        report = Runner(case).run(preflight=args.preflight)
        write_reports(report, args.output)
    except (OSError, ValueError, KeyError, TypeError, RunError) as exc:
        message = str(exc) if isinstance(exc, RunError) else type(exc).__name__
        if isinstance(exc, FileExistsError) and (args.save_baseline or args.compare):
            message = "Output already exists; choose a new baseline file or comparison directory"
        print(f"GAS retest configuration error: {message}", file=sys.stderr)
        if args.save_baseline or args.compare:
            return 2
        report = {"SchemaVersion": 1, "Name": args.case.stem, "ExitCode": 2, "Mode": "Preflight" if args.preflight else "Replay",
                  "Records": [{"Id": "Configuration", "Status": "Error", "Message": message}]}
        try:
            write_reports(report, args.output)
        except OSError:
            print("Could not write configuration error report", file=sys.stderr)
        return 2
    print(f"{report['Name']}: exit={report['ExitCode']}; " + ", ".join(f"{r['Id']}={r['Status']}" for r in report["Records"]))
    return report["ExitCode"]
