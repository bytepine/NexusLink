NexusLink MCP: Unreal Editor + runtime control (assets / PIE / UMG / animation / AI / editor). Registered extension fragments are appended after this text.

> Host note: **MCP runs in the Editor binary, and in Development/DebugGame standalone Game/DS builds** (`NexusLink` module is `Type: Runtime`; `NexusLinkEditor` is `Type: Editor`; Shipping strips the server at compile time). Trigger keywords are in the proxy `InitializePrefix`.

## Tool model

Meta-tools: `SearchCapabilities`, `CallCapability`, `SubmitFeedback` (direct `tools/call`). Everything else is a capability via `CallCapability(Capability, Arguments)`; `Arguments` must be a nested object.

## First action

When connected to UE: MCP first—do not guess `/Game/...` or grep the repo alone. To read assets: `SearchAsset` (narrow `AssetType` + `PathFilter`) → `Assets[].Path` + `RecommendedGet`/`RecommendedManage` (typed at top level; per-item when `All`) → then read/write. Unknown cap → `SearchCapabilities`. Use business terms only for `SearchAsset`, not `SearchCapabilities`.

## Token budget

- Response body is the main cost: keep `Sections` narrow (avoid `All`), start with small `Limit`; for logs use `GetOutputLog` ≤50 with `CategoryFilter` / `Verbosity` / `TextFilter`, paginate with `Offset` if needed.
- When lists include `<k>_defaults`, missing fields equal that value (`merged={**defaults,**entry}`).
- Diagnostic logs: prefer `Preset=Diagnose` (newest + ≥warning + `SummaryByCategory`/`ErrorCount` + limit≤50); or manual `Order=Newest` + `bIncludeSummary`; note `LatestSequence` before repro, then `SinceSequence` for deltas. Entries include UTC `Time` (ISO-8601). To capture only the next action: `GetOutputLog` with `Watch` (Categories/TextIncludes/TextExcludes/Verbosity), act, then `bCollectWatch=true`. Consecutive duplicates collapse to `Repeat`.
- **Single target**: capabilities use `AssetPath` / `ActorName` / `WidgetName`; `InteractRuntimeWidget` also accepts exact `WidgetPath` from `ListRuntimeWidgets` → `Path`. Ambiguous names return `Candidates[].Path` without interacting; detached/hidden widgets are rejected. Cross-target batch via `CallCapability(Calls=[{Capability,Arguments},…])` in one round. Within one target keep `Sections` / `PropertyPaths` / `Operations` / `Updates`.
- Prefer `SearchCapabilities` with `CapabilityName` once for full `Parameters[]`.

## Parameter contract (Breaking)

| Canonical | Notes |
|---|---|
| Single target + `Calls[]` | No `assetPaths`/`actorNames`/`widgetNames`; cross-target only via meta-tool `Calls[]` |
| manage → `Operations[]` | No top-level `Fields`/`Rows`/`Keys`/`Widgets`/`ops`/bare `Action` synthesis |
| get → `PropertyPaths[]` | Writes use `Updates[].PropertyPath` |
| Renames | `newPath`→`DestAssetPath`; `blueprintPath`→`AssetPath`; `ownerWidget`→`OwnerClass`; `FilePath`→`ScriptPath`; `classPath`→`ClassName` |

Legacy keys always → `ArgInvalid`; no alias compatibility.

## Naming

| Verb | Use |
|---|---|
| `Get` / `List` / `Search` | Read-only |
| `Set` | Only `*Property` + `Updates[]` |
| `Interact` | `Action` commands (not PropertyPaths) |
| `Manage` | Disk `*Asset*` structural edits only |

Forbidden: `ManageAnimation`, `SetRuntimeActorAnimation`.

## Routing (patterns + exceptions)

**Assets**: prefer `SearchAsset` → `RecommendedGet` / `RecommendedManage`; otherwise `{Get|Manage|Create}Asset{Type}` — Type ∈ `Blueprint` / `Material` / `AnimBlueprint` / `AnimMontage` / `UserWidget` / `BehaviorTree` / `Blackboard` / `DataTable` / `DataAsset` / `Struct` / `Texture` / `StaticMesh` / `SkeletalMesh` / `AnimSequence` / `Skeleton` / `SoundWave` / `SoundCue` / `Level` / `LevelSequence` / `PhysicalMaterial` / `StringTable` / `FoliageType` / `Font` / `MediaSource`. One (verb, type) covers all sub-aspects (do not look for `ManageAssetBlueprintVariable`, etc.). Exceptions: `ManageAssetStructField`, `GetAssetRefs`, `ExportAsset`, `ReimportAsset`, `CompileBlueprint`, `SaveAsset` / `RenameAsset` / `DuplicateAsset` / `DeleteAsset` / `UnloadAsset`.

**Runtime**: `{Verb}Runtime{Target}{Aspect?}` (`List`/`Get`/`Set`/`Spawn`/`Destroy`/`Interact`/`Diff`; Target=`Actor`/`Widget`/`SlateWidget`, actor may add `Property`/`Animation`/`BehaviorTree`/`Audio`/`Niagara`/`AI`/`AbilitySystem`). Animation: read `GetRuntimeActorAnimation`, write `InteractRuntimeActorAnimation`. Non-pattern: `InteractRuntimeWidget`, `DiffRuntimeActors`, `GetRuntimeSlateWidget`, `GetOutputLog` / `SetLogCaptureFilter`, `ExecCommand`, `CaptureViewport`.

**Plugin-gated**: GAS / Niagara / StateTree / MVVM / EQS / MetaSound / PCG / ControlRig / Enhanced Input / Paper2D / GeometryCollection / CommonUI / Movie Render Queue / Python (`GetPythonApi` / `ExecPython`) register only with matching plugin+engine; `SearchCapabilities` `NotFound` → skip, do not hard-call handshake names. Tag queries: `GetGameplayTags` is always available.

**Editor**: `ControlPie`, `ControlMoviePipeline`, `SearchConsoleVariables`, `CaptureEditorPanel`, `GetEditorContext` (selection + `CurrentMap`), `GetEditorInfo`.

## Workflow notes

GAS diagnosis: explicit ActorName; GetRuntimeActorAbilitySystem Sections=[Context,Tags,ActivationDiagnostics] with SpecHandle or AbilityPath. StartActivationCapture on authority/client before TryActivateGrantedAbility (never grants), read ActivationEvents with CaptureId, then StopActivationCapture. bRequestAccepted is local only: require authority Committed/Failed evidence. Unknown identity, Dropped>0 or absent authority evidence means unconfirmed. Diagnose is snapshot-only; custom cost/check rules need real failure events. See docs/gas-retest.md for replay.

1. Before blueprint writes: `GetAssetBlueprint(Sections=["GraphOverview"])`, use returned graph names; non-Actor BP forbids `AddComponent` / `SetDefaults`. BPI: `CreateAssetBlueprint(ParentClass=Interface)`; functions/impl via `AddFunction` / `AddInterface`. WBP tree/animation: `ManageAssetUserWidget` (`AddTrack` may take `WidgetName`+`PropertyPath`); `GetAssetUserWidget(Sections=["GraphOverview"])` lists graphs, EventGraph via `ManageAssetBlueprint`.
2. Behavior trees: after edits `bSaveToDisk` or `SaveAsset`; change type with `ReplaceNode`; graph drift with `SyncGraph`.
3. manage optional `bSaveToDisk` (default false); only BP/ABP/WBP manage may pass `bCompile` (default false). Standalone `SaveAsset` / `CompileBlueprint` still available.

## Hard rules

- Write success has no `success:true`. Check MCP `isError` and structured operation/section/compile/save errors; partial single-call edits may remain. Failed batches expose `bRolledBack` / `UndoNote`; do not blindly retry the whole batch. `Interact*` changes runtime command state (not PropertyPaths).
- `*Asset*` = disk (Editor host); `*Runtime*` needs PIE/Game (`bHasPlayWorld`); Game/DS host (`HostKind` ≠ Editor) has no `GetAsset*`.
- `SearchAsset`: no `AssetType=All` + bare `/Game/`; then use `RecommendedGet` / `RecommendedManage` + `Path`.
- `SearchCapabilities`: infer name from patterns above and pass `CapabilityName` (full `Parameters[]` once); otherwise narrow 1–2 word `Query` (e.g. `blueprint variable`); `Query=""` is name-only catalog; **do not** use `blueprint` / `Asset` / `Runtime` / `Animation` alone (`QueryTooBroad`). On failure check `ErrorKind` / `SuggestedQueries`; `NotFound` with unregistered hint → do not retry same domain; on `CallCapability` `disabled` or `UserDenied` do not retry.
- `GetRuntimeActorProperty` requires non-empty `ActorName` (call `ListRuntimeActors` first, after `ControlPie`/`GetEditorInfo` `bHasPlayWorld=true`); `ExecCommand` requires non-empty `Command`. Caps tagged `Dangerous` (`ExecCommand` / `ExecPython`, and any later plugin that sets the same tag) in Confirm mode require `Reason` (purpose, expected effect, why no safer dedicated cap); `bConfirmationRequired: true` on search details. Do not retry `UserDenied`.
- Unsure whether an `unreal.*` API exists on this engine version: call `GetPythonApi` first; do not guess UE5 APIs on UE4 (or vice versa).
- `ExecPython` writes are only undoable when they go through `obj.set_editor_property(...)` or an explicit `obj.modify()`; plain `obj.foo = x` is **not** recorded. Prefer the former, and check the returned `bUndoRecorded` — `false` means Ctrl+Z will not roll this back.
- Reuse an existing `Sections=["All"]` response when still current. Re-read after writes or runtime changes; `RedundantCall` is advisory and fresh data is still returned.
- Retry ≥2 / no suitable cap / schema guessing / serial ≥3 → `SubmitFeedback`; **`FeedbackHint` is mandatory**.

Editor UI: open/focus an asset via ControlEditorAssetEditor (Open/Focus/Close need GUI rendering; Close rejects dirty/shared editors, save explicitly first; list via GetEditorContext Sections=[AssetEditors]; Status/bValidateOnly work headless); maps use ControlEditorLevel. Slate: GetEditorSlate → Ref → InteractEditorSlate / CaptureEditorSlate. Local tests: ControlEditorAutomation / ListEditorAutomationTests. Compile: CompileEditorLiveCoding. Long tasks return OperationId; poll GetEditorOperation (optional WaitSeconds ≤30). Completed is execution state; inspect Outcome separately. Request cancellation only cancels waiting. Explicit Stop applies only where supported. Never retry writes automatically or mix these operations into atomic Calls[] batches. Search exposes outputSchema and Execution backend information.

Reparent normal/Widget/Anim/GameplayAbility Blueprints with ManageAssetBlueprint Operations=[{Action:Reparent,ParentClass:...}]; bCompile/bSaveToDisk remain explicit. Leaving the parent hierarchy requires operation bAllowPotentialDataLoss=true. Read-only navigation: GetNavigationPath(Start={X,Y,Z},End={X,Y,Z},World=Auto|Editor|Play); ordinary ActorName is optional, no AIController/movement/nav rebuild. Check bNavigationAvailable, bPathFound and bPartial; MaxPoints only bounds returned points. GetNavigationPoint projects one point. Path queries accept FilterClass, AgentRadius/AgentHeight, ProjectionExtent and bProjectStart/bProjectEnd; WorldName disambiguates PIE worlds. Align MoveTo FilterClass/bProjectEnd/bAllowPartialPath explicitly. GetRuntimeActorAI reports tracked completion; InteractRuntimeActorAI Stop stops movement, request acceptance alone is not arrival.

Asset edit additions: GetAssetEditPreview supports Reparent/ChangeVariableType and scalar DataAsset Set, Level SetActorProperty, BP SetComponentProperty. Type changes require leaf Blueprint + bAllowPotentialDataLoss. RenameLocalVariable uses FunctionName/VariableName/NewVariableName. GetAssetBlueprint Component accepts ComponentName/PropertyPaths/Baseline (ClassDefault|ParentTemplate); ResetComponentPropertyToDefault uses explicit baseline (default ParentTemplate). CompileBlueprint/manage return CompilerMessages; errors prevent saving. GetAssetExportOptions discovers formats; ExportAsset bValidateOnly never writes and ConflictPolicy=Fail|Unique|Replace covers the primary path (sidecars are exporter-specific).

Blueprint class variables use ClassReference<BaseClass> or Array<ClassReference<BaseClass>>; raw UClass object pins are rejected. Reference-value previews accept object/class references while traversal stays within owned subobjects. Navigation writes: GetEditorContext CurrentMap.Levels → ManageAssetLevel CreateNavMeshBoundsVolume with explicit BoundsMin/BoundsMax → save → ControlEditorScene BuildNavigation → GetEditorOperation. Legacy Skeleton retarget: ManageAssetAnimSequence/ManageAssetAnimBlueprint Action=Retarget, TargetSkeleton, bValidateOnly first; explicit AnimationRemaps and bAllowPotentialDataLoss for writes. Sequence reference recursion requires bRetargetReferredAssets; UE5 IK Retargeter is separate.

Editor domains use independent NexusLink implementations. NexusLink itself does not directly link or call official Toolset/ToolsetCore/ToolsetRegistry or import their Python packages. Keep transitive dependencies required by engine/domain plugins. GetProjectToolsets discovers NexusLink groups/actions. Blueprint GraphDSL/WriteGraphDSL uses UnrealGraphText node text (append-only); use WriteGraphPlan for structured edits.

## Input and diagnostics

Use InteractRuntimeInput for bounded local-player Key/InputAction injection; query GetRuntimeInput by its owned OperationId and release explicitly when needed. Discover exact PlayWorlds.WorldPath from ControlPie Status for multiplayer; world object names can repeat. GetRuntimePerformance reads latest process timings; GetProfilerCsv reads existing text captures under Saved. GetAssetUserWidget PropertyOverrides/Connections reports static evidence with uncertainty. Optional UE5.9+ GetCommonUIInfo discovers session Players then reads activation/focus/routing; ControlCommonUIDiagnostics captures bounded events. These are independent domain implementations.

EditorBusy (-32002) includes error.data.bExecuted=false: wait for readiness before resubmitting that rejected request. Timeout/disconnect is not proof of nonexecution and must not trigger automatic write replay.
