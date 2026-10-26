"""Project-neutral templates; generation is offline and never grants abilities."""
import copy
from .runner import RunError, direct_url


def generic_activation(config):
    allowed = {"Name", "Endpoints", "Actors", "Actor", "AbilityPath", "ActivationEndpoint", "AuthorityEndpoint", "ExpectedEvent", "FailureTagHints"}
    if not isinstance(config, dict) or set(config) - allowed:
        raise RunError("Unknown GenericActivation configuration field")
    case = {"SchemaVersion": 1, "Name": config.get("Name", "GenericActivation"), "bRequireStableIdentity": True,
            "Endpoints": copy.deepcopy(config.get("Endpoints")), "Actors": copy.deepcopy(config.get("Actors")),
            "FailureTagHints": copy.deepcopy(config.get("FailureTagHints", {})), "Setup": [], "Steps": [], "Teardown": []}
    endpoints, actors = case["Endpoints"], case["Actors"]
    if not isinstance(case["Name"], str) or not case["Name"].strip():
        raise RunError("Name must be a nonempty string")
    if not isinstance(endpoints, dict) or not endpoints or not isinstance(actors, dict) or len(actors) != 1:
        raise RunError("GenericActivation requires endpoints and exactly one logical Actor")
    alias = config.get("Actor", next(iter(actors)))
    source, authority = config.get("ActivationEndpoint"), config.get("AuthorityEndpoint")
    ability, expected = config.get("AbilityPath"), config.get("ExpectedEvent", "Committed")
    if alias not in actors or source not in endpoints or authority not in endpoints or expected not in ("Committed", "Failed"):
        raise RunError("Actor, ActivationEndpoint, AuthorityEndpoint and ExpectedEvent must be explicit and valid")
    if not isinstance(ability, str) or not ability.startswith("/"):
        raise RunError("AbilityPath must be a discovered full class path")
    case.update(ActivationEndpoint=source, EvidenceAbilityPath=ability)
    for endpoint, cfg in endpoints.items():
        if (not isinstance(endpoint, str) or not endpoint or not isinstance(cfg, dict)
                or set(cfg) - {"Url", "TokenEnv", "bAuthority"} or type(cfg.get("bAuthority")) is not bool):
            raise RunError("Each endpoint needs Url and bAuthority; credentials use TokenEnv, never inline tokens")
        cfg["Url"] = direct_url(cfg.get("Url"))
    if not endpoints[authority]["bAuthority"]:
        raise RunError("AuthorityEndpoint must declare bAuthority=true")
    binding = actors[alias]
    if not isinstance(binding, dict):
        raise RunError("Actor binding must be an object")
    if "Bindings" in binding:
        if (set(binding) != {"Bindings"} or not isinstance(binding["Bindings"], dict)
                or set(binding["Bindings"]) != set(endpoints)
                or any(not isinstance(v, str) or not v for v in binding["Bindings"].values())):
            raise RunError("Bindings must explicitly name an Actor on each endpoint")
    elif (not isinstance(binding.get("ClassFilter"), str) or not binding["ClassFilter"]
          or (binding.get("OwnerEndpoint") not in endpoints and type(binding.get("PlayerId")) is not int)):
        raise RunError("Automatic binding requires ClassFilter and OwnerEndpoint or PlayerId")
    hints = case["FailureTagHints"]
    if not isinstance(hints, dict) or any(not isinstance(k, str) or not isinstance(v, str) for k, v in hints.items()):
        raise RunError("FailureTagHints must map tag strings to explanatory text")
    case["RequiredAbilities"] = [{"Actor": alias, "Endpoint": endpoint, "AbilityPath": ability}
                                 for endpoint in dict.fromkeys([source, authority])]
    def call(identifier, endpoint, cap, **arguments):
        escaped = lambda value: value.replace("~", "~0").replace("/", "~1")
        return {"Id": identifier, "Type": "Call", "Endpoint": endpoint, "Capability": cap,
                "Arguments": {"ActorName": {"Ref": f"/Actors/{escaped(alias)}/{escaped(endpoint)}"}, **arguments}}
    captures = {}
    # 使用数字步骤 ID；端点名称不拼入 JSON Pointer。
    for index, endpoint in enumerate(endpoints):
        identifier = f"capture{index}"
        captures[endpoint] = {"Ref": f"/Steps/{identifier}/Payload/CaptureId"}
        case["Setup"].append(call(identifier, endpoint, "InteractRuntimeActorAbilitySystem", Action="StartActivationCapture"))
        case["Teardown"].append(call(f"stop{index}", endpoint, "InteractRuntimeActorAbilitySystem", Action="StopActivationCapture", CaptureId=captures[endpoint]))
    case["Steps"].append(call("diagnose", authority, "GetRuntimeActorAbilitySystem", Sections=["Context", "Tags", "ActivationDiagnostics"], AbilityPath=ability))
    case["Steps"].append(call("attempt", source, "InteractRuntimeActorAbilitySystem", Action="TryActivateGrantedAbility", AbilityPath=ability))
    wait = call("authority", authority, "GetRuntimeActorAbilitySystem", Sections=["ActivationEvents"], CaptureId=captures[authority])
    wait.update(Type="Await", Check={"Path": "/Payload/EventTypes", "Op": "Contains", "Value": expected})
    case["Steps"].append(wait)
    for index, endpoint in enumerate(endpoints):
        case["Steps"].append(call(f"events{index}", endpoint, "GetRuntimeActorAbilitySystem", Sections=["ActivationEvents"], CaptureId=captures[endpoint]))
    return case
