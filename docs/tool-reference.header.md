# NexusLink Tool Reference

**Language / Language**: **English** · [简体中文](tool-reference.zh.md)

This document lists all MCP tools and capabilities exposed by NexusLink with detailed parameter notes.

> **Tools vs capabilities**
> - **MCP tools (3)**: `SearchCapabilities`, `CallCapability`, `SubmitFeedback` — call directly via MCP `tools/call`.
> - **Capabilities (all others)**: via `CallCapability`: single `Capability` + `Arguments`; or batch `Calls=[{capability,arguments?},...]`. Every `###` section below except meta-tools is a capability; the heading is the `Capability` field value.
>
> Example (single): `CallCapability(Capability="ListRuntimeActors", Arguments={"ClassFilter":"BP_Enemy"})`

> **`SearchCapabilities` failure kinds** (check `ErrorKind`; do not rely on a single `Error` string)
> - `NotFound`: capability name not in registry
> - `disabled`: exact `CapabilityName` / cap-name `Query` hit a cap disabled in settings
> - `DisabledOnly`: fuzzy `Query` matched only **disabled** caps (see `DisabledCapabilities[]`)
> - `CallCapability` also uses `UserDenied` when the editor user rejects a dangerous cap (do not retry)

> **General conventions**
> - MCP / JSON-RPC standard keys retain their standard spelling (`capabilities`, `instructions`, `inputSchema`, `annotations`, `code`, `message`); PascalCase applies to capability payloads.
> - Operation, section, compile and save errors return `isError=true` with structured details. `Calls[]` (2+ items) checks all operations and automatic compiles before automatic saves; inspect `bRolledBack` / `UndoNote`. Disk writes and explicit package/runtime side effects are not atomic or reverted. A single manage call retains successful operations after partial failure.
> - All `AssetPath` values are UE content paths, e.g. `/Game/Blueprints/BP_Player`
> - Property paths (`PropertyPath`) support `A.B.C` dot drill-down + container indices `[i]` / `["Key"]`:
>     - Arrays: `Items[0]`, `Matrix[0][1]`
>     - Maps: `Users["alice"].Score` (key compared to `ExportText`; string keys strip quotes)
>     - Sets: `Tags["Player.Ally"]`
>     - Combined: `MyComp.RelativeLocation.X`, `Users["alice"].Modules[2].Name`
>     - Segments split on `.`; map string keys **cannot contain `.`**
> - Filters support substring (`Player`), prefix (`^BP_`), suffix (`Actor$`), regex (`/^BP_.+$/`)
> - List tools support `Offset` (default 0) and `Limit` (default 100, max 500)
> - ★ marks required parameters
> - **manage finalize**: all `manage_asset_*` accept optional `bSaveToDisk` (default false); only `ManageAssetBlueprint` / `ManageAssetAnimBlueprint` / `ManageAssetUserWidget` also accept `bCompile` (default false). UDS fields still auto-compile after edits; materials use `Recompile` op. Standalone `SaveAsset` / `CompileBlueprint` remain available
> - **Single target + cross-target batch (Breaking)**: capabilities handle one target (`AssetPath` / `ActorName` / `WidgetName`); cross-target via `CallCapability(Calls=[{capability,arguments?},...])`. Within one target keep `Sections` / `PropertyPaths` / `Operations` / `Updates`. No `assetPaths`/`actorNames`/`widgetNames` or legacy keys (`blueprintPath`→`AssetPath`, `newPath`→`DestAssetPath`, `ownerWidget`→`OwnerClass`, `FilePath`→`ScriptPath`, top-level `Fields`/`Rows`/`Keys`/`Widgets`→`Operations`); legacy keys → `ArgInvalid`
> - **Response default compaction (enabled globally)**: `NexusMcpDispatcher` recursively scans object-array fields `K` in `StructuredContent` before serialization. Only scalar fields (string / number / bool / null) present on **every** object entry are candidates; when the dominant value meets thresholds (`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`), write `<K>_defaults` at the same level and omit equal fields from entries. Sparse fields (only some entries) are not extracted. Identity fields (`name` / `Path` / `AssetPath` / `NodeId` / `Tag` / `Message` / `Timestamp` / `frame` / `Id` / `Label` / `Title` / `text` / `Error`) never enter defaults. ForcedDefault: typed `SearchAsset` extracts `AssetType`; `GetOutputLog` with `verbosity≠all` extracts floor; filters like `CategoryFilter` / `ListRuntimeActors.ClassFilter` / `ListRuntimeWidgets.ClassFilter` extract only when **all entries on the page agree** (N=1 included; do not treat filter substrings as defaults).
>
>   **Merge rule**: `merged = {**defaults, **entry}`; missing entry fields equal defaults.
>
>   **Merge / skip**: when `<K>_defaults` already exists, merge new keys without overwriting ForcedDefault; skip field names ending in `_defaults` or equal to `Content`.
>
>   Toggle: Editor → Editor Preferences → Plugins → NexusLink → `Compact response defaults`.

---
