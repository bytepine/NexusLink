NexusLink MCP: Unreal Editor + runtime control (**MultiTool mode**). Registered extension fragments are appended after this text.

GAS: GetRuntimeActorAbilitySystem adds Context/Tags/ActivationDiagnostics/ActivationEvents. Diagnostic sections require ActorName; select a skill by SpecHandle or AbilityPath. InteractRuntimeActorAbilitySystem adds TryActivateGrantedAbility (no grant) and Start/StopActivationCapture. Capture on each endpoint before activation. bRequestAccepted is not authority confirmation; inspect authority Committed/Failed events. Dropped records, unknown identities and timeouts mean unconfirmed. See docs/gas-retest.md.

> **Host note**: **MCP runs in the Editor binary, and in Development/DebugGame standalone Game/DS builds** (`NexusLink` module is `Type: Runtime`; `NexusLinkEditor` is `Type: Editor`; Shipping strips the server at compile time).

## Tool model

`tools/list` exposes **all enabled capabilities** as separate MCP tools (name = capability name).

- **Direct** `tools/call` — **do not** wrap with `CallCapability(Capability=...)`.
- This mode has **no** `SearchCapabilities`; rely on each tool's `description` / `InputSchema` (includes `[see: ...]`).
- Meta-tool: `SubmitFeedback`.

## First action (required)

When connected to UE: **call MCP first** before answering blueprint/Widget/material/asset questions. Flow: `SearchAsset` (top-level `Assets`; typed `RecommendedGet` at top) → then read/write.

## Naming & read/write (same as SearchMode)

- **Set** — only `*_property` (e.g. `SetRuntimeActorProperty`).
- **Interact** — `Action` commands (`InteractRuntimeWidget`, `InteractRuntimeActorAnimation`).
- **Manage** — only `*_asset_*` structural edits.
- **Forbidden**: `ManageAnimation`, `SetRuntimeActorAnimation`.

## Decision rules

1. **Read vs write**: read-only `get/list/search`; `manage/set/create/delete` write assets/properties; **`interact_*` writes runtime commands**. Write success has no `success:true`; check MCP `isError` and structured operation/section/compile/save errors. Partial single-call edits may remain; do not blindly retry writes.
2. **Asset vs runtime**: `*_asset_*` = editor disk; `*_runtime_*` needs PIE.
3. **Single target**: each tool uses `AssetPath`/`ActorName`/`WidgetName`; `InteractRuntimeWidget` also accepts exact `WidgetPath` from `ListRuntimeWidgets` → `Path`. Ambiguous names return `Candidates[].Path` without interacting; detached/hidden widgets are rejected. Cross-Target = multiple `tools/call` (no `CallCapability.calls[]` in this mode). Within one target use `Sections`/`PropertyPaths`/`Operations`/`Updates`. **SearchAsset must be narrowed**; prefer returned `RecommendedGet` / `RecommendedManage` (typed at top) + `assets[].path`. Lists with `<k>_defaults`: missing fields equal that value (`merged={**defaults,**entry}`).
4. **Texture/mesh/animation/audio/VFX/level asset get/manage pairs** (fallback without `recommended*`): read `GetAssetTexture` / `ManageAssetTexture`; `GetAssetStaticMesh` / `ManageAssetStaticMesh`; `GetAssetSkeletalMesh` / `ManageAssetSkeletalMesh`; `GetAssetAnimSequence` / `ManageAssetAnimSequence`; `GetAssetSkeleton` / `ManageAssetSkeleton`; `GetAssetSoundWave` / `ManageAssetSoundWave`; `GetAssetSoundCue` / `ManageAssetSoundCue`; `GetAssetLevel` / `ManageAssetLevel` (`EditorOnly`); `GetAssetLevelSequence` / `ManageAssetLevelSequence` / `CreateAssetLevelSequence`; `GetAssetPhysicalMaterial` / `ManageAssetPhysicalMaterial` / `CreateAssetPhysicalMaterial`; `GetAssetStringTable` / `ManageAssetStringTable` / `CreateAssetStringTable`; `GetAssetFont` / `ManageAssetFont` (TTF via `ReimportAsset`); `GetAssetFoliageType` / `ManageAssetFoliageType` / `CreateAssetFoliageType`; `GetAssetMediaSource` / `ManageAssetMediaSource` / `CreateAssetMediaSource`. Gated caps (Niagara / GAS / StateTree / MVVM / IK / PCG / ControlRig / Paper2D / GeometryCollection / CommonUI / MoviePipeline / Python `GetPythonApi` / `ExecPython`) follow `tools/list`; skip if absent. PIE actors: `ListRuntimeActors`; PIE animation read `GetRuntimeActorAnimation`, write `InteractRuntimeActorAnimation`; audio/Niagara/AI via `InteractRuntimeActorAudio` / `_niagara` / `_ai`.
5. **GAS PIE** (needs GameplayAbilities; skip if absent from `tools/list`): read-only `GetRuntimeActorAbilitySystem`; activate/apply/give/cue/loose tags via `InteractRuntimeActorAbilitySystem`.
6. **Editor / Game capture**: `GetEditorContext` (selection, Content Browser path, `CurrentMap` for the open editor level); `SearchConsoleVariables`; `CaptureViewport` captures Game/PIE viewport (`Target=pie`) or the top-level window (`Editor` / `EditorDesktop`); LevelEditor panel tabs and Actor `ViewAngle` shots need `CaptureEditorPanel` (Editor only, `Target='list'` enumerates panels). Tag referencers via `GetGameplayTags` (`Referencers` + `Tag`). Logs: `GetOutputLog` (`Watch` then `bCollectWatch=true` for the next action; `Preset=diagnose` for errors) / `SetLogCaptureFilter` (available in Editor and standalone Game). Wait for `bHasPlayWorld` before `list_runtime_*`.

## Parameter contract (Breaking)

Same as SearchMode: no `assetPaths`/`actorNames`/`widgetNames`; manage only `operations[]`; get only `PropertyPaths[]`; `newPath`→`DestAssetPath`, `blueprintPath`→`AssetPath`, `ownerWidget`→`OwnerClass`, `FilePath`→`ScriptPath`, `classPath`→`ClassName`. Legacy keys → `ArgInvalid`.

## Blueprint / GAS

Same as SearchMode: `GetAssetBlueprint` before graph edits; GAS graphs via `ManageAssetBlueprint`; behavior trees save with `bSaveToDisk` or `SaveAsset` (`ReplaceNode` for type changes, `SyncGraph` for graph drift). manage optional `bSaveToDisk`; BP/ABP/WBP may pass `bCompile`.

## Hard rules

- Arguments must match `InputSchema`; required strings must be non-empty.
- **`GetRuntimeActorProperty` requires non-empty `ActorName`** — call `ListRuntimeActors` first.
- **`ExecCommand` requires non-empty `Command`**. Dangerous caps in Confirm mode require `Reason`; `UserDenied` → do not retry.
- Unsure whether an `unreal.*` API exists on this engine version: call `GetPythonApi` first; do not guess UE5 APIs on UE4 (or vice versa).
- `ExecPython` writes are only undoable via `obj.set_editor_property(...)` or an explicit `obj.modify()`; plain `obj.foo = x` is **not** recorded. Check the returned `bUndoRecorded` — `false` means Ctrl+Z will not roll this back.
- Before `get/manage_asset_*`, call `SearchAsset`; prefer returned `RecommendedGet` / `RecommendedManage`.
- Reuse an existing `Sections=["All"]` response when still current. Re-read after writes or runtime changes; `RedundantCall` is advisory and fresh data is still returned.
- `SubmitFeedback` triggers same as SearchMode; **`FeedbackHint` is mandatory**.
