# Changelog — NexusLink

**语言 / Language**: [简体中文](CHANGELOG.md) · **English**

All changes follow [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Changed

- Clarify uncertain write outcomes after proxy disconnections/timeouts, no automatic resend, cache invalidation, and existing long-operation polling/cancellation boundaries; link the sample's controlled UE 5.7 validation entry point and its evidence scope.

## [2.3.6] - 2026-11-13

### Added

- Scene diagnostics cover navigation state, bounded waits, spatial contributors, Recast tiles, BSP, and component physics state. Ray queries support complex collision and return components, hit faces, and positions.
- Static meshes expose navigation collision, section collision, collision LOD, and bounded triangle snapshots, with component physics refresh and mesh collision rebuilds.
- ControlEditorLevel can load, show, hide, and switch existing sublevels; ManageAssetLevel can resize navigation volumes.
- CallCapability supports read-only asynchronous batches of up to 64 items across frames, with progress queries, deadlines, and cancellation. Synchronous read-only batches do not create editor transactions.
- InteractRuntimeInput / GetRuntimeInput inject keys or Enhanced Input Actions for local players in a specified Play World, with timed and explicit release.
- GetRuntimePerformance exposes frame and thread timings; GetProfilerCsv supports bounded CSV snapshots, frame-range aggregation, metric sorting, and event queries.
- GetAssetUserWidget diagnoses property overrides and binding connections; CommonUI tools on UE 5.9+ expose activation trees, focus, routing, and bounded event capture.
- Blueprint migration supports custom-event naming, same-type inherited-member remapping, validation-only execution, and nested-default diagnostics. Array writes validate types, integer truncation, and numeric ranges.
- GetAssetLevel/ActorPropertySchema lists instance-owned subobjects and struct fields; exports include bounded warnings, errors, and persistent-level BSP counts.
- GetEditorGraph / ControlEditorGraph support graph discovery, layout reads, opening, navigation, node selection, and zoom. UE 5.8+ PCG provides separate main/embedded-subgraph documents; queries do not open targets automatically.
- Blueprint-specific operations split/recombine struct pins and copy bounded node sets, preserving supported numeric precision and current subvalues while returning layouts, comments, pin hierarchies, and node-ID mappings.
- Slate supports filtered snapshots, parent widgets and dimensions, right/middle clicks, double-clicks, wheel input, modifiers, in-widget positioning, and dragging under mouse capture; text inputs support replacement.

### Changed

- **BREAKING**: Blueprint GraphDSL / WriteGraphDSL use UnrealGraphText and no longer accept legacy Toolset S-expressions; structured editing uses WriteGraphPlan.
- **BREAKING**: GetProjectSkills.Details accepts only SKILL.md paths returned by List in SkillPath, no longer AgentSkill class paths.
- **BREAKING**: GetProjectToolsets returns NexusLink-owned capability groups and action schemas; old external Registry names are no longer queryable. See the [migration guide](https://github.com/bytepine/NexusLink/blob/nexus-link-v2.3.6/docs/toolset-backends.md) for these changes.
- **BREAKING**: Synchronous and asynchronous CallCapability.Calls batches both require 1–64 items; split larger batches.
- Editor and domain tools call engine APIs directly and report Backend=Independent. Prefer dedicated graph tools, with Slate as the UI fallback. Semantic search uses the optional SemanticSearch service.
- Only arbitrary-code-execution entry points are marked Dangerous; dedicated capabilities previously disabled solely because of that tag are re-enabled.
- SearchCapabilities queries live catalog and enablement state, offering narrowing suggestions for overly broad queries.

### Fixed

- Fix missing-symbol Linux Game / DS linking failures caused by MCP tag constants under C++14.
- Reject ordinary requests with EditorBusy while modal windows or slow tasks are active; UE 5.9 still responds to ping and status requests.
- Fix Actor read scope for streaming levels, missing default-subobject handling, and transform/navigation updates after property writes.
- Correct navigation-volume dimensions; return StateConflict for dirty levels or busy navigation.
- Preflight read-only files and file locks before saving; preserve original files and dirty flags on failure. Save maps natively and validate function parent references.
- Fix UE 4.26 animation-parameter import, legacy Sequencer blending, UE 4.26–5.1 skeletal sampling, and UE4 BlendSpace1D read/write compatibility.
- Animation Blueprints locate nested state graphs by full path; a missing requested Play World returns available world paths.
- Fix editor shutdown caused by undo references during Widget Blueprint compilation and temporary-world collection.
- Assemble UTF-8 WebSocket fragments correctly, avoiding incomplete or timed-out large UE4 requests.

## [2.3.4] - 2026-11-10

### Added

- ManageAssetBlueprint supports variable ToolTip, Replication/RepNotify, parameterless CallInEditor functions, ClassReference<BaseClass>, and container types. Read back configuration and diagnose dangerous class references; RepNotify uses existing notification functions.
- ManageAssetLevel creates NavMesh Bounds Volumes; GetEditorContext/CurrentMap returns level state, scene bounds, and navigation volumes; ControlEditorScene/BuildNavigation supports asynchronous navigation building and saving.
- ManageAssetAnimSequence and ManageAssetAnimBlueprint support legacy Skeleton retargeting, validation-only preflight, animation-reference remapping, and linked-sequence protection, returning affected assets and missing bones.

### Fixed

- Fix SCS/inherited-component default comparison and restoration for level Actors, and whole-batch read failures caused by null references.
- Improve batch Blueprint-variable category writes, correct category readback and instance-editable flags, and make existing subclasses/instances inherit new defaults while preserving overrides.
- Route FindAsset to SearchAsset; GetAsset suggests typed asset tools. Add actor collision keywords and GraphDSL guidance for older engines.
- Property previews, ExpectedValue, and post-write verification support object/class references while preserving type constraints and external-reference write protection.
- DeleteAsset diagnoses references, Undo, editor usage, and read-only files, reporting partial deletion accurately.
- Keep proxy-error summaries and detailed counts consistent in feedback reports.
- Fix mixed Slate/Editor screenshots deleting newly created files and losing PNG/JPEG images or inline batch attachments.

### Changed

- Installation packages no longer include Python retest/performance tools under tools/. Full C++ source remains included; obtain command-line tools from the corresponding source release.

## [2.3.3] - 2026-11-07

### Added

- ManageAssetBlueprint reparents ordinary, UMG, animation, and GameplayAbility Blueprints with compatibility previews, Undo, and type/skeleton/member-conflict protection. Rename member/local variables, change types under protection while preserving GUIDs, read complete container types, compare inherited-component overrides, and restore defaults.
- GetAssetLevel/ActorProperties compares level Actor properties against class defaults read-only and reuses loaded maps.
- GetNavigationPath and GetNavigationPoint query existing navigation paths and projection read-only, with Agent, Filter, exact world selection, complete/partial paths, bounded points, path length, and cost.
- GetRuntimeActorAI queries movement state and request completion; InteractRuntimeActorAI supports MoveTo/Stop, Filter, projection, partial paths, and acceptance radius, distinguishing request acceptance from arrival.
- ControlEditorAssetEditor opens, focuses, queries, and safely closes asset editors, refusing unsaved assets or shared windows. GetEditorContext/AssetEditors lists assets, editor types, and dirty state.
- GetAssetEditPreview adds reparenting and variable-type preflight; ExpectedValue / bVerify extend to DataAssets, level Actors, and Blueprint-component properties.
- GetAssetExportOptions discovers exporters and formats. Export supports validation-only and Fail/Unique/Replace collision policies; validation-only creates no directories or files.

### Fixed

- Dispatch HTTP/WebSocket requests at frame boundaries to prevent Blueprint-editing reentrancy. Compile Blueprints centrally on demand while preserving Undo, report graph/node locations for compile errors, and block saving after compilation failure.
- Blueprint-component reads preserve inherited overrides. Scalar previews and verification support owned subobjects and explicitly reject unsupported external references.
- Fix UE 5.5+ property-text reads returning empty strings for defaults such as false and 0.
- Reject invalid property values and trailing text, preserving scientific notation and unspecified struct fields; fix dotted container keys and nested level-property reads/writes.
- Choose exporters by extension, run automated tasks, and return errors without interactive dialogs.
- Fix default-object name-collision crashes when Reset follows a project-settings write.
- Fix feedback-report category trends, average hit counts, Other comments, and error-reproduction selection; attach redacted argument summaries to slow calls.
- Fix build compatibility for Linux Clang, UE5.8, and non-editor targets including iOS.
- Support Android API 23 network-adapter enumeration and use application-writable directories for Android/iOS authentication tokens.
- Disable MCP services and related initialization at compile time in Shipping builds.

## [2.3.2] - 2026-11-05

### Added

- Add pure C++/Blueprint UNexusLinkSettings::IsMcpEnabled() to report actual MCP server state; return false before startup, after startup failure, and in Shipping builds.
- ControlEditorLevel opens editor maps by asset path. Refuse unsaved main/sublevels or external-Actor packages unless bDiscardUnsaved is explicit; never start PIE. Restore dirty flags for surviving main packages after load failure.
- SetRuntimeActorTransform uses SetActorLocation / SetActorRotation / SetActorScale3D and returns previous values. Validate all arguments before writing; reject nonnumeric, nonfinite, and malformed values without partial changes.
- GetAssetPackageChunks reads AssetRegistry pak chunk IDs; listing packages by chunk requires PathFilter.
- ManageAssetBlueprint adds CopyComponent and ReattachComponent. Copy actual inherited override templates; reject self/descendant reattachment while keeping root and complete component trees consistent. AddComponent.AttachTo can target parent Blueprint SCS or native C++ components.
- ManageAssetLevel adds ResetActorPropertyToDefault to restore level Actor properties from Blueprint CDO defaults with Undo.
- ListRuntimeWidgets adds ActorName to include widget trees of registered WidgetComponents on that Actor. Returned paths precisely target interaction/property access; interaction rejects hidden or unregistered components.

### Fixed

- SearchAsset resolves extension-registered type aliases such as abl_ability to loadable UClasses under the same capability instead of reporting Unknown.
- Use .umap when saving or deleting World packages.
- GetPythonApi automatically prefixes bare identifiers with unreal.
- GetAssetBlueprint component entries include template properties that differ from the CDO.
- Standardize PascalCase business fields, Blueprint Functions lists, coordinate/color components, and enums; fix path-echo deduplication and Call-category feedback summaries, updating invocation guidance and retest tools.

## [2.3.0] - 2026-11-03

### Added

- Editor automation supports Slate queries, screenshots, hovering, dragging, forms, and text interaction; Automation discovery, execution, and stopping; and nonblocking Live Coding. Long operations provide progress, owned task history, bounded waits, and backend-supported cancellation.
- Adapt UE 5.8 official Toolsets with action-level backend, availability, and parameter discovery; tool/schema queries for custom C++/Blueprint/Python Toolsets; asset-type-constrained semantic/similarity search; and AgentSkill queries. Preserve official enablement filters/errors and use compatible backends on older engines.
- Blueprint/UMG editing adds node discovery, connected subgraphs, layout, function parameters, GraphPlan, widget renaming, variables, events, UIComponent, and structural operations. MVVM supports nested properties, five binding directions, single-input conversion functions, compilation, and saving.
- Asset edit previews/readback provide scalar preflight, ExpectedValue, optional post-write verification, and bounded BP/UMG/PCG structural previews. Failed structural validation blocks automatic saving; asset source control provides read-only-state preflight.
- Support first import of images, WAV, and FBX. Protect existing animation assets per FBX take and return every imported product; skeletal-mesh imports require a single mesh node.
- Edit PCG parameters, instance overrides, subgraphs, and nodes; execute instances and sample point output, with compatible implementations for older engines.
- Advanced animation and Sequencer/Control Rig editing includes tracks, sections, keys, channel baking, Rig controls, animation layers/export, conditions, folders, FBX, Outliner, custom bindings, AnimMixer, and skeletal-pose sampling.
- Edit Niagara systems, emitters, renderers, dynamic inputs, Data Interfaces, User Variables, and Set Parameters, with deep diagnostics and explicit Stack Fix.
- Create PhysicsAssets and edit bodies, shapes, mass, and constraint limits; StaticMesh supports LODs, convex collision generation, and Nanite settings.
- Typed project-settings access, plugin dependencies/enablement, scene folders/selection/merge/Level Instances, INI source management and protected GameplayTags changes, plus read-only DataRegistry schemas, sources, and caches.
- Manage Game Features session lifecycles on UE 5.7+ with the host plugin enabled: query, activate, deactivate, wait for callbacks, and read back state.
- Configure multiplayer PIE topology; screenshots return standard MCP PNG/JPEG blocks with path-only mode, asynchronous retrieval, and batch attachments.
- GAS network diagnostics/retests cover ASC context, tags, activation diagnostics/events, explicit multi-endpoint cases, generic activation templates, offline baselines, and HTML/JSON/JUnit reports. BehaviorTree/Blackboard support condition changes, snapshot waits, restoration, and baseline retests.
- Read-only analysis of stopped Unreal Insights traces on UE 4.26+ includes frames, CPU timings, and bookmarks; offline comparisons under matching conditions support repeated-sample medians and performance budgets.
- Publish InputSchema compatibility baselines and checks detecting removed fields, narrowed types/enums, and newly required fields.

### Changed

- **BREAKING**: Capability IDs, inputs, outputs, action names, and tags use PascalCase, with b-prefixed booleans; legacy snake_case/camelCase names are rejected. Standard MCP/JSON-RPC envelope fields stay unchanged.
- Remove Win64/Mac module-platform allowlists from plugin descriptors; other platforms still depend on engine dependencies and source compatibility.

### Fixed

- UMG widget/animation add/remove/wrap operations compile correctly on UE 5.6+, preserving unchanged widget variable identities.
- Reject invalid Sequencer binding GUIDs such as all-zero values, avoiding engine ensures on negative requests.
- Runtime UMG interaction supports exact full-instance paths, returns candidates for ambiguous names, and excludes cached widgets removed from the viewport, hidden, or on inactive pages.
- UE5 Blueprint float defaults retain floating-point precision consistent with variable types; UE4 retains its original category.
- Section reads correctly parse explicit names and expand All.
- Batch calls aggregate business, compilation, and save errors, attempt in-memory rollback and skip autosave on failure, and explicitly report disk/runtime side-effect boundaries.
- DataTable writes accept dynamic Fields dictionaries with field validation and Undo; temporary rows are released correctly on failure.
- Reads always return current data; redundant-call hints no longer block reads, and writes clear read history.

## [2.3.0-beta.5] - 2026-10-29

> Pre-release; use outside production environments only.

### Added

- Advanced Sequencer / Control Rig editing through UE 5.8 official animation Toolsets: discover/edit tracks and sections, typed keys and channel baking, Rig controls/transforms, baking to Control Rig, animation layers/export, fixed action mappings, and on-demand native parameter schemas.
- Deep Niagara writes through UE 5.8 official Toolsets: system/emitter properties, renderer creation/edit/deletion, nested dynamic inputs and Data Interfaces, User Variables, Set Parameters, and corresponding schema/data readback.
- Game Features session lifecycle on UE 5.7+ with the plugin enabled: discover built-in Feature plugins, query/activate/deactivate, asynchronously wait for engine callbacks, read final state, and protect same-plugin concurrency without editing descriptors or unloading plugins.
- Custom Toolset discovery through UE 5.8 Registry: enabled C++/Blueprint/Python Toolsets, paged tools, native input/output schemas, and animation action mappings. Respect Registry filters/disablement, reflect registration changes immediately, and expose no arbitrary Toolset execution.
- PCG parameter editing through UE 5.8 official Toolsets: node settings/readback, graph parameter definition/removal, instance override/reset, and subgraph insertion. Preflight cycles/name collisions and retain owned asynchronous-operation queries.
- Detailed PhysicsAsset editing: create from SkeletalMesh, add/remove bodies, resize/transform/remove individual shapes, scale mass, edit angular constraints, and read back shape indices, geometry, mass, and limits.
- Manage project GameplayTags by adding, renaming, and deleting individual tags and reading Info/INI Sources. Save immediately through engine INI editing; deletion checks children/asset references, and dangerous operations do not support batches or transactional rollback.
- Read-only DataRegistry queries on UE 5.0+ with the plugin enabled: registered registries, schemas, item IDs, configured/runtime sources, and cached values for explicit items. Mark uncached items individually without implicit loads or Acquire.

### Changed

- Existing capabilities reuse UE 5.8 Registry synchronous backends when corresponding official plugins are enabled: settings/plugin lists, DataRegistry items/caches, protected tag writes, PhysicsAsset bodies/mass, PCG node deletion/connections, Sequencer display rate, Control Rig pins, Niagara emitter deletion, MVVM binding deletion, uniquely named StaticMesh material slots, and material compilation. Preserve synchronous results, filtering/paging, error protection, and older engine backends; official disablement/filtering never triggers compatibility retries.

### Fixed

- Fix cross-version PhysicsAsset compatibility: use UnrealEd generation on UE4, existing reference-skeleton fields for UE 4.26 bone validation, and legacy generation signatures on UE 5.3 and earlier.
- Fix DataRegistry source-path API compatibility on UE 5.0–5.4, reading existing soft references without loading additional DataTables.
- Fix creation of new GameplayTags INI sources on UE 4.26 using the engine's existing tag-writing API to create and persist them.

## [2.3.0-beta.4] - 2026-10-28

> Pre-release; use outside production environments only.

### Added

- Advanced Slate operations include hover, drag, dropdown selection, checkbox values, form filling, text replacement, and waiting for visible text, with form preflight, partial-failure positions, and protection against similarly named menu misclicks.
- UE 5.8 UMG structure operations include moving/reordering siblings, wrapping, template/single-child replacement, and filling empty named slots; read back roots, order, widget slots, and inherited slots.
- Adapt UE 5.8 official Toolsets for PCG instance execution/node sampling, deep Niagara diagnostics/explicit Stack Fix, asset-type-constrained semantic/similarity search, and registered AgentSkill list/details, preserving official business results, enablement filters, and plugin prerequisites.
- Read-only project-settings enumeration and explicit property reads, plugin information/dependencies/dependents, and asset mount ownership queries.
- All three screenshot types return standard MCP PNG/JPEG blocks by default, with path-only mode, asynchronous retrieval, and batch attachments. Bound image counts/bytes and retain omission reasons and FilePath.

- Complete MVVM binding distinguishes WBP, widgets, and differently named ViewModels of the same class. Support nested paths, five directions, enabled/compile flags, discoverable single-input conversions, context/field-path/conversion readback, BindingId deletion, compilation, and saving. Invalid paths/types/conversions do not create empty bindings; preserve legacy empty-binding calls and fix compilation failures from uninitialized GUIDs in old AddViewModel behavior.

### Fixed

- Fix lowercase viewport targets such as editor, same-second screenshot filename collisions, and MultiTool image attachments. Serialize image-bearing text after metadata processing to retain final TTL fields; capture attached Slate windows directly instead of offscreen repainting that triggers viewport ensures.
- PCG graph readback adds input/output endpoints and node paths. Connections choose real pins by default and validate failures; built-in endpoints cannot be deleted.

- Fix UE5 Blueprint float variables using legacy pin categories and compiling as int32, restoring float defaults and MVVM numeric-binding type consistency; retain UE4's original float category.

## [2.3.0-beta.3] - 2026-10-27

### Changed

- Align Chinese/English Unreal Engine MCP landing-page introductions, plugin positioning, and client links, and simplify display descriptions. Remove README Fab content to prevent product-page 403s from failing link checks.
- Remove PlatformAllowList / WhitelistPlatforms from Runtime/Editor modules instead of restricting descriptors to Win64/Mac; other platforms still depend on engine dependencies and source compatibility.

### Added

- Editor Slate queries/screenshots/input, Automation discovery/execution/stopping, and nonblocking Live Coding. UE 5.8 reuses allowlisted official Toolsets while older versions use compatible engine implementations. Long operations support session-owned queries, bounded waits, output-contract validation, and nontransactional-batch preflight.

### Fixed

- Fix ambiguous integer conversion in UE 5.2 Blackboard key-ID comparisons, restoring cross-version builds.

- InteractRuntimeWidget accepts full instance Paths from ListRuntimeWidgets as WidgetPath. Ambiguous names return candidate paths and refuse interaction. Exclude cached widgets removed from viewport/hierarchy, hidden, or on inactive Switcher pages; support nested UserWidgets in the current viewport.

## [2.3.0-beta.2] - 2026-10-26

> Pre-release; use outside production environments only.

### Added

- BehaviorTree/Blackboard bt_retest supports explicit Pawn/AIController, read-only preflight, condition changes, snapshot waits, restoration, last evidence on timeout, and domain-isolated baselines. Share GAS execution/reporting code while preserving existing CLI/import/report compatibility. Add Runtime Actor Context and BehaviorTree paths, running state, and typed inherited keys; SetBlackboard rejects incorrect scalar types.

- Same-condition performance reports in tools/trace_compare export stopped traces from explicit endpoints and compare offline HTML/JSON with repeated-sample medians. GetTrace uses one explicit time window for Frames/Timers, adds median complete-frame statistics, and returns CPU entry totals/truncation. Mismatched conditions, zero baselines, missing data, and unfinished analysis never produce false performance-change conclusions.

- GetAssetEditPreview reads scalar types, current values, and normalized targets for Blueprint SetDefaults, DataTable Set, and Widget SetProperty. Writes support ExpectedValue and bVerify, disabled by default; relocate/read after compilation, skip autosave on failure, and record Undo before CDO/Widget writes.

- Generic GAS GenericActivation templates use explicit JSON endpoint/Actor mappings, abilities, and authoritative events. Support Actors without PlayerState, recheck local ASC identity before every step, stop on replacement/map changes, and show custom failure-tag explanations only for actually observed tags.

- Publish InputSchema compatibility baselines/checks for removed fields, narrowed types/enums, and newly required inputs, supporting explicit host capability sets. Schema tests export to host Saved by default; update baselines explicitly only.

- GAS network diagnostics/retests expose ASC Context/Tags/ActivationDiagnostics/ActivationEvents, activate existing abilities on explicit targets, and capture timed events. tools/gas_retest executes multi-endpoint JSON steps, waits, and assertions with read-only preflight, explicit-endpoint templates, and HTML/JSON/JUnit reports separating case state, authoritative events, and unconfirmed results. Offline passing baselines compare new failures, recoveries, missing steps, and endpoint-event changes using case signatures and full before/after evidence; refuse to overwrite baseline/comparison directories. Example-project fixed-case retests reuse signature checks when manually launched. UE-free tool regressions run in GitHub CI; retain failure evidence and never retry writes.

### Fixed

- Return independent GAS event snapshots so response compression cannot mutate capture caches and remove ability identity on later reads; discover ASCs supplied through Pawn PlayerState components.

- Resolve explicit MultiSection names by registration, preserving case compatibility and expanding All correctly instead of lowercasing PascalCase into unknown sections.
- Restore existing field spellings for discovery, registry files, WebSocket authentication, and proxy handshakes, fixing released Desktop/Rider/VSCode clients that could not discover/connect to newer plugins. Capability business fields retain PascalCase.

## [2.3.0-beta.1] - 2026-10-23

> Pre-release; use outside production environments only.

### Added

- scripts/audit_cpp_naming.py reports type prefixes/filenames, b-prefixed booleans, k-prefixed constants, and PascalCase protocol fields without failing by default.
- Read-only Unreal Insights on UE 4.26+ Editors: ListTraces lists Saved/Profiling and local Trace Store; GetTrace reads stopped .utrace files with frame statistics, hitches, CPU timings, and bookmarks. Retry when bAnalysisComplete=false. UE4 Store streams without paths accept TraceId directly with empty Path/TracePath and no cache file. Adapt legacy types, CPU aggregation, and summary fields; ignore incomplete frames to avoid nonfinite durations/default windows. Never tail active files; support UE 5.8 timer and Trace Store smart-pointer APIs.

### Changed

- **BREAKING**: Capability IDs, inputs, outputs, action names, and tags become PascalCase. Prefix booleans with b (saveToDisk → bSaveToDisk); reject legacy snake_case/camelCase names. Standard MCP envelope fields (jsonrpc, method, params, isError, structuredContent) remain unchanged.
- Align private Capability class names with filenames: FNexus plus the extensionless filename, for example FNexusDestroyRuntimeActorCapability.
- docs(proxy): Document PascalCase, cache/snapshot isolation by final UE host/port, -32602 for invalid explicit ports before cache or confirmation handling, and cache boundaries during instance switching across all three clients.
- Audit parameters against current source first and use exported schemas only when extraction fails, preventing stale catalogs from hiding invalid parameters or contaminating synthetic-text tests.

### Fixed

- Restore standard MCP/JSON-RPC field casing so standard clients parse handshakes, tool schemas, annotations, and errors correctly; Capability business fields retain PascalCase.
- SearchMode/MultiTool aggregate item, section, compilation, and save errors with structured details. On Calls[] business/automatic compilation failure, attempt in-memory rollback and skip autosave. Defer saving until the whole batch passes and explicitly report rollback boundaries for disk/runtime side effects.
- ManageAssetDataTable accepts nonempty dynamic Fields dictionaries, rejects unknown fields, retains defaults for null, records Undo, and destroys temporary rows on both success and failure paths.
- RedundantCall no longer blocks reads; return hints alongside current data, count successful reads only, and clear history after writes.

## [2.2.0] - 2026-10-13

### Added

- feat(mcp): get_output_log session watch filters (categories / textIncludes / textExcludes / verbosity) use a 1000-entry side buffer; collectWatch=true reads only that interval without changing configuration. Merge consecutive identical logs as repeat and report overflow as dropped.

### Changed

- Move UnLua's 14 capabilities and FNexusLuaUtils to [NexusLinkExt](https://github.com/bytepine/NexusLinkExt) (NexusLinkExt / NexusLinkExtEditor). Stop detecting WITH_UNLUA here; append only registered handshake fragments and remove these capabilities from this catalog. UnLua L1 lives in the example project's NexusLinkExtTestSuite. NexusLinkExt releases separately with nexus-linkext-v* tags.
- Remember proxy write-gate always decisions within the current MCP session.
- Use ordinary authentication and confirmation-before-write wording in documentation and UI.
- Reuse MultiTool tools/list results while the enabled set, list mode, and dangerous-capability access remain unchanged. Successful tools/call below the slow-call threshold log at Verbose; errors and slow calls retain existing levels.

### Fixed

- Return an error when manage_asset_* has no target asset.
- Response-default compression preserves tool-provided *_defaults; convert those keys to FString per entry on UE 5.8.
- search_asset accepts userdefinedenum as a UserDefinedEnum type name.

## [2.2.0-beta.2] - 2026-10-12

> Pre-release; use outside production environments only.

### Fixed

- fix(mcp): manage_asset_* reports an error and returns on an empty target instead of dereferencing null. CapabilitySpec §8.9 requires checking returned pointers before dereference.

## [2.2.0-beta.1] - 2026-10-08

> Pre-release; use outside production environments only.

### Added

- feat(mcp): get_output_log session watch filters (categories/textIncludes/textExcludes/verbosity) use a 1000-entry side buffer; collectWatch=true reads only that interval without modifying configuration. Merge identical consecutive logs into repeat and return dropped on buffer overflow.

### Fixed

- fix(mcp): Default compression preserves tool-provided *_defaults, preventing get_output_log from reconstructing Error pages as Warning. Skip high-cardinality fields early; extract search_asset.assetType through automatic compression. Convert existing *_defaults keys to FString per entry on UE 5.8, whose Values keys are no longer FString.

### Changed

- refactor(plugin): Move UnLua's 14 capabilities and FNexusLuaUtils to [NexusLinkExt](https://github.com/bytepine/NexusLinkExt) (NexusLinkExt / NexusLinkExtEditor) and stop detecting WITH_UNLUA here. Compose handshake instructions only from FNexusInstructionRegistry registrations without scanning plugin directories. CapabilitySpec §2.1.2 documents extension modules, dangerous tags, and handshake registration. Remove these capabilities from this catalog; host UnLua L1 tests live in example-project NexusLinkExtTestSuite instead of NexusLinkTestSuite. NexusLinkExt releases independently with nexus-linkext-v* tags.
- docs: Document proxy write-gate always as current-MCP-session scope rather than process scope.
- docs: Replace coined terms in documentation and UI with ordinary wording: authentication and confirmation before writes.

## [2.1.1] - 2026-09-21

### Added

- feat(mcp): Add hostKind (Editor/Game/DedicatedServer, unchanged during PIE) and hasPlayWorld to /status, plus get_editor_context.current_map. control_pie/get_editor_info echo hasPlayWorld, returning state=starting when a session exists before its Play World is ready. Filter search recommendations and unknown-capability guidance by host, directing Game/DS users to Editor as needed.

### Changed

- docs: Add recommended NexusDesktop quick setup, token/write-operation guidance, and standalone Game/DS runtime debugging to Chinese/English landing pages and the usage guide. Clarify that -server does not read Preferences and requires explicit -EnableNexusMcp.
- feat(mcp): Add conversational keywords to get_asset_refs / exec_python; search_capabilities reports native C++ UENUM as outside its scope and directs UserDefinedEnum to get_asset_enum.

## [2.1.0] - 2026-09-15

### Added

- feat(plugin): Split into NexusLink Runtime, which can host MCP in standalone Game/DS, and NexusLinkEditor; exclude the server from Shipping. Add -EnableNexusMcp / -NexusMcpPort= / -NexusWsPort= / -NexusAllowLan.
- feat(server): Activation precedence is console > -EnableNexusMcp > Preferences (Editor only) > off. Add NexusLink.Mcp on|off|status|restart|panel.
- feat(mcp): Add three dangerous-capability modes (all disabled / confirm every call / custom), dangerous exec_python, and read-only get_python_api.
- feat(runtime): Move get_output_log / set_log_capture_filter / exec_command / capture_viewport into Runtime; editor screenshots use capture_editor_panel.
- feat(server): Default MaxConcurrentSessions to 16 and warn on slow calls.

### Changed

- docs: 38 Runtime + 192 Editor capabilities = 230; install to Plugins/NexusLink and publish only a universal EngineVersion: 4.26 package.
- chore(settings): Record default-disabled dangerous capabilities by name.

### Fixed

- fix(runtime): Runtime writes no longer fall through to editor-level Actors when PIE is not running.
- fix(mcp): set_runtime_lua.value accepts scalars; missing dofile_runtime_lua files produce per-item errors.
- fix(compat): Support optional-plugin capabilities across UE 4.26–5.8.
- ControlRig schema / fix(asset): Avoid adding /Game/ twice to missing packages and crashing the editor; fix Niagara/CommonUI Style creation, initial ViewModel View creation, and PCG/ControlRig schemas.

## [2.1.0-beta.2] - 2026-09-15

> Pre-release; use outside production environments only.

### Added

- feat(runtime): Move get_output_log / set_log_capture_filter / exec_command / capture_viewport into Runtime; editor screenshots use capture_editor_panel.
- feat(server): Activation precedence is console > -EnableNexusMcp > Preferences (Editor only) > off. Add NexusLink.Mcp on|off|status|restart.
- feat(ui): Add the in-game NexusLink.Mcp panel for PIE/standalone builds with session-scoped capability enablement.

### Fixed

- fix(runtime): Fix null state-machine access in get_runtime_actor_animation in standalone Game.

## [2.1.0-beta.1] - 2026-09-14

> Pre-release; use outside production environments only.

### Added

- feat(plugin): Split into NexusLink Runtime, which can host MCP in standalone Game/DS, and NexusLinkEditor; exclude the server from Shipping. Add -EnableNexusMcp / -NexusMcpPort= / -NexusWsPort= / -NexusAllowLan.
- feat(mcp): Add three dangerous-capability modes (all disabled / confirm every call / custom), dangerous exec_python, and read-only get_python_api.
- feat(server): Default MaxConcurrentSessions to 16 and warn on slow calls.

### Changed

- docs: Install to Plugins/NexusLink, publish only a universal EngineVersion: 4.26 package, and move L1 tests to the host's NexusLinkTestSuite.
- chore(settings): Record default-disabled dangerous capabilities by name.

### Fixed

- fix(mcp): set_runtime_lua.value accepts scalars; missing dofile_runtime_lua files produce per-item errors.
- fix(compat): Support optional-plugin capabilities across UE 4.26–5.8.
- ControlRig schema / fix(asset): Avoid adding /Game/ twice to missing packages and crashing the editor; fix Niagara/CommonUI Style creation, initial ViewModel View creation, and PCG/ControlRig schemas.

## [2.0.2] - 2026-09-04

### Fixed

- fix(mcp): Report search_capabilities overflow only when results are actually truncated; add items.enum to array inputs and reduce noisy search keywords.
- fix(mcp): get_asset_data_table mode=rows accepts rowNames or nameFilter; non-Actor Blueprints support set_defaults.

## [2.0.1] - 2026-09-03

### Fixed

- fix(compat): Fix undefined FNexusLanHost::Loopback symbols when linking with Mac Clang.
- fix(plugin): Fix detection of optional plugins such as GAS/Niagara under custom engine paths.

## [2.0.0] - 2026-08-31

### Added

- feat(mcp): Expand to 227 capabilities, covering StringTable, Font, Foliage, Media, Paper2D, GeometryCollection, CommonUI, MoviePipeline, GAS Cue, and multiple create/manage families. Add saveToDisk/compile, Undo for writes, control_movie_pipeline, and LAN binding.
- feat(mcp): Use one machine authentication token shared with UE/Desktop/Rider/VSCode; enable authentication by default, support extra tokens, and copy remote connection configuration.

### Changed

- **BREAKING** refactor(mcp): Use English for MCP/AI-visible text while retaining Chinese source comments, settings UI, and logs.
- chore(plugin): Detect GAS/Niagara from the host instead of forcing enablement; exclude L1 tests from release ZIPs.
- docs: Turn the README into a landing page and consolidate installation/connection guidance in docs/usage-guide.md.

### Fixed

- fix(mcp): Dispatch HTTP /stream and /status back to GameThread to avoid crashes during editor shutdown.
- fix(mcp): Group settings capabilities by source directory; retain only capabilities enabled in the current host in relatedCapabilities.
- fix(compat): Build on UE 4.26–5.8, covering PIE pause, MovieScene, Niagara, promote_pin, StaticMesh, HTTP hot switching, and more.
- fix(mcp): Split extra authentication tokens on commas/semicolons, use weak callbacks after WebSocket shutdown, and stop reporting ready when token persistence fails.

### Security

- Require Bearer authentication for HTTP/WebSocket, reject Origin, limit bodies/frames to 1MB, and bind loopback by default.
- Disable dangerous capabilities by default. With LAN enabled, same-network clients with a token can connect; do not expose the server publicly.
- UE 4.26–5.1 WebSockets cannot specify a bind address and listen on all adapters; log Error at startup when authentication is off.
- Clear previously authenticated WebSocket connections when turning authentication on; use token-file permissions 0600 on POSIX.

## [1.16.2] - 2026-08-12

### Changed

- chore(plugin): Restore the main module to Type: Runtime for Game/Server linking; MCP actually runs only in Editor/PIE.

## [1.16.1] - 2026-08-12

### Changed

- **BREAKING** feat(schema): Validate inputs strictly against schemas and reject unknown keys; accept operations[] only. Capabilities are single-target; use calls[] for multiple targets.
- **BREAKING** refactor(schema): Rename inputs without legacy aliases, including destAssetPath, ownerClass, scriptPath, and luaPath; standardize top-level manage batching on operations[].

### Added

- feat(mcp): Add incremental cursors, preset=diagnose, and category summaries to get_output_log; always capture Warning/Error.

### Fixed

- fix(compat): Adapt UE 5.8 FJsonObject key types and custom-engine HELP_TEXT.

## [1.16.0] - 2026-08-04

### Added

- feat(host): Add a Runtime base class and host filtering; DS/Game expose Runtime capabilities only, and Shipping never starts MCP.
- feat(server): `NexusLink.EnableMcp 1|0`；`-EnableNexusMcp`
- feat(mcp): Add inheritance directions children/descendants/parent/ancestors to get_asset_refs.
- feat(mcp): Add replace_node / sync_graph to manage_asset_behavior_tree and Event-node support to manage_asset_blueprint.
- feat(feedback): Record plugin, engine, and platform versions in feedback.

### Changed

- chore(plugin): Set the main module Type to Runtime, allowing -server/Game loading with MCP disabled by default.

### Fixed

- fix(compat): Adapt Montage duration APIs, using SetCompositeLength on UE5+.
- fix(mcp): Ensure BeginPlay in create_asset_blueprint, avoid abstract-enum crashes in create_asset_enum, and match strings case-insensitively by default.
- fix(mcp): Fix an ensure during unload_asset registration.

## [1.15.3] - 2026-07-28

### Added

- feat(mcp): Automatically unload batch-read assets by memory high-water/package count, support call_capability.keepLoaded, and add manual unload_asset.
- feat(mcp): Include recommendedGet / recommendedManage in search_asset results.

### Changed

- perf(mcp): Remove the results[{}] envelope for single results, standardize identity on path, and omit success:true on success.

### Fixed

- fix(compat): Correct UE 5.2 material/skeletal-mesh header version gates.
- fix(release): Enable Git LFS in CI so release packages contain assets instead of pointer text.

## [1.15.2] - 2026-07-17

### Changed

- fix(update-checker): Read the installed version from .uplugin VersionName because distribution packages do not include VERSION.

## [1.15.1] - 2026-07-17

### Added

- feat(mcp): When capability names are called as tools in SearchMode, recommend call_capability.
- feat(mcp): Add WebSocket nexus/proxy_feedback so proxy failures enter the feedback workflow.
- feat(mcp): Include owned, inherited, and native C++ components in get_asset_blueprint.component.

### Changed

- feat(mcp): Reject overly broad search_capabilities queries with narrowing suggestions.
- fix(mcp): Reject empty required schema strings and require actorName in get_runtime_actor_property.

## [1.15.0] - 2026-07-08

### Added

- IKRig / IKRetargeter；MetaSound / MetaSoundPatch；PCG；PoseSearch / feat(mcp): Add BlendSpace/AnimSequence keys and curves, ControlRig/IKRig/IKRetargeter, MetaSound/MetaSoundPatch, PCG, and PoseSearch.
- feat(mcp): Add Enhanced Input, MaterialFunction, MPC, StateTree writes, LevelSequence, PhysicsAsset, EQS, and DataLayer.
- feat(mcp): Add node flatIndex to get_asset_behavior_tree and expandable subobject properties.

## [1.14.0] - 2026-07-02

### Added

- feat(update-checker): Check updates in settings and optionally silently at startup.
- feat(feedback): Capture redacted arguments, error fingerprints, minimal reproductions, and prefilled GitHub Issues in the browser.
- feat(mcp): `get_asset_state_tree` / `get_asset_view_model`（UE 5.5+）

### Changed

- chore(release): Support X.Y.Z-beta.N Pre-releases; release notes come only from CHANGELOG.
- docs: Add an English README, remove private-repository references, and recommend marketplace installation first.

### Fixed

- fix(compat): Adapt UE 5.8 FJsonObject key types.
- fix(ci): Stop reporting Markdown same-page anchors as HTTP 404s.

## [1.14.0-beta.1] - 2026-06-29

> Pre-release; use outside production environments only.

### Added

- feat(update-checker): Check updates in settings and optionally silently at startup.
- feat(feedback): Capture redacted arguments, error fingerprints, minimal reproductions, and prefilled GitHub Issues in the browser.
- feat(mcp): `get_asset_state_tree` / `get_asset_view_model`（UE 5.5+）

### Changed

- chore(release): Support Pre-releases; release notes come only from CHANGELOG.
- docs: Add an English README and remove private-repository references.

### Fixed

- fix(compat): Adapt UE 5.8 FJsonObject key types.

## [1.13.1] - 2026-06-24

### Changed

- docs: Move plugin documentation to its own repository and add packaging scripts/GitHub Actions.

### Fixed

- fix(mcp): Do not silently succeed after manage_asset_blueprint connection failures; support friendly names in FindBPPin.

## [1.13.0] - 2026-06-23

### Added

- feat(mcp): Add BT move_node/childIndex, animation notifies, Blackboard enum keys, and ProgressBar reads/writes.

### Fixed

- fix(mcp): Fix save_asset persistence, HTTP /stream thread safety, search aliases, Mac LiveCoding, and viewport-overlay click-through.

## [1.12.2] - 2026-06-09

### Added

- feat(server): Add Enable MCP Server, off by default, with immediate start/stop when toggled.

### Fixed

- fix(compat): Build Game targets with WITH_EDITOR=0 across all supported versions.
- fix(plugin): Fix editor-shutdown crashes and settings capability-group paths.

## [1.12.1] - 2026-06-04

### Added

- feat(mcp): Add nexus/proxy_config so UE supplies IDE proxy tool descriptions and initialize prefixes.

## [1.12.0] - 2026-06-04

### Added

- feat(mcp): Add read-only editor queries, multiple manage_asset_* families, export_asset/reimport_asset, and runtime GAS/BT/UMG, expanding capabilities from 91 to 107.

### Fixed

- fix(mcp): Connect exec_command with get_output_log.

## [1.11.0] - 2026-06-03

### Added

- feat(mcp): Add read-only mesh/animation/audio/Niagara/level capabilities, compile_blueprint, Montage interaction, and ten GAS tools gated by WITH_GAS.
- feat(mcp): Add errorKind, legacy capability-name mapping, and UMG/Slate layout.

## [1.10.0] - 2026-06-01

### Added

- feat(mcp): Add duplicate_asset and search_capabilities exact-name shortcuts/OR fallback.

### Fixed

- fix(mcp): Receive WebSocket messages on GameThread so slow tools no longer break persistent IDE connections.
- fix(compat): Support custom-engine WITH_EDITOR_ENCRYPTION.

## [1.9.0] - 2026-05-27

### Added

- feat(mcp): Add SearchMode/MultiTool modes, call_capability.calls[], and get_asset_blueprint.component.

### Fixed

- fix(mcp): Inject TTL metadata through both response paths and enable scrolling in the settings capability tree.
- fix(compat): Adapt UE 5.8 JSON and engine APIs.

## [1.8.1] - 2026-05-12

### Fixed

- fix(mcp): Tokenize search_capabilities by underscores, correct diff_runtime_actors parameter names, and include original errorText in feedback reports.

## [1.8.0] - 2026-05-11

### Changed

- refactor(mcp): Layer Utils, standardize Capability/Tool metadata and four-part descriptions, and rename UMG tools.

### Fixed

- fix(compat): Build on UE 4.26–5.7.
- fix(mcp): Correct search_capabilities AND scoring and result limits.

## [1.7.1] - 2026-05-09

### Fixed

- fix(mcp): Enforce true AND matching and result limits in search_capabilities; return early on redundant_call without executing again.

## [1.7.0] - 2026-05-09

### Added

- feat(mcp): Add search_capabilities / call_capability / submit_feedback meta-tools and persist feedback in .nexus-feedback/.
- feat(mcp): Add manage_asset_actor and AnimBlueprint/Montage/BehaviorTree/Blackboard reads/writes.
- feat(server): WebSocket `nexus/instructions`

### Changed

- **BREAKING** refactor(mcp): Decouple Capability from Tool and invoke capabilities by name; rename groups including search_asset and get_asset_*.
- **BREAKING** refactor(mcp): Merge property reads/writes into get_property / set_property, replace batches with single targets, and remove get_asset_generic / call_blueprint_function and related tools.
- **BREAKING** refactor(mcp): Standardize output on results[] and use enumerated visibility for dangerous/hidden tools.

### Fixed

- fix(compat): Build on UE 4.26–5.7.
- fix(mcp): Fix validation and dirty marking for materials, widgets, DataTables, structs, and more.

## [1.6.4] - 2026-04-24

### Changed

- feat(mcp): Default ToolsListMode to Starter to reduce token usage.

### Fixed

- fix(mcp): Include description in tools/list.

## [1.6.3] - 2026-04-24

### Changed

- fix(mcp): Default bMinimalContentText to false because mainstream clients do not read structuredContent.

## [1.6.2] - 2026-04-24

### Added

- feat(mcp): Add multiword AND matching/aliases to search_tools and generate initialize.instructions by ToolsListMode.

### Changed

- perf(mcp): Condense response JSON and hide search_tools in Full mode.

## [1.6.1] - 2026-04-24

### Added

- feat(mcp): Add search_tools, ToolsListMode (Full/Starter/Custom), response TTL metadata, and detail levels.

### Fixed

- fix(mcp): Fix log-allowlist races, save Blueprints immediately after creation, and clean HTTP sessions by TTL.
- fix(mcp): Return search_tools text even for clients that do not parse structuredContent.
- fix(mcp): Broadcast tools/list_changed after changing ToolsListMode.

## [1.6.0] - 2026-04-23

### Added

- perf(mcp): Compress response defaults by extracting repeated list fields into *_defaults.

### Changed

- refactor(mcp): Remove the low-value _meta.shape footer while retaining report_unused_fields.

## [1.5.4] - 2026-04-23

### Added

- feat(mcp): Add report_unused_fields and optional field-shape hints for large responses.

## [1.5.3] - 2026-04-23

### Added

- feat(mcp): Add actor section=all to get_property.

### Fixed

- fix(plugin): Fix unresponsive Open Directory / Export feedback actions on Windows caused by relative paths.

## [1.5.2] - 2026-04-22

### Fixed

- fix(mcp): Distinguish feedback argsDigest arrays by content instead of incorrectly merging equal-length arrays.
- fix(mcp): Accept singular inputs as runtime fallbacks in batch tools while schemas still advertise plural inputs.

## [1.5.1] - 2026-04-21

### Changed

- perf(mcp): Separate schema enums from descriptions; token audits recognize namespace prefixes and InitializeInstructions.md.

### Fixed

- fix(compat): Automatically fall back to VS2019 for UE 4.27.

## [1.5.0] - 2026-04-21

### Added

- feat(mcp): Allow pure Name() calls in get_property and batch comparison through diff_actors.
- feat(mcp): Add feedback capture, submit_feedback, and export_feedback with environment, fingerprints, and Issue drafts.
- feat(test): L1 Automation + L2 pytest E2E

### Changed

- **BREAKING** refactor(mcp): Merge six property-tool sets into get_property / set_property.
- **BREAKING** feat(mcp): Use updates[] / operations[] / plural arrays for write tools and omit success:true from successful items.

### Fixed

- fix(mcp): Refuse SCS edits during PIE by default, preload UE.* for eval, and distinguish functions/private fields when properties are not found.

## [1.4.0] - 2026-04-20

### Added

- feat(mcp): Add container indices and material-node traversal to get_asset_property; support get_asset.defaults and assetPaths[].
- feat(mcp): Add feedback capture, submit_feedback, and export_feedback.
- feat(server): Return netRole from /status.

### Fixed

- fix(mcp): Fix material Texture-node binding and stop set_node from silently succeeding.

### Changed

- perf(mcp): Use English runtime error messages.

## [1.3.1] - 2026-04-17

### Changed

- perf(mcp): Replace long schema descriptions with enum/default and greatly shorten initialize.instructions.
- **BREAKING** refactor(mcp): Rename list_runtime_widgets.ownerWidget to widgetClass.

### Fixed

- fix(mcp): Align get_editor_info descriptions with actual fields.

## [1.3.0] - 2026-04-16

### Added

- feat(mcp): capture_viewport supports windows, panels, actorName, widgetName, viewAngle, and windowIndex through cross-platform ReadPixels.

## [1.2.0] - 2026-04-16

### Added

- feat(mcp): Add create_behavior_tree, AnimBlueprint/AnimMontage, exec_command, get_asset_refs, and animation/GameplayTag/BehaviorTree/viewport screenshots.
- feat(mcp): Support UnLua 1.x/2.x, get_lua/manage_lua, and enable all tools by default.

### Fixed

- fix(compat): Build on UE 5.0–5.1.
- fix(mcp): Fix control_pie status and batch-deletion retries.

## [1.1.1] - 2026-04-15

### Fixed

- fix(mcp): Improve DataAsset abstract-class hints, Blueprint K2_ node prefixes, material-input disconnection, CheckBox isChecked, and material-instance automatic inference.

## [1.1.0] - 2026-04-15

### Added

- feat(mcp): Add Lua evaluation/call stacks, diff_actors, Slate, material tools, property batching/diagnostics, and a tool-enablement panel defaulting to read-only.

### Changed

- refactor(mcp): Merge eleven asset tools into get_asset / get_asset_property / set_asset_property, reducing tools from 49 to 41.

### Fixed

- fix(compat): Adapt UE 5.3–5.7 material APIs, advance ports after conflicts, refresh tool lists, and support dynamic runtime child widgets.

## [1.0.1] - 2026-04-14

### Changed

- refactor(mcp): Merge list_data_assets into list_assets and row operations into manage_data_table_row.

### Fixed

- fix(compat): Build on UE 4.26–5.7, including UE 5.2 C4668 and missing-header fixes.

## [1.0.0] - 2026-04-13

### Added

- DataTable / DataAsset / Struct / Widget / Actor / PIE） / feat(mcp): Add the MCP server (Streamable HTTP, WebSocket, and /status), instance registration, port-conflict fallback, and 47 tools covering Blueprints, DataTables, DataAssets, structs, widgets, Actors, and PIE.
