<a id="独立编辑器能力与版本范围"></a>
# Independent editor capabilities and version scope
[中文](editor-compatibility.md) · English

[Documentation](README.en.md) · [Independent backends](toolset-backends.en.md)

NexusLink implements these capabilities directly using engine editor/domain-plugin APIs. The plugin itself does not directly link or call official Toolsets, Registry, or official Python toolset packages. Standard modules retain [engine-required transitive dependencies](toolset-backends.en.md). Limits come from underlying APIs; discover actions/parameters via GetProjectToolsets.Sections=["Actions"].

<a id="进阶参数与结构编辑"></a>
## Advanced parameters and structure editing

- ControlEditorPCG compatibility supports graph parameter definition/removal, instance parameter override/reset, and node settings writes. GetEditorPCG supports instance lists/parameters, graph parameter descriptions/values, and node setting descriptions. Compatible types are numeric scalars, Bool, String, Name; PCG must be enabled (current plugin support UE 5.4+). Full object paths such as /Game/PCG/G.G are recommended. Execution, sampling, and node Schema use independent PCG implementations; exact types/actions follow current engine APIs.
- ControlEditorNiagara compatibility supports System properties, User Variables, Renderer addition/removal and property writes; corresponding queries read values back. User Variables allow known numeric, Bool, vector, color, and quaternion structures, rejecting arbitrary UObject/data interfaces. propertyValues is a JSON string keyed by engine property names. Compatible writes apply only after every field converts successfully. Independent Niagara also provides module inputs, Stack diagnostics, and explicit fixes; check versions/types/scope through action discovery. Existing ManageAssetNiagaraSystem actions remain available.
- Get/ControlEditorAnimation compatibility supports Sequence track discovery/addition, Binding lists, Section lists/addition/removal, range/Ease, channel names, Float/Bool/Integer/String key reads/writes, and per-frame deletion. Frames are integer display-rate frames converted to engine Tick Resolution for writes. Float interpolation supports Auto/Cubic, Linear, Constant. Advanced Control Rig baking/layers use current public APIs. Existing ControlRig asset operations use Get/ManageAssetControlRig.
- ManageAssetUserWidget Move/Wrap/ReplaceWithChild/ReplaceWithTemplate/SetNamedSlotContent have older-version compatibility implementations; UE 5.8 retains FWidgetBlueprintOperationUtils. Compatible template replacement handles only leaf widgets/empty Panels; Named Slots only empty slots in the current WidgetTree. Use SetSlot after moving between Panels to configure the new Slot. Cycles, occupied single-child containers, and invalid hosts are rejected.

These operations keep existing fields and do not accept caller-specified tool classes/execution paths. Asynchronous interfaces use GetEditorOperation and report Backend=Independent. Result.CompatibilityResult / Result.OfficialResult may remain historical data containers; follow output Schema. Missing underlying engine actions return explicit version/type limits.

<a id="导航与碰撞诊断"></a>
## Navigation and collision diagnostics

GetEditorScene provides read-only actions in the current editor World, with results in Result.CompatibilityResult:

- NavigationStatus returns bNavigationAvailable, bNavigationBuilding, bBuildingLocked, RemainingTasks, RunningTasks. Optional WaitSeconds (0–30) observes existing builds, returning OperationId for GetEditorOperation. Timeout returns bTimedOut=true without starting/stopping/rebuilding navigation.
- NavigationContributors requires finite BoundsMin/BoundsMax and queries the existing navigation octree, returning owner/Actor paths, actual bounds, geometry/modifier flags, deferred collection state, and area classes. Offset/Limit paginate. It does not trigger deferred geometry collection; absent octrees report bOctreeAvailable=false.
- NavigationTiles uses the same spatial bounds and default Recast data. ScanLimit examines at most 65536 tile slots per call; NextOffset is the next scan position and Limit returns up to 500. bTruncated means slots remain unscanned; missing Recast data reports bRecastAvailable=false.
- LevelGeometry defaults to persistent Level.Model BSP node/surface/point/polygon counts and overall/bounded node bounds. AssetPath selects an already loaded level package. It never switches maps, exports, or loads hidden references.
- Collision requires exact ActorPath or ComponentPath, returning registration state, bPhysicsStateCreated, bValidBodyInstance, collision/Visibility responses, and navigation flags. Trace accepts bTraceComplex and returns hit component, FaceIndex, position, normal.

ControlEditorScene.RefreshComponentPhysics requires one exact ComponentPath and stopped PIE, calls RecreatePhysicsState, and neither changes nor saves asset properties. BuildNavigation returns ErrorKind=StateConflict while navigation is building/locked. Dirty packages blocking a map switch return the same error and DirtyWorldPackages; handle listed packages explicitly.

GetAssetStaticMesh adds Navigation, Sections, LODForCollision, bContainsPhysicsTriMeshData. MaxTriangles (default 0, up to 128), TriangleOffset, LODIndex read render-mesh local triangles/centroids. Render-face and physics-face indices need not agree, so these fields alone do not prove component physics state. ManageAssetStaticMesh.RefreshCollision rebuilds existing BodySetup and refreshes registered mesh instances. Collision complexity/simple-collision edits also refresh physics state and navigation component data. Explicit saving still persists assets.

CallCapability(Calls=[...], bAsyncReadOnly=true, TimeoutSeconds=30) accepts only registered, batchable read-only capabilities in Editor hosts: 1–64 items, one per frame. OperationId exposes CompletedCount, TotalCount, FailureCount, Results, progress. ControlEditorOperation(OperationId=...) cancels pending reads. Deadlines 0.01–120 seconds are checked between items and cannot interrupt an engine call already running. Failure/cancellation/timeout retain completed results, without write retries. Normal synchronous Calls also allows up to 64.

ExportAsset returns ExporterDiagnostics (up to 32 native warnings/errors), ExporterErrors, and a World LevelGeometry summary. Empty diagnostics remain empty lists; exporter, output path, and bWritten remain available. bValidateOnly does not write files.

<a id="静态网格生成"></a>
## Static mesh generation

ManageAssetStaticMesh adds:

- GenerateLODs(TrianglePercents): 1–7 retention fractions excluding LOD0, in (0,1], nonincreasing.
- RemoveLODs: keeps LOD0.
- GenerateConvexCollision(HullCount, MaxHullVertices, HullPrecision): bounded integers; read back with GetAssetStaticMesh.
- SetNaniteEnabled(bEnabled): UE 5+; UE4 explicitly unsupported.

UE5 uses StaticMeshEditorSubsystem and UE4 EditorStaticMeshLibrary, both called directly by NexusLink. Writes retain existing asset-saving behavior without saving other dirty assets.

Collision geometry coordinates use Schema-declared X/Y/Z or explicit ExtentX/Y/Z and LocX/Y/Z. Lowercase x/y/z in older reference lists have been corrected; use field spelling returned by SearchCapabilities.

<a id="编辑器场景"></a>
## Editor scenes

GetEditorScene(Action=Folders|FolderActors|Selection|Trace) and ControlEditorScene operate in the current editor World. Trace is a single Visibility-channel raycast taking Start/End. FolderActors supports recursion; Selection is the current editor selection.

Writes support folder creation/rename/removal, Actor folders, selection, StaticMeshActor merging, and UE5+ Level Instance creation/edit/commit. Actors must belong to the current editor World. Merge takes unique StaticMeshActors; AssetPath is the engine merge tool's output base path (actual generated mesh names are available from the returned Actor). Existing base packages are rejected and source Actors are kept by default. Level Instance creation references another existing level; commit saves only that instance's level package. bDiscard discards instance edits.

Folder, Trace, merge, selection, and instance management use independent engine C++; instance commit does not call save-all-dirty methods. UE4 has no Level Instance.

<a id="蓝图图编辑"></a>
## Blueprint graph editing

GetAssetBlueprint adds Subgraph, NodeTypes, NodeTypePins, GraphDSL sections, all requiring GraphName. Subgraph starts at NodeId; compatible traversal is bounded by MaxDepth/Limit. NodeTypes requires NameFilter of at least two characters and includes loaded K2 classes/callable functions. Pin queries use FunctionName/FunctionClass or discovered NodeTypeId according to input Schema.

ManageAssetBlueprint adds ArrangeNodes, AddFunctionParameter, RemoveFunctionParameter, SetVariableCategory, SetVariableEditable, WriteGraphPlan, WriteGraphDSL. Layout, parameter, and variable-property operations use independent BlueprintGraph/BlueprintEditorUtils engine interfaces.

SetVariableMetadata(VariableName, ToolTip) changes metadata; empty ToolTip clears it. Categories still use SetVariableCategory; batch edits do not rebuild the skeleton per item. SetVariableReplication(VariableName, Replication=None|Replicated|RepNotify) supports Actor/ActorComponent-owned members and rejects replicated Set/Map. RepNotify also requires RepNotifyFunction naming an existing function with no inputs/outputs and neither pure nor RPC (AddFunction first if needed). It does not enable Actor/component replication or remove existing notification graphs. SetFunctionCallInEditor(FunctionName, bCallInEditor) configures editor buttons for owned no-argument functions. All use explicit bCompile/bSaveToDisk; Variable/Function sections read back ToolTip, Category, Replication, RepNotifyFunction, bCallInEditor.

WriteGraphPlan appends 1–64 nodes and at most 128 links. Each GraphNodes item has Id and existing AddNode fields, supporting K2Node_CallFunction, K2Node_VariableGet, K2Node_VariableSet. GraphLinks uses From/FromPin/To/ToPin and connects only nodes created in this plan. Connection failure removes those plan nodes and returns an error. Success returns Id→NodeId mappings; read back through existing graph and new Subgraph sections.

GraphDSL/WriteGraphDSL use UnrealGraphText, the native copyable node text exported by FEdGraphUtilities. GraphDSL returns Code; WriteGraphDSL appends 1–500 copyable nodes after validating import in a temporary complete Blueprint. Old official Lisp/S-expression text is no longer accepted; no arbitrary scripting is provided. WriteGraphPlan retains the structured plan above, without a GraphPlan read section. Parameter/structure changes retain compilation/save options.
