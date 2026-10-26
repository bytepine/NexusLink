NexusLink MCP: Unreal Editor + runtime control (assets / PIE / UMG / animation / AI / editor). Registered extension fragments are appended after this text.

> Host note: **MCP runs in the Editor binary, and in Development/DebugGame standalone Game/DS builds** (`NexusLink` module is `Type: Runtime`; `NexusLinkEditor` is `Type: Editor`; Shipping strips the server at compile time). Trigger keywords are in the proxy `InitializePrefix`.

## Tool model

Meta-tools: `SearchCapabilities`, `CallCapability`, `SubmitFeedback` (direct `tools/call`). Everything else is a capability via `CallCapability(capability, arguments)`; `Arguments` must be a nested object.

## First action

When connected to UE: MCP first—do not guess `/Game/...` or grep the repo alone. To read assets: `SearchAsset` (narrow `AssetType` + `PathFilter`) → `assets[].path` + `RecommendedGet`/`RecommendedManage` (typed at top level; per-item when `All`) → then read/write. Unknown cap → `SearchCapabilities`. Use business terms only for `SearchAsset`, not `SearchCapabilities`.

## Token budget

- Response body is the main cost: keep `Sections` narrow (avoid `All`), start with small `Limit`; for logs use `GetOutputLog` ≤50 with `CategoryFilter` / `Verbosity` / `TextFilter`, paginate with `Offset` if needed.
- When lists include `<k>_defaults`, missing fields equal that value (`merged={**defaults,**entry}`).
- Diagnostic logs: prefer `Preset=diagnose` (newest + ≥warning + `SummaryByCategory`/`ErrorCount` + limit≤50); or manual `Order=newest` + `bIncludeSummary`; note `LatestSequence` before repro, then `SinceSequence` for deltas. Entries include UTC `Time` (ISO-8601). To capture only the next action: `GetOutputLog` with `Watch` (categories/TextIncludes/TextExcludes/verbosity), act, then `bCollectWatch=true`. Consecutive duplicates collapse to `Repeat`.
- **Single target**: capabilities use only `AssetPath` / `ActorName` / `WidgetName`; cross-target batch via `CallCapability(Calls=[{capability,arguments},…])` in one round. Within one target keep `Sections` / `PropertyPaths` / `Operations` / `Updates`.
- Prefer `SearchCapabilities` with `CapabilityName` once for full `parameters[]`.

## Parameter contract (Breaking)

| Canonical | Notes |
|---|---|
| Single target + `calls[]` | No `assetPaths`/`actorNames`/`widgetNames`; cross-target only via meta-tool `calls[]` |
| manage → `operations[]` | No top-level `Fields`/`Rows`/`Keys`/`Widgets`/`ops`/bare `Action` synthesis |
| get → `PropertyPaths[]` | Writes use `updates[].PropertyPath` |
| Renames | `newPath`→`DestAssetPath`; `blueprintPath`→`AssetPath`; `ownerWidget`→`OwnerClass`; `FilePath`→`ScriptPath`; `classPath`→`ClassName` |

Legacy keys always → `ArgInvalid`; no alias compatibility.

## Naming

| Verb | Use |
|---|---|
| `get` / `list` / `search` | Read-only |
| `Set` | Only `*_property` + `updates[]` |
| `interact` | `Action` commands (not PropertyPaths) |
| `manage` | Disk `*_asset_*` structural edits only |

Forbidden: `ManageAnimation`, `SetRuntimeActorAnimation`.

## Routing (patterns + exceptions)

**Assets**: prefer `SearchAsset` → `recommended*`; otherwise `{get|manage|create}_asset_{type}` — type ∈ `blueprint` / `Material` / `AnimBlueprint` / `AnimMontage` / `user_widget` / `BehaviorTree` / `Blackboard` / `data_table` / `data_asset` / `struct` / `Texture` / `StaticMesh` / `SkeletalMesh` / `AnimSequence` / `Skeleton` / `SoundWave` / `SoundCue` / `Level` / `level_sequence` / `PhysicalMaterial` / `string_table` / `foliage_type` / `font` / `media_source`. One (verb, type) covers all sub-aspects (do not look for `manage_asset_blueprint_variable`, etc.). Exceptions: `ManageAssetStructField`, `GetAssetRefs`, `ExportAsset`, `ReimportAsset`, `CompileBlueprint`, `SaveAsset` / `RenameAsset` / `DuplicateAsset` / `DeleteAsset` / `UnloadAsset`.

**Runtime**: `{verb}_runtime_{target}[_aspect]` (`list`/`get`/`Set`/`spawn`/`destroy`/`interact`/`diff`; Target=`Actor`/`Widget`/`slate_widget`, actor may add `_property`/`_animation`/`_behavior_tree`/`_audio`/`_niagara`/`_ai`/`_ability_system`). Animation: read `GetRuntimeActorAnimation`, write `InteractRuntimeActorAnimation`. Non-pattern: `InteractRuntimeWidget`, `DiffRuntimeActors`, `GetRuntimeSlateWidget`, `GetOutputLog` / `SetLogCaptureFilter`, `ExecCommand`, `CaptureViewport`.

**Plugin-gated**: GAS / Niagara / StateTree / MVVM / EQS / MetaSound / PCG / ControlRig / Enhanced Input / Paper2D / GeometryCollection / CommonUI / Movie Render Queue / Python (`GetPythonApi` / `ExecPython`) register only with matching plugin+engine; `SearchCapabilities` `NotFound` → skip, do not hard-call handshake names. Tag queries: `GetGameplayTags` is always available.

**Editor**: `ControlPie`, `ControlMoviePipeline`, `SearchConsoleVariables`, `CaptureEditorPanel`, `GetEditorContext` (selection + `CurrentMap`), `GetEditorInfo`.

## Workflow notes

GAS diagnosis: explicit ActorName; GetRuntimeActorAbilitySystem Sections=[Context,Tags,ActivationDiagnostics] with SpecHandle or AbilityPath. StartActivationCapture on authority/client before TryActivateGrantedAbility (never grants), read ActivationEvents with CaptureId, then StopActivationCapture. bRequestAccepted is local only: require authority Committed/Failed evidence. Unknown identity, Dropped>0 or absent authority evidence means unconfirmed. Diagnose is snapshot-only; custom cost/check rules need real failure events. See docs/gas-retest.md for replay.

1. Before blueprint writes: `GetAssetBlueprint(Sections=["graphOverview"])`, use returned graph names; non-Actor BP forbids `AddComponent` / `SetDefaults`. BPI: `CreateAssetBlueprint(ParentClass=Interface)`; functions/impl via `AddFunction` / `AddInterface`. WBP tree/animation: `ManageAssetUserWidget` (`AddTrack` may take `WidgetName`+`PropertyPath`); `GetAssetUserWidget(Sections=["graphOverview"])` lists graphs, EventGraph via `ManageAssetBlueprint`.
2. Behavior trees: after edits `bSaveToDisk` or `SaveAsset`; change type with `ReplaceNode`; graph drift with `SyncGraph`.
3. manage optional `bSaveToDisk` (default false); only BP/ABP/WBP manage may pass `bCompile` (default false). Standalone `SaveAsset` / `CompileBlueprint` still available.

## Hard rules

- Write success has no `success:true`. Check MCP `isError` and structured operation/section/compile/save errors; partial single-call edits may remain. Failed batches expose `bRolledBack` / `UndoNote`; do not blindly retry the whole batch. `interact_*` changes runtime command state (not PropertyPaths).
- `*_asset_*` = disk (Editor host); `*_runtime_*` needs PIE/Game (`bHasPlayWorld`); Game/DS host (`HostKind` ≠ Editor) has no `get_asset_*`.
- `SearchAsset`: no `AssetType=all` + bare `/Game/`; then use `recommended*` + `Path`.
- `SearchCapabilities`: infer name from patterns above and pass `CapabilityName` (full `parameters[]` once); otherwise narrow 1–2 word `Query` (e.g. `blueprint variable`); `Query=""` is name-only catalog; **do not** use `blueprint` / `Asset` / `Runtime` / `Animation` alone (`QueryTooBroad`). On failure check `ErrorKind` / `SuggestedQueries`; `NotFound` with unregistered hint → do not retry same domain; on `CallCapability` `disabled` or `UserDenied` do not retry.
- `GetRuntimeActorProperty` requires non-empty `ActorName` (call `ListRuntimeActors` first, after `ControlPie`/`GetEditorInfo` `bHasPlayWorld=true`); `ExecCommand` requires non-empty `Command`. Caps tagged `dangerous` (`ExecCommand` / `ExecPython`, and any later plugin that sets the same tag) in Confirm mode require `Reason` (purpose, expected effect, why no safer dedicated cap); `bConfirmationRequired: true` on search details. Do not retry `UserDenied`.
- Unsure whether an `unreal.*` API exists on this engine version: call `GetPythonApi` first; do not guess UE5 APIs on UE4 (or vice versa).
- `ExecPython` writes are only undoable when they go through `obj.set_editor_property(...)` or an explicit `obj.modify()`; plain `obj.foo = x` is **not** recorded. Prefer the former, and check the returned `bUndoRecorded` — `false` means Ctrl+Z will not roll this back.
- Reuse an existing `Sections=["All"]` response when still current. Re-read after writes or runtime changes; `RedundantCall` is advisory and fresh data is still returned.
- Retry ≥2 / no suitable cap / schema guessing / serial ≥3 → `SubmitFeedback`; **`FeedbackHint` is mandatory**.
