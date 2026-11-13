<a id="动画niagaragame-features-与自有动作发现"></a>
# Animation, Niagara, Game Features, and native action discovery
[中文](animation-niagara-gamefeatures-toolsets.md) · English

[Documentation](README.en.md) · [Independent backends](toolset-backends.en.md)

These capabilities operate on existing assets and editor sessions. NexusLink calls public engine APIs directly and does not directly link or call official Toolset, ToolsetRegistry, or official Python toolset packages. Available actions depend on the engine version and relevant domain plugins. Standard modules retain the [transitive dependencies required by the engine](toolset-backends.en.md).

<a id="版本与启用"></a>
## Versions and enablement

- Sequencer / Control Rig use engine MovieScene, Sequencer, ControlRig, and related editor scripting APIs. Advanced features are limited to available interfaces; AnimationAssistantToolset and SequencerAnimMixerToolset are not required.
- AnimMixer actions require UE 5.8+ and the engine domain plugin MovieSceneAnimMixer to be explicitly enabled in the host, followed by recompilation and restart. It supplies the Mixer track, layer, and transition data model; NexusLink does not dispatch through a Toolset. When disabled, action discovery returns bSupported=false and lists the PluginEnabled:MovieSceneAnimMixer prerequisite.
- Niagara uses Niagara / NiagaraEditor data, graph, and Stack APIs; NiagaraToolsets is not required.
- Game Features requires GameFeatures. Its capabilities use the engine subsystem and asynchronous completion callbacks; GameFeaturesToolset is not required.
- GetProjectToolsets queries NexusLink's own registered metadata without discovery or dispatch through an external Registry.

Domain plugins must be enabled in the host and recompiled. These tools do not automatically enable plugins for a request.

## Sequencer / Control Rig

GetEditorAnimation and ControlEditorAnimation accept fixed Action and Parameters values. First query InputSchema, MinimumVersion, Prerequisites, bSupported, and Reason with GetProjectToolsets(Sections=["Actions"], Capability=..., Action=...), then construct Parameters using that action's parameter names. Object, track, section, and Rig references come from actual query results.

FBX export requires a nonempty sequence playback range with valid lower and upper bounds; the engine explicitly validates this starting in UE 5.9. Configure the target sequence's playback range first: export does not change the asset range for you. Export uses a new .fbx file path; import uses an existing .fbx file and explicit import settings.

- Bindings, tracks, and sections: Bindings, SequenceTracks, BindingTracks, Sections, SectionProperties, SectionRange, plus addition/removal and range configuration.
- Channels and keys: ChannelNames, Keys, ChannelDefault; writing, deleting, setting defaults, and baking Float / Bool / Integer / String keys.
- Control Rig: Rig/control discovery, value reads and writes, transforms, keys, tracks, and baking and layer operations supported by the current engine.
- Sequencer extensions: conditions, Outliner, binding conversion, FBX/animation import and export, sequence folders, and AnimMixer. Underlying APIs must exist; open the target Sequence and establish explicit selection first when necessary.

Actions execute only NexusLink's predefined implementations and do not accept external tool class names or arbitrary Python paths. Animation reads/writes use Backend=Independent. Existing Result.CompatibilityResult / Result.OfficialResult fields remain historical data containers; read their fields according to the output Schema.

<a id="niagara-深层写入"></a>
## Deep Niagara writes

Use SystemSummary / EmitterTopology / ScriptTopology to discover emitter, script, module, and input names, then read the corresponding Schema and data before using ControlEditorNiagara.

- Read/write System/Emitter data according to its property Schema; UserVariables use explicit types and default values.
- Renderer operations use discovered emitters, renderer indices, types, and property data.
- Module, Stack Input, dynamic input chain, linked variable, and Set Parameters operations take fields defined by each independent action Schema. Do not guess nested node references.
- StackIssues queries and ExecNiagaraStackFix use explicit IssueId / FixId values returned for the same system. Fixes are not automatically selected.

Input structures and supported types depend on the current engine's actual APIs. Query, validation, and write failures return specific reasons; unsupported inputs are not silently converted to other types.

## Game Features

GetProjectGameFeatures returns Plugins by default, with name filtering and pagination. Use Sections=["State"] and PluginName for live state. Only built-in plugins recognized by engine Game Features settings are listed.

ControlEditorGameFeature accepts Action=Activate|Deactivate and a discovered PluginName. It waits for the engine completion callback, then checks final state. Results include PluginName, State, bActive, and bApplied; the historical Result.OfficialResult container does not mean an official Toolset was called. A second request for a plugin with an operation already running is rejected. Arbitrary URLs are not accepted; descriptors are not changed and plugins are not unloaded or deleted.

<a id="nexuslink-自有发现"></a>
## NexusLink-owned discovery

GetProjectToolsets keeps its historical capability name and Sections parameter, but now returns NexusLink Capability and action metadata:

- Actions: the default section; supports Capability / Action filtering and returns action availability and the Parameters InputSchema.
- AnimationRoutes: animation action metadata without external tool mappings. Query/call using each entry's Capability / Name.
- Toolsets: groups by NexusLink registration source and returns group name, description, and visible ToolCount.
- Tools: pass a ToolsetName returned by Toolsets to obtain visible Capability names and descriptions in the group.
- Schema: pass an exact ToolsetName and a ToolName returned by Tools to obtain that Capability's Name, Description, InputSchema, and available OutputSchema.

Lists support NameFilter, Offset, and Limit (up to 500) and obey NexusLink capability disablement and host visibility rules. They do not read or execute external C++ / Blueprint / Python Toolsets. Migrate old external ToolsetName/ToolName values to NexusLink identifiers returned here. Project descriptions and Schema values are treated as data.

<a id="操作和保存"></a>
## Operations and saving

Long operations return OperationId; wait using GetEditorOperation. Canceling a wait does not mean underlying work has stopped. Completed means execution has ended; check the business result too. Game Features verifies activation state in its callback.

Most editor actions directly change memory or editor state. Explicitly compile/save changed assets; long operations do not provide transaction rollback across tools. Results may contain temporary object references: rediscover them after editor restart or graph rebuilding.
