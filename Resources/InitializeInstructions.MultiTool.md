NexusLink MCP: Unreal Editor + runtime control (**MultiTool mode**). Registered extension fragments are appended after this text.

GAS: GetRuntimeActorAbilitySystem adds Context/Tags/ActivationDiagnostics/ActivationEvents. Diagnostic sections require ActorName; select a skill by SpecHandle or AbilityPath. InteractRuntimeActorAbilitySystem adds TryActivateGrantedAbility (no grant) and Start/StopActivationCapture. Capture on each endpoint before activation. bRequestAccepted is not authority confirmation; inspect authority Committed/Failed events. Dropped records, unknown identities and timeouts mean unconfirmed. See docs/gas-retest.md.

> **Host note**: **MCP runs in the Editor binary, and in Development/DebugGame standalone Game/DS builds** (`NexusLink` module is `Type: Runtime`; `NexusLinkEditor` is `Type: Editor`; Shipping strips the server at compile time).

## Tool model

`GetEditorInfo` is Editor-only. After switching to Game/DS, use instance discovery metadata (projectName/engineVersion/hostKind/netRole/hasPlayWorld) or the host `/status` endpoint for identity; do not reuse Editor-only calls on that process.

`tools/list` exposes **all enabled capabilities** as separate MCP tools (name = capability name).

- **Direct** `tools/call` — **do not** wrap with `CallCapability(Capability=...)`.
- This mode has **no** `SearchCapabilities`; rely on each tool's `description` / `InputSchema` (includes `[see: ...]`).
- Meta-tool: `SubmitFeedback`.

## First action (required)

When connected to UE: **call MCP first** before answering blueprint/Widget/material/asset questions. Flow: `SearchAsset` (top-level `Assets`; typed `RecommendedGet` at top) → then read/write.

## Naming & read/write (same as SearchMode)

- **Set** — only `*Property` (e.g. `SetRuntimeActorProperty`).
- **Interact** — `Action` commands (`InteractRuntimeWidget`, `InteractRuntimeActorAnimation`).
- **Manage** — only `*Asset*` structural edits.
- **Forbidden**: `ManageAnimation`, `SetRuntimeActorAnimation`.

## Decision rules

1. **Read vs write**: read-only `get/list/search`; `manage/set/create/delete` write assets/properties; **`Interact*` writes runtime commands**. Write success has no `success:true`; check MCP `isError` and structured operation/section/compile/save errors. Partial single-call edits may remain; do not blindly retry writes.
2. **Asset vs runtime**: `*Asset*` = editor disk; `*Runtime*` needs PIE.
3. **Single target**: each tool uses `AssetPath`/`ActorName`/`WidgetName`; `InteractRuntimeWidget` also accepts exact `WidgetPath` from `ListRuntimeWidgets` → `Path`. Ambiguous names return `Candidates[].Path` without interacting; detached/hidden widgets are rejected. Cross-Target = multiple `tools/call` (no `CallCapability.Calls[]` in this mode). Within one target use `Sections`/`PropertyPaths`/`Operations`/`Updates`. **SearchAsset must be narrowed**; prefer returned `RecommendedGet` / `RecommendedManage` (typed at top) + `Assets[].Path`. Lists with `<k>_defaults`: missing fields equal that value (`merged={**defaults,**entry}`).
4. **Texture/mesh/animation/audio/VFX/level asset get/manage pairs** (fallback without `recommended*`): read `GetAssetTexture` / `ManageAssetTexture`; `GetAssetStaticMesh` / `ManageAssetStaticMesh`; `GetAssetSkeletalMesh` / `ManageAssetSkeletalMesh`; `GetAssetAnimSequence` / `ManageAssetAnimSequence`; `GetAssetSkeleton` / `ManageAssetSkeleton`; `GetAssetSoundWave` / `ManageAssetSoundWave`; `GetAssetSoundCue` / `ManageAssetSoundCue`; `GetAssetLevel` / `ManageAssetLevel` (`EditorOnly`); `GetAssetLevelSequence` / `ManageAssetLevelSequence` / `CreateAssetLevelSequence`; `GetAssetPhysicalMaterial` / `ManageAssetPhysicalMaterial` / `CreateAssetPhysicalMaterial`; `GetAssetStringTable` / `ManageAssetStringTable` / `CreateAssetStringTable`; `GetAssetFont` / `ManageAssetFont` (TTF via `ReimportAsset`); `GetAssetFoliageType` / `ManageAssetFoliageType` / `CreateAssetFoliageType`; `GetAssetMediaSource` / `ManageAssetMediaSource` / `CreateAssetMediaSource`. Gated caps (Niagara / GAS / StateTree / MVVM / IK / PCG / ControlRig / Paper2D / GeometryCollection / CommonUI / MoviePipeline / Python `GetPythonApi` / `ExecPython`) follow `tools/list`; skip if absent. PIE actors: `ListRuntimeActors`; PIE animation read `GetRuntimeActorAnimation`, write `InteractRuntimeActorAnimation`; audio/Niagara/AI via `InteractRuntimeActorAudio` / `_niagara` / `_ai`.
5. **GAS PIE** (needs GameplayAbilities; skip if absent from `tools/list`): read-only `GetRuntimeActorAbilitySystem`; activate/apply/give/cue/loose tags via `InteractRuntimeActorAbilitySystem`.
6. **Editor / Game capture**: `GetEditorContext` (selection, Content Browser path, `CurrentMap` for the open editor level); `SearchConsoleVariables`; `CaptureViewport` captures Game/PIE viewport (`Target=pie`) or the top-level window (`Editor` / `EditorDesktop`); LevelEditor panel tabs and Actor `ViewAngle` shots need `CaptureEditorPanel` (Editor only, `Target='list'` enumerates panels). Tag referencers via `GetGameplayTags` (`Referencers` + `Tag`). Logs: `GetOutputLog` (`Watch` then `bCollectWatch=true` for the next action; `Preset=Diagnose` for errors) / `SetLogCaptureFilter` (available in Editor and standalone Game). Wait for `bHasPlayWorld` before `ListRuntime*`.

## Parameter contract (Breaking)

Same as SearchMode: no `assetPaths`/`actorNames`/`widgetNames`; manage only `Operations[]`; get only `PropertyPaths[]`; `newPath`→`DestAssetPath`, `blueprintPath`→`AssetPath`, `ownerWidget`→`OwnerClass`, `FilePath`→`ScriptPath`, `classPath`→`ClassName`. Legacy keys → `ArgInvalid`.

## Blueprint / GAS

Same as SearchMode: `GetAssetBlueprint` before graph edits; GAS graphs via `ManageAssetBlueprint`; behavior trees save with `bSaveToDisk` or `SaveAsset` (`ReplaceNode` for type changes, `SyncGraph` for graph drift). manage optional `bSaveToDisk`; BP/ABP/WBP may pass `bCompile`.

## Hard rules

- Arguments must match `InputSchema`; required strings must be non-empty.
- **`GetRuntimeActorProperty` requires non-empty `ActorName`** — call `ListRuntimeActors` first.
- **`ExecCommand` requires non-empty `Command`**. Dangerous caps in Confirm mode require `Reason`; `UserDenied` → do not retry.
- Unsure whether an `unreal.*` API exists on this engine version: call `GetPythonApi` first; do not guess UE5 APIs on UE4 (or vice versa).
- `ExecPython` writes are only undoable via `obj.set_editor_property(...)` or an explicit `obj.modify()`; plain `obj.foo = x` is **not** recorded. Check the returned `bUndoRecorded` — `false` means Ctrl+Z will not roll this back.
- Before `GetAsset*/ManageAsset*`, call `SearchAsset`; prefer returned `RecommendedGet` / `RecommendedManage`.
- Reuse an existing `Sections=["All"]` response when still current. Re-read after writes or runtime changes; `RedundantCall` is advisory and fresh data is still returned.
- `SubmitFeedback` triggers same as SearchMode; **`FeedbackHint` is mandatory**.

Editor UI: open/focus an asset via ControlEditorAssetEditor (Open/Focus/Close need GUI rendering; Close rejects dirty/shared editors, save explicitly first; list via GetEditorContext Sections=[AssetEditors]; Status/bValidateOnly work headless); maps use ControlEditorLevel. Slate: GetEditorSlate → Ref → InteractEditorSlate / CaptureEditorSlate. Local tests: ControlEditorAutomation / ListEditorAutomationTests. Compile: CompileEditorLiveCoding. Long tasks return OperationId; poll GetEditorOperation (optional WaitSeconds ≤30). Completed is execution state; inspect Outcome separately. Request cancellation only cancels waiting. Explicit Stop applies only where supported. Never retry writes automatically or mix these operations into atomic Calls[] batches. Search exposes outputSchema and Execution backend information.

Reparent normal/Widget/Anim/GameplayAbility Blueprints with ManageAssetBlueprint Operations=[{Action:Reparent,ParentClass:...}]; bCompile/bSaveToDisk remain explicit. Leaving the parent hierarchy requires operation bAllowPotentialDataLoss=true. Read-only navigation: GetNavigationPath(Start={X,Y,Z},End={X,Y,Z},World=Auto|Editor|Play); ordinary ActorName is optional, no AIController/movement/nav rebuild. Check bNavigationAvailable, bPathFound and bPartial; MaxPoints only bounds returned points. GetNavigationPoint projects one point. Path queries accept FilterClass, AgentRadius/AgentHeight, ProjectionExtent and bProjectStart/bProjectEnd; WorldName disambiguates PIE worlds. Align MoveTo FilterClass/bProjectEnd/bAllowPartialPath explicitly. GetRuntimeActorAI reports tracked completion; InteractRuntimeActorAI Stop stops movement, request acceptance alone is not arrival.

Asset edit additions: GetAssetEditPreview supports Reparent/ChangeVariableType and scalar DataAsset Set, Level SetActorProperty, BP SetComponentProperty. Type changes require leaf Blueprint + bAllowPotentialDataLoss. RenameLocalVariable uses FunctionName/VariableName/NewVariableName. GetAssetBlueprint Component accepts ComponentName/PropertyPaths/Baseline (ClassDefault|ParentTemplate); ResetComponentPropertyToDefault uses explicit baseline (default ParentTemplate). CompileBlueprint/manage return CompilerMessages; errors prevent saving. GetAssetExportOptions discovers formats; ExportAsset bValidateOnly never writes and ConflictPolicy=Fail|Unique|Replace covers the primary path (sidecars are exporter-specific).

Blueprint class variables use ClassReference<BaseClass> or Array<ClassReference<BaseClass>>; raw UClass object pins are rejected. Reference-value previews accept object/class references while traversal stays within owned subobjects. Navigation writes: GetEditorContext CurrentMap.Levels → ManageAssetLevel CreateNavMeshBoundsVolume with explicit BoundsMin/BoundsMax → save → ControlEditorScene BuildNavigation → GetEditorOperation. Legacy Skeleton retarget: ManageAssetAnimSequence/ManageAssetAnimBlueprint Action=Retarget, TargetSkeleton, bValidateOnly first; explicit AnimationRemaps and bAllowPotentialDataLoss for writes. Sequence reference recursion requires bRetargetReferredAssets; UE5 IK Retargeter is separate.

Editor domains use independent NexusLink implementations. NexusLink itself does not directly link or call official Toolset/ToolsetCore/ToolsetRegistry or import their Python packages. Keep transitive dependencies required by engine/domain plugins. GetProjectToolsets discovers NexusLink groups/actions. Blueprint GraphDSL/WriteGraphDSL uses UnrealGraphText node text (append-only); use WriteGraphPlan for structured edits.

## Input and diagnostics

Use InteractRuntimeInput for bounded local-player Key/InputAction injection; query GetRuntimeInput by its owned OperationId and release explicitly when needed. Discover exact PlayWorlds.WorldPath from ControlPie Status for multiplayer; world object names can repeat. GetRuntimePerformance reads latest process timings; GetProfilerCsv reads existing text captures under Saved. GetAssetUserWidget PropertyOverrides/Connections reports static evidence with uncertainty. Optional UE5.9+ GetCommonUIInfo discovers session Players then reads activation/focus/routing; ControlCommonUIDiagnostics captures bounded events. These are independent domain implementations.

EditorBusy (-32002) includes error.data.bExecuted=false: wait for readiness before resubmitting that rejected request. Timeout/disconnect is not proof of nonexecution and must not trigger automatic write replay.
