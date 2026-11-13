<a id="独立编辑器工具与领域能力"></a>
# Independent editor tools and domain capabilities
[中文](editor-toolsets.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

Dedicated graph editor operations, Slate UI, Automation tests, and nonblocking Live Coding are engine capabilities provided by NexusLinkEditor, requiring neither NexusLinkExt nor a second official MCP service.

<a id="接口"></a>
## Interfaces

- GetEditorGraph: pass AssetPath without GraphName to discover graphs; then pass a returned GraphName for node layout/comments, SelectedNodeIds, and available view state. Narrow using NodeId, NameFilter, Offset, Limit.
- ControlEditorGraph: Action=Open|SetView|ZoomToFit|FocusNode|SelectNodes|ClearSelection operates a specified graph through native APIs without mouse/key events or Slate Ref. Continue using domain ManageAsset* for node/pin/link data edits.
- GetEditorSlate: Action=Windows|Snapshot|WaitFor, optional Ref and MaxDepth (0–100). Snapshot filters text/tooltips with TextFilter and widget types with TypeFilter while traversing unmatched containers. It scans up to 20000 nodes and returns 2000 matches. Nodes includes Ref, ParentRef, LocalSize, Role, Tooltip, Actions, focus/check state; NexusLink owns Refs. ParentRef identifies parents; LocalSize={X,Y} is local widget size. Parents may be filtered/truncated; query a subtree by Ref. Truncation is reported.
- CaptureEditorSlate: optional snapshot Ref, empty selects the active window; returns local FilePath and standard MCP image content.
- InteractEditorSlate: Action=Click|DoubleClick|Scroll|Type|PressKey|Focus|Hover|Drag|SelectOption|SetChecked|FillForm with a queried Ref. Window focus uses window Refs from Windows. Generic snapshot Actions include Click, DoubleClick, Scroll, Drag, PressKey, Focus, Hover; textboxes/checkboxes add relevant actions. Actual outcomes depend on widget input handling.

Type/bReplace=true supports SSearchBox and ordinary edit boxes. Use ControlEditorLevel(Action=LoadStreaming|Show|Hide|MakeCurrent, AssetPath=...) for sublevels already referenced by the current map. LoadStreaming loads existing references; MakeCurrent shows and selects the current level.
- ControlEditorAutomation: Action=Discover|Run|Stop. After discovery, ListEditorAutomationTests(NameFilter, Limit) returns full names. Run takes TestNames[]; Stop takes the owned run's OperationId. Only the current editor's local test session is used.
- CompileEditorLiveCoding starts nonwaiting compilation, requiring Windows, a supported build, and Live Coding enabled by the user in this session. It does not toggle enablement or replace full compilation/editor restart.
- GetEditorOperation(OperationId, WaitSeconds) queries this MCP session's operation. Default returns immediately; waits support 0–30 seconds. Running results may be queried again.

Long-operation envelopes include OperationId, Backend, State, bCanCancel, Message, ElapsedSeconds, optional Result. Completed means execution ended; inspect per-test results or compilation Result.Outcome. Unreliably determined outcomes remain Unknown; "no longer compiling" does not imply success. Result retention is bounded; restart invalidates IDs.

<a id="后端与兼容"></a>
## Backends and compatibility

UI, tests, compilation directly use Slate, AutomationController, ILiveCodingModule. No NexusLink version directly links/calls official Toolsets, Registry, or Python toolset packages. Standard modules retain [engine-required transitive dependencies](toolset-backends.en.md). NexusLink owns Ref management, operation ownership, validation, asynchronous results.

PCG, Niagara, Control Rig, SemanticSearch still require their domain plugins/APIs. Toggling official Toolsets does not change execution paths. Ext carries only third-party plugin capabilities.

Live Coding uses nonwaiting underlying entry points across versions. Mac/unsupported builds return explicit reasons. These capabilities target full Editor only, not standalone Game/DS; Shipping still strips MCP.

<a id="权限等待与输出"></a>
## Permissions, waits, and output

UI input, test control, compilation, PCG control, Niagara fixes are dedicated writes without Dangerous. Only arbitrary code execution uses dangerous Confirm/Custom gates. These asynchronous controls have no asset-transaction rollback guarantee. UI/domain diagnostics/control stay single-call; independent settings/plugin/skill queries may aggregate through Calls[]. UI, task state, images are not cached or served as disconnected snapshots.

Desktop, VSCode, Rider waits support MCP notifications/progress. Clients must supply progress tokens and accept SSE; notifications go only to their request. Numbers are elapsed wait seconds, not completion percentages. Initial direct UE HTTP uses status polling.

Standard notifications/cancelled ends waiting; underlying operations remain queryable. Stopping tests requires explicit Stop; Live Coding need not be stoppable. Timeouts/disconnects do not justify replaying writes.

These long-operation capabilities declare envelope output Schema via search-detail outputSchema and MultiTool definitions. Output is validated after adaptation/compression; undeclared legacy capabilities retain behavior.

Discovery Execution includes fixed backend/reason. Output Schema covers windows, snapshot nodes, screenshots, per-test results, compiler diagnostics, plus Nodes_defaults / Windows_defaults / Tests_defaults for compressed output validation. Empty window titles are valid; unknown fields or mismatched nested types are rejected.

Slate Ref binds to the NexusLink session and weak widget reference. Snapshots reread current visible trees. Rediscover Refs after UI rebuilding, window closure, or widget invalidation. Hidden targets, targets under disabled ancestors, and detached window widgets reject input.

Use the domain MCP first for editor work: GetAsset*/ManageAsset* for graph structure/data, GetEditorGraph/ControlEditorGraph for opening/focus/selection/view. For a missing graph action, discover dedicated tools and record the gap with SubmitFeedback; do not immediately substitute mouse wiring, context menus, or generic Slate input. Use Slate only for UI without domain/graph interfaces, and computer control only when no MCP path exists, such as native dialogs or external UI. Modal dialogs/slow tasks remain EditorBusy-gated; modal control is not guaranteed. Do not automatically retry writes after disconnect/timeout.

<a id="专用图表操作"></a>
## Dedicated graph operations

Discover asset paths with SearchAsset, then graphs with GetEditorGraph(AssetPath). Use returned relative GraphName paths; never guess. Ambiguous duplicate names reject; use the full returned path. Existing UEdGraphs inside Blueprints, Widget/Animation Blueprints, behavior trees, PCG, etc. use asset-owned graphs. Open material/material-function editors use their current preview graphs, distinguishing source assets from preview copies. Discovery determines available graphs.

With GraphName, GetEditorGraph returns node IDs, positions, recorded dimensions/comments; filter with NodeId/NameFilter, paginate Offset/Limit. SelectedNodeIds, ViewLocation, Zoom describe existing native editor state. Unopened graph editors have no view; queries never open documents automatically. Blueprint views come from the target editor's unique graph document, avoiding same-asset Diff windows. Behavior trees use owned editor views in UE 5.3+; 4.26–5.2 are structure-only. PCG 5.7 uses the owned single graph tab; 5.8+ matches native documents per graph for separate main/embedded views/selection, and Open can open unopened targets. PCG 5.4–5.6 are structure-only. Check bViewAvailable. Discover graphs/nodes before passing GraphName, NodeId, NodeIds[] to control.

ControlEditorGraph requires AssetPath and, except for Open, GraphName. Actions:

- Open: without GraphName, open/focus the asset editor and return graph discovery; with a discovered GraphName, attempt to open that graph.
- SetView: provide ViewLocation or Zoom. ViewLocation={X,Y} is the top-left graph offset, not the view center. Zoom is (0,10]; the engine may quantize to supported levels. Requery actual values.
- ZoomToFit: fit the graph, or current selection with bOnlySelection=true.
- FocusNode: locate a discovered NodeId.
- SelectNodes: supply 1–256 NodeIds[], SelectionMode=Replace|Add|Remove, default Replace. Validate all before selection changes.
- ClearSelection: clear the graph's node selection.

Both tools return synchronously without OperationId. bRequestAccepted/bViewDeferred distinguish acceptance from delayed view updates. View/selection need GUI and native graph editors. FocusNode/ZoomToFit move views later through the engine; read back with GetEditorGraph rather than treating synchronous return as completed animation. No arbitrary scripts or graph structure edits are executed, and domain compile/save/Undo contracts remain. Use ManageAsset* for node/pin/link/layout data changes.

<a id="蓝图结构与布局"></a>
### Blueprint structure and layout

Read GetAssetBlueprint(Sections=["graph"], GraphName=...) first. Nodes include PosX, PosY, Width, Height, Comment; pins include bHidden, child ParentPin, parent SubPins[]. Use current read-back IDs with ManageAssetBlueprint.Operations[]:

- SplitPin/RecombinePin require GraphName, NodeId, PinName and use K2 schema. Input structs support Vector/Rotator/Vector2D/LinearColor with preserved defaults; reject other inputs before changes. Output structs follow schema support. Supported inputs preserve valid numeric text, avoiding float conversion/fixed-decimal truncation of large coordinates/tiny fractions. Recombination uses current child values. Splitting rejects already split, connected, or unsplittable pins. Both reject invalid/nonfinite/out-of-range defaults before edits. Recombine accepts parent/child pin names but rejects connected descendants. Returns parent name and complete updated Node; use the new pin list afterward.
- DuplicateNodes requires GraphName and 1–64 unique same-graph NodeIds[]. OffsetX/OffsetY default 64, may be zero/negative; int32-overflow positions reject. Supports native CallFunction, VariableGet, VariableSet, Knot, Comment. Events, graph boundaries, Timeline, Composite, and other resource/structure nodes are unsupported. Internal links are retained, external links omitted. New GUIDs return in NodeIdMap={oldGUID:newGUID} and Nodes[].

Use existing Connect/Disconnect, SetNode, ArrangeNodes for links/properties/positions. Compilation/persistence still require explicit bCompile/bSaveToDisk or CompileBlueprint/SaveAsset.

<a id="slate-进阶操作"></a>
## Advanced Slate operations

Type(Text, bReplace=true) replaces text; empty clears. PressKey accepts Ctrl+, Shift+, Alt+, Cmd+ combinations and Unreal Key names. Replacement only supports standard editable text widgets and validates focus/full selection before clearing. Engine key events verify full selection before clear/type.

Click/DoubleClick and Drag accept Button=Left|Right|Middle, default Left. Modifiers[] combines Ctrl/Shift/Alt/Cmd by injected events without changing OS key state. Widgets reading FSlateApplication::GetModifierKeys() global state may not respond; verify via readback/screenshots. Position={X,Y} is normalized within the target, axes [0,1], default center; Hover/Scroll accept it too. Current geometry converts coordinates while checking visibility, enablement, hit-testability.

Scroll requires nonzero WheelDelta in [-20,20], positive up/negative down. Drag requires source Ref and EndRef; EndPosition={X,Y} uses Position bounds/defaults. Same Ref at both ends supports within-widget drags. Use ControlEditorGraph(SetView) for graph pan/zoom. Acceptance does not prove the intended operation; requery or capture afterward.

SetChecked(Value="true"|"false") accepts checkbox Refs. SelectOption(Value) matches visible options exactly only in the combobox's own popup; it never clicks unrelated equal text when missing. For host-window menus use focused keyboard navigation. All input retains NexusLink permission checks.

FillForm Ref selects the root; Fields[] entries contain Ref, Value, FieldType=textbox|checkbox|combobox. Validate all as visible/enabled descendants before writing, then recheck each during execution. Textboxes replace text. Mid-run invalidation returns AppliedCount/FailedFieldIndex; earlier successes remain without retry.

WaitFor(Text, TextGone, TimeoutSeconds) checks all visible windows without Ref, polling via ticker for up to 30 seconds. Returns bConditionMet/bTimedOut; timeout means unmet condition. Hidden subtrees do not match. Traversal is count/depth bounded; truncated "not found" cannot prove disappearance.

<a id="umg-结构编辑"></a>
## UMG structure editing

ManageAssetUserWidget.Operations[] adds:

- Move: WidgetName, ParentWidget, optional ChildIndex; reordering within one parent works, -1 appends.
- Wrap: WidgetName and panel WidgetClass, returning WrapperName.
- ReplaceWithChild replaces a container with its single child. ReplaceWithTemplate uses WidgetClass with engine reference/BindWidget type checks.
- SetNamedSlotContent creates WidgetName/WidgetClass in a valid empty slot. SlotName required; ParentWidget is slot host, omitted for inherited slots. Occupied slots are not overwritten.

Read RootWidget, widget Parent/ChildIndex, NamedSlots, InheritedNamedSlots with GetAssetUserWidget first. After parent-template changes, bCompile=true or explicit CompileBlueprint before deriving widgets/filling inherited slots. Read back, compile/save as needed. Existing tree/property/animation operations remain across versions. Structure actions use that version's UMGEditor/WidgetTree APIs, with inherited-slot/template replacement scope limited to underlying support.

<a id="项目设置与插件查询"></a>
## Project settings and plugin queries

GetProjectSettings defaults to Containers; Container selects Categories, then Category selects Sections. Properties/Values also need Section; Values requires 1–100 explicit PropertyNames. Reads registered settings UObjects without changing/resetting/saving config. Custom panels without UObjects explicitly error. Discovery supports NameFilter, Offset, Limit.

GetProjectPlugins defaults to Plugins with NameFilter, bEnabledOnly, pagination. PluginName selects Info, Dependencies, Dependents. AssetOwner resolves project/engine/plugin mount roots from AssetPath. Plugins are not toggled or descriptors rewritten.

<a id="独立领域-api"></a>
## Independent domain APIs

These interfaces call domain plugin APIs directly with Backend=Independent envelopes. Result.CompatibilityResult/Result.OfficialResult may remain historical containers, not backend selectors. Follow output Schema. Completed means the call ended; inspect business outcomes/read after writes. Discover actions/parameters first via GetProjectToolsets(Sections=["Actions"]).

- **PCG:** GetEditorPCG supports Instances, GraphStructure, GraphSchema, NativeNodes, NativeNodeSchema, NodeData. Graphs use asset object paths; ActorPath/NodePath use discovered refPath. NodeData optionally takes PinLabel/AttributeName, with Offset/Limit up to 1000. Initial read enables inspection and may require reexecution; then ControlEditorPCG(Action=Execute, ActorPath=...), followed by reading. Spawn uses AssetPath, ActorName, optional Location, default volume scale 25/25/10. PCG calls serialize inspection state and return busy reasons while another runs. GetAssetPCGGraph returns input/output nodes and Path; old graph editing can connect them and rejects built-in endpoint removal.
- **Niagara:** GetEditorNiagara queries CompileState, StackIssues, Dependencies, SystemData, module topology/Schema/values, renderer/input Schema/values, DynamicInputChain, AvailableDynamicInputs. SystemData discovers EmitterName/ScriptName/ModuleName. Input chains use InputNameStack, renderers EmitterName/RendererIndex, class queries ClassPath. ExecNiagaraStackFix applies only explicit IssueId/FixId from that system's latest StackIssues, without automatic fix selection.
- **SemanticSearch:** SearchAssetsSemantic Search takes Query; FindSimilar takes an indexed source AssetPath. Both require ClassPaths and allow PathRegexes, Limit (1–100). Real engine hybrid vector/BM25/similar search requires SemanticSearch indices/embedding providers. Missing providers, uninitialized/empty indices, or unindexed sources explicitly error, without name-match fallback. Only the SemanticSearch domain plugin/model configuration is required, not SemanticSearchToolset. Results are Result.CompatibilityResult.Items with scores and available descriptions.
- **Project skills:** GetProjectSkills defaults to List, returning Name/Description/SkillPath in Result.CompatibilityResult.Skills[]. Details accepts only exact listed SKILL.md paths and adds Instructions. Scans project Skills/, .agents/skills/, enabled plugin Skills/, engine Extras/AgentTools/Skills/, at most 1 MiB per file. Reading Instructions does not automatically apply session instructions or create/update files. Migrate old AgentSkill class paths to SKILL.md.

<a id="截图与标准-mcp-图片"></a>
## Screenshots and standard MCP images

CaptureViewport, CaptureEditorPanel, CaptureEditorSlate by default return MCP content[] image blocks alongside FilePath metadata: type=image, mimeType=image/png|image/jpeg, base64 data. bIncludeImage=false keeps path-only mode. Fetch asynchronous Slate captures through owned GetEditorOperation; batches retain attachments. Text/StructuredContent preserve original metadata.

Each response permits at most 4 images and 4 MiB total raw bytes. Limits, invalid files, unsupported formats add omission reasons while retaining paths. Only files explicitly generated by screenshots attach; arbitrary business FilePath does not imply attachments. Same-second screenshots use distinct names. Desktop/VSCode/Rider skip large-text offloading and proxy caches for image results, allowing direct display.

For PCG node/graph/instance parameters and subgraphs plus PhysicsAsset, GameplayTags, DataRegistry read/write loops/version boundaries, see [parameters and project data](pcg-physics-tags-dataregistry.en.md).


For further domains, see [animation, deep Niagara writes, Game Features, native action discovery](animation-niagara-gamefeatures-toolsets.en.md).
