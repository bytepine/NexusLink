<a id="功能缺口首批补充"></a>
# Initial feature-gap additions
[中文](feature-followups.md) · English

This batch delivers initial functionality in nine areas. Availability depends on UE version, host, and enabled plugins; each scope is specified below.

<a id="动作级发现"></a>
## Action-level discovery

GetProjectToolsets(Sections=["Actions"], Capability="ControlEditorAnimation", Action="AddFloatKey") returns bSupported, Backend, MinimumVersion, Prerequisites, Reason, and InputSchema. It covers six Animation/Niagara/PCG editor read/write entry points; omitting filters lists them with pagination. Every version uses independent implementations. Discovery reflects current engine APIs/domain plugins, without reading official Registry or enabling plugins.

Forwarding entry-point Schema describes inner Parameters fields; flat entry points return the complete client input contract. Paths come from actual discovery. Availability still requires valid targets, property types, and other constraints; it does not imply every native parameter type is supported by compatibility implementations.

<a id="结构预览与回读"></a>
## Structure preview and readback

GetAssetEditPreview plans sequentially on isolated temporary copies:

- Blueprint：RemoveNode、Disconnect、DisconnectAll。
- UserWidget: named Add, Remove, Move. Add requires WidgetName and, when a root exists, a parent panel.
- PCGGraph：AddNode、RemoveNode、AddEdge、RemoveEdge。

Returns StructureHash, structural Added/Removed records, and per-operation bValid. Hashes preserve Unicode names including Chinese. Blueprint copies include interface implementation graphs; UE 5.8 widget-removal plans include variable-reference nodes removed by the engine. Processing stops on the first failure. IDs of new preview nodes are preview-only; read again after real writes. Plans validate nodes, edges, and hierarchy without compilation, saving, or graph business-result validation. Do not mix scalar and structure preview.

Corresponding Manage operations accept ExpectedStructureHash and reject structure changes since preview. bVerifyStructure=true builds per-operation structural plans and compares real structure, returning bStructureVerified. Mismatch prevents automatic saving; already-applied memory changes remain under existing partial-success rules. Whole-disk rollback is not promised. Scalar ExpectedValue / bVerify retain their rules.

<a id="首次导入"></a>
## First import

ImportAsset accepts one absolute local SourceFile, DestinationPath (a directory under /Game/), AssetType, and optional AssetName:

- Texture: PNG/JPEG/BMP/TGA/EXR/HDR; bSRGB controls color space.
- SoundWave：WAV。
- StaticMesh: FBX; combines meshes in the file, with bGenerateCollision controlling automatic collision.
- SkeletalMesh / AnimSequence: FBX, requiring an existing SkeletonPath. Initial SkeletalMesh support accepts one mesh node and fixes the complete target name. Split files containing multiple mesh nodes, including multiple LODs or fragments sharing a skeleton, to avoid factory naming being ignored and causing overwrites. AnimSequence supports multiple takes.

ConflictPolicy defaults to Fail, with explicit Unique or Replace. Animation FBX checks potential single/multiple-take target names before writing. Any conflict makes Fail reject the entire import; Unique chooses an unused basename for all takes; only Replace permits overwriting. Take names colliding after sanitization are rejected. All actually created/changed animation paths are returned, including takes omitted from the factory's primary return. FBX imports no materials/textures, creates no PhysicsAsset, and mesh imports include no animation. Returns product paths, types, and readback without automatic save. Then call SaveAsset; inspect GetOutputLog on failure. ReimportAsset updates existing assets.

<a id="多人-pie-配置"></a>
## Multiplayer PIE configuration

ControlPie Start adds NetMode=Standalone|ListenServer|Client, NumberOfClients=1..8, and bRunUnderOneProcess. Default is single-player; Standalone/Simulate allow only one player. Wait for actual PlayWorlds readiness in Status; entries report PieInstance, WorldPath, and actual NetMode. Separate processes report only their own Worlds: discover/connect their endpoints. This batch excludes input recording/replay and ListView-entry interaction.

<a id="低版本-pcg-执行与输出"></a>
## Older-version PCG execution and output

The compatibility ControlEditorPCG backend supports Spawn and asynchronous Execute. Wait through its GetEditorOperation. Repeated starts on generating components are rejected; cancellation and a 120-second timeout are supported. Generation completion is not business success; inspect logs/output.

GetEditorPCG NodeData supports point data from one execution stack, including Transform, Density, Seed, with Offset/Limit up to 1000. Execute automatically enables inspection; reexecute if data is unavailable. Nonpoint data, multiple loop stacks, and arbitrary Metadata attributes return explicit errors. Deep Niagara dynamic input and Rig baking depend on available public engine APIs and action discovery.

<a id="骨骼姿态采样"></a>
## Skeletal pose sampling

GetAssetAnimSequence on UE 4.26+ accepts BoneNames (1–32 actual skeleton names) and SampleTimes (1–128 seconds within the clip), returning local-space BoneSamples: Translation, XYZW RotationQuaternion, and Scale. Without sample parameters, metadata querying is unchanged. Bone-track writing, trimming, and resampling are excluded.

GetAssetBlendSpace / ManageAssetBlendSpace support both 1D and 2D. 1D allows only AxisIndex=0 and automatically rebuilds its interpolation grid after edits. UE4 2D edits still need grid rebuilding in the BlendSpace editor; RebuildNote explains this. SetAxis requires finite values with Max > Min, GridNum 1–256; AddSample uses engine coordinate, duplicate-sample, and animation-compatibility validation. Animation Blueprint GraphName accepts nested state graph names or GraphPath from GraphOverview; duplicate names require full paths. States/transitions use their native graph Schema. Intermediate edits do not implicitly compile; after connecting, explicitly pass bCompile=true to validate/save.

<a id="所属任务历史与取消"></a>
## Owned operation history and cancellation

GetEditorOperation without OperationId lists recent summaries for the current MCP session, supporting bRunningOnly and Limit (1–128). Summaries include bHasResult; fetch full results by ID. ControlEditorOperation requests cancellation only for owned running operations exposing stop callbacks. Terminal, noncancelable, and other-session tasks reject it. Canceling a wait still cancels only the wait.

History is bounded in memory and cleared on editor restart, without restoration across identities. VSCode, Rider, and Desktop use the same session routing; queries/cancellation preserve Owner, do not cache task responses, and do not replay writes. This batch adds no task panel.

<a id="性能预算"></a>
## Performance budgets

Offline trace_compare compare adds --budget budget.json:

```json
{
  "MinSamples": 3,
  "Frames": {"P95Seconds": {"MaxAfterSeconds": 0.02, "MaxIncreasePercent": 5}},
  "Cpu": {"GameThread": {"MaxIncreaseSeconds": 0.01}}
}
```

Frame metrics accept MedianSeconds/P95Seconds/MaxSeconds; CPU uses exact timer names. Rules are MaxAfterSeconds, MaxIncreaseSeconds, MaxIncreasePercent. Exit codes: 0 pass (or reporting without budgets), 1 exceeded budget, 2 input/execution error, 3 insufficient evidence. Different conditions, insufficient samples, missing/duplicate timers, and zero percentage baselines cannot pass budgets. JSON/HTML retain details. Trace recording, GPU, memory, and network analysis are excluded.

<a id="资产源控预检"></a>
## Asset source-control preflight

GetAssetSourceControl queries 1–128 saved packages, reporting file paths, read-only attributes, Provider availability, and cached status. States include controlled, checked out, checked out by others, can check out, conflicted, current, and added. bStatusKnown=false means safe writing cannot be inferred. Explicit bRefresh=true asks the Provider to refresh; failures return errors. This batch is read-only, without checkout/add/submit or binary semantic diffs.
