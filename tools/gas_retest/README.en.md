<a id="gas-retest--联机复测"></a>
# GAS multiplayer retesting
[中文](README.md) · English

Python 3.10+, standard library only. Run from the plugin's tools directory:

```sh
python -m gas_retest case.json --output results
python -m gas_retest case.json --preflight --output preflight-results
python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare
```

Configure each process's confirmed **direct UE** /stream URL in the case Endpoints. Credentials come from the TokenEnv environment variable or the shared local NexusLink token file and must not be written in cases. Generic cases support one or more endpoints; the original networking examples use a server and two clients.

For arbitrary projects, generate a case offline with python -m gas_retest case.json --template GenericActivation --config project.json, then preflight, run, and compare offline. See [generic integration](../../docs/gas-retest.en.md#genericactivation) for configuration. Associate automatically by PlayerId or explicitly bind an Actor at each endpoint without requiring PlayerState. Explicit mappings are user declarations, not automatic cross-endpoint identity proofs. Preflight verifies the actual ASC, Owner/Avatar, authority, and granted abilities; every step checks process, World, and local object identity. The initial version supports only InstancedPerActor, with a fresh capture per attempt and serialized attempts on the same ability.

examples/PredictionRejected.json and examples/Success.json use NexusDiagnosticGameMode from the public NexusUnreal project. Replace example ports before running. Cases modify the test character's runtime state.

With three diagnostic processes already running, use --template PredictionRejected or --template Success and --server URL --owner URL --observer URL to create a new case. Confirm addresses beforehand: instances are not automatically scanned/selected and files are not overwritten. --preflight checks Actor bindings, process/World, and explicit RequiredAbilities read-only, without scenario or cleanup steps. Full commands are linked below.

In the public NexusUnreal example, run python Script/gas_network_retest.py --ue-root <UE-directory> --quickstart to launch a server and two clients, check connectivity, reproduce prediction rejection, restore conditions, verify success, and generate Saved/GasRetest/index.html.

Outputs are offline report.html, results.json, and junit.xml. Reports distinguish case status from actual ability outcomes and show endpoint events, failing steps, and raw evidence. Exit 0: passed; 1: assertion failure; 2: configuration/runtime error. Writes are never automatically retried; cleanup failures are recorded separately; missing authority evidence never claims server success.

Saving baselines and comparing before/after are offline operations consuming generated results.json. They do not connect to UE or rerun cases:

```sh
python -m gas_retest previous/results.json --save-baseline gas-retest-results/baselines/Success.json
python -m gas_retest current/results.json --compare gas-retest-results/baselines/Success.json --output gas-retest-results/comparison-01
```

Baselines require a passed replay with no dropped or unidentified events. Preflight results and overwriting existing files are rejected. A passed expected-rejection case may be saved while retaining its separate ability outcome. Comparison output directories must not exist. Outputs include comparison.html, comparison.json, and complete before/ and after/ reports with raw evidence.

Comparisons distinguish newly failed, recovered, added, and missing steps, and show endpoint events, failure tags, and role changes. Case name, mode, endpoint roles, and newer case signatures must agree; changing assertions cannot count as recovery. Signatures exclude deployment URLs and token variable names. Legacy reports remain comparable with a clear warning that the full case definition cannot be verified. Event comparison excludes process IDs, ports, times, capture IDs, SpecHandle, and PredictionKey, while preserving capture boundaries, order, counts, abilities, and failure tags.

Use --compare previous/results.json to compare ordinary results too, including recovery after failure. Comparison exits: 0 current case passed without missing steps; 1 current failure/error or missing steps; 2 invalid/incompatible input or output failure. Event changes alone do not decide regression; unconfirmed outcomes do not become server success.

Users can manually run python Script/gas_regression.py --ue-root <UE-directory> in the public NexusUnreal project. It validates version-controlled baselines and case signatures before running fixed PredictionRejected / Success cases, generating summary HTML/JSON/JUnit, per-case comparisons, and reports.zip including logs. Failed and unexecuted cases remain in archives and gates; baselines are not automatically updated. GitHub CI performs only UE-free tool checks in the cloud and does not schedule local UE runs; see the example test guide.


Full format and limitations: [GAS diagnostics and replay](https://github.com/bytepine/NexusLink/blob/master/docs/gas-retest.md).
