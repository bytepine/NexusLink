# NexusLink Tool Reference

This document lists all MCP tools and capabilities exposed by NexusLink with detailed parameter notes.

> **Tools vs capabilities**
> - **MCP tools (3)**: `search_capabilities`, `call_capability`, `submit_feedback` — call directly via MCP `tools/call`.
> - **Capabilities (all others)**: via `call_capability`: single `capability` + `arguments`; or batch `calls=[{capability,arguments?},...]`. Every `###` section below except meta-tools is a capability; the heading is the `capability` field value.
>
> Example (single): `call_capability(capability="list_runtime_actors", arguments={"classFilter":"BP_Enemy"})`

> **`search_capabilities` failure kinds** (check `errorKind`; do not rely on a single `error` string)
> - `not_found`: capability name not in registry
> - `disabled`: exact `capabilityName` / cap-name `query` hit a cap disabled in settings
> - `disabled_only`: fuzzy `query` matched only **disabled** caps (see `disabledCapabilities[]`)

> **General conventions**
> - All `assetPath` values are UE content paths, e.g. `/Game/Blueprints/BP_Player`
> - Property paths (`propertyPath`) support `A.B.C` dot drill-down + container indices `[i]` / `["key"]`:
>     - Arrays: `Items[0]`, `Matrix[0][1]`
>     - Maps: `Users["alice"].Score` (key compared to `ExportText`; string keys strip quotes)
>     - Sets: `Tags["Player.Ally"]`
>     - Combined: `MyComp.RelativeLocation.X`, `Users["alice"].Modules[2].Name`
>     - Segments split on `.`; map string keys **cannot contain `.`**
> - Filters support substring (`Player`), prefix (`^BP_`), suffix (`Actor$`), regex (`/^BP_.+$/`)
> - List tools support `offset` (default 0) and `limit` (default 100, max 500)
> - ★ marks required parameters
> - **manage finalize**: all `manage_asset_*` accept optional `saveToDisk` (default false); only `manage_asset_blueprint` / `manage_asset_anim_blueprint` / `manage_asset_user_widget` also accept `compile` (default false). UDS fields still auto-compile after edits; materials use `recompile` op. Standalone `save_asset` / `compile_blueprint` remain available
> - **Single target + cross-target batch (Breaking)**: capabilities handle one target (`assetPath` / `actorName` / `widgetName`); cross-target via `call_capability(calls=[{capability,arguments?},...])`. Within one target keep `sections` / `propertyPaths` / `operations` / `updates`. No `assetPaths`/`actorNames`/`widgetNames` or legacy keys (`blueprintPath`→`assetPath`, `newPath`→`destAssetPath`, `ownerWidget`→`ownerClass`, `filePath`→`scriptPath`, Lua `path`→`luaPath`, top-level `fields`/`rows`/`keys`/`widgets`→`operations`); legacy keys → `arg_invalid`
> - **Response default compaction (enabled globally)**: `NexusMcpDispatcher` recursively scans object-array fields `K` in `structuredContent` before serialization. Only scalar fields (string / number / bool / null) present on **every** object entry are candidates; when the dominant value meets thresholds (`MinCount=2` / `MinMatchRatio=0.7` / `MinNetSaveBytes=20`), write `<K>_defaults` at the same level and omit equal fields from entries. Sparse fields (only some entries) are not extracted. Identity fields (`name` / `path` / `assetPath` / `nodeId` / `tag` / `message` / `timestamp` / `frame` / `id` / `label` / `title` / `text` / `error`) never enter defaults. ForcedDefault: typed `search_asset` extracts `assetType`; `get_output_log` with `verbosity≠all` extracts floor; filters like `categoryFilter` / `list_runtime_actors.classFilter` / `list_runtime_widgets.classFilter` extract only when **all entries on the page agree** (N=1 included; do not treat filter substrings as defaults).
>
>   **Merge rule**: `merged = {**defaults, **entry}`; missing entry fields equal defaults.
>
>   **Merge / skip**: when `<K>_defaults` already exists, merge new keys without overwriting ForcedDefault; skip field names ending in `_defaults` or equal to `content`.
>
>   Toggle: Editor → Editor Preferences → Plugins → NexusLink → `Compact response defaults`.

---
