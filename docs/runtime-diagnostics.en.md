<a id="输入性能与-ui-诊断"></a>
# Input, performance, and UI diagnostics
[中文](runtime-diagnostics.md) · English

These capabilities use UE domain APIs directly, without official Toolset registration or calls. Availability depends on engine version, host, and domain plugins.

<a id="编辑器忙碌状态"></a>
## Editor busy state

During modal dialogs or slow tasks, ordinary MCP requests return JSON-RPC error -32002 with error.data containing:

- `ErrorKind=EditorBusy`、`Reason=ModalDialogOpen|SlowTaskInProgress`。
- bExecuted=false: the request did not execute.
- RetryAfterSeconds: suggested delay before checking editor state again.

ping, connection status, and wait cancellation remain responsive. HTTP /status and WebSocket status add BusyReason; empty means no busy condition detected. Canceling a wait does not stop the business operation.

Resubmit after recovery only when a rejection explicitly states bExecuted=false. Timeouts, disconnections, and unknown outcomes do not prove nonexecution and must not automatically replay writes.

UE 5.9 uses per-port HTTP service interfaces to serve these connections during Slate modal loops, slow-task progress callbacks, and Windows native modal message loops. It does not drive Worlds, global HTTP services, or the core Ticker. Older versions retain pre-execution interception but do not guarantee timely responses while the core Ticker is blocked. macOS native system dialogs do not guarantee continuous service.

<a id="玩家输入"></a>
## Player input

InteractRuntimeInput Action=Apply accepts Inputs[], DurationSeconds, WorldPath (or unique WorldName), and LocalPlayerIndex.

- Select either a scalar Key or a discovered InputAction AssetPath for each input.
- X/Y/Z must be in [-1,1], default (1,0,0), with a nonzero axis. Digital keys require (1,0,0); Action axis count must match its type.
- Apply 1–16 inputs simultaneously for 0–30 seconds. Zero releases after at least one real game Tick. Paused Worlds reject new inputs.
- Discover unique World identity from ControlPie(Action=Status) PlayWorlds.WorldPath. WorldName works only if unique. Player indices belong to the selected World. Multiple Play Worlds require exact WorldPath. Dedicated Server has no local players and cannot inject input.
- Continuous InputAction injection requires EnhancedInput and UE 5.7+. Legacy scalar Keys use native PlayerInput.

The returned OperationId belongs to the current connection session. Read GetRuntimeInput(OperationId) for operation and active PlayerInput Key/Action state; release early with InteractRuntimeInput(Action=Release,OperationId). Completion, explicit release, World cleanup, or server shutdown clears inputs owned by this tool; a bounded lease handles stopped game Ticks.

Existing pressed states, Keys held by other operations, and existing Action injection are not overwritten. Reported input describes this tool's requests, not physical OS keys or proof of movement/ability success. Verify through Actor/Widget reads or screenshots.

<a id="实时性能"></a>
## Live performance

GetRuntimePerformance returns the latest available process-wide frame and Game/Render/RHI thread timings in milliseconds, with sample time, frame count, and TtlSeconds=0. Thread samples can come from different frames and do not represent synchronized single-frame profiling.

GpuMs is returned for valid completed GPU samples; otherwise bGpuTimingAvailable=false and a reason. Queries do not start recording or change console configuration. This version excludes active GPU Pass capture and live Stat trees.

<a id="csv-性能查询"></a>
## CSV performance queries

GetProfilerCsv is available in UE 5.7+ Editor:

1. Action=Files lists CSVs under project Saved/Profiling and returns CapturePath.
2. Action=Summary reads the CapturePath's frame count, metric names, and event count.
3. Action=Stats with StatFilter, FrameStart, FrameEnd computes Min/Max/Mean/Total. SortBy accepts Mean/Max/Total or Name.
4. Action=Events queries events in that same frame range.

Ranges are half-open [FrameStart, FrameEnd), with FrameEnd=-1 meaning the end. Invalid/empty ranges are rejected. Offset/Limit paginate. Metric units come from the capture: counts/bytes are not all milliseconds. Nonfinite samples are excluded; actual SampleCount is returned.

Only text .csv under project Saved is accepted: at most 16 MiB, 500,000 frames, 8192 columns, and 4 million samples. The engine parser reads a bounded memory snapshot preserving empty EVENTS columns, quotes, and commas. Complete trailing headers are supported; trailing columns absent early in a streaming capture count as zero. Extra columns, duplicate metric names, and invalid ranges are rejected. Binary CSV is unsupported. Queries neither start recording nor return full Metadata that may include command lines.

<a id="umg-属性来源与连接"></a>
## UMG property origins and connections

GetAssetUserWidget adds two Sections:

- PropertyOverrides: class defaults, inherited WidgetTree archetypes, and relevant binding/direct graph-write declarations by widget. PropertyPaths accepts up to 32 entries; defaults are Visibility, RenderOpacity, bIsEnabled.
- Connections: legacy property bindings, MVVM directions, direct widget event/delegate nodes, variable writes, and selected Widget setter evidence through inheritance.

NameFilter filters both; TypeFilter filters widget type only for PropertyOverrides. Offset/Limit paginate diagnostic records. Results identify source objects, Blueprints, graphs, and node GUIDs. LastStaticSource means only the latest static source; bRuntimeWinnerCertain=false does not infer the final runtime value.

Branch execution, aliases, runtime handlers, arbitrary C++ setters, and cross-function propagation are not fully proved. Declarations use DeclaredButUnconfirmed; default-disabled or noncompiled MVVM bindings use KnownNotAppliedByDefault with exact flags. Two-way and reverse bindings follow their actual directions.

<a id="可选-commonui-诊断"></a>
## Optional CommonUI diagnostics

Registered only in UE 5.9+ Editor/PIE hosts with CommonUI enabled and the CommonUIDebugging domain module:

- GetCommonUIInfo defaults to Sections=Players, returning session-wide player indices and stable PlayerId. These differ from the input tool's per-World indices.
- After selecting a player, query ActivatableTree, InputRouting, Focus, ActionBindings. Limit bounds tree prefixes or list pages; truncated results are not complete trees.
- ControlCommonUIDiagnostics(Action=Start) starts a bounded 1–60-second capture with BufferSize 64–4096 and no call stacks by default. Returns OperationId and Epoch.
- Action=Stop accepts only an OperationId owned by this session, without replacing/stopping other consumers' logs.
- GetCommonUIInfo(Sections=[EventLog]) reads buffered logs and DroppedCount; LocalPlayerIndex=-1 reads retained departed-player events. Poll with Epoch and AfterSeq; discard the cursor when Epoch changes.

Logs filter by stable PlayerId. An event does not prove a business handler ultimately processed input successfully. Retained records remain readable after completion/stop. Editor-module unload and MCP service stop clean up this tool's captures. This version diagnoses rather than automatically repairing focus or activation stacks.
