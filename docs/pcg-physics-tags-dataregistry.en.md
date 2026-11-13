<a id="pcg-参数physicsassetgameplaytags-与-dataregistry"></a>
# PCG parameters, PhysicsAsset, GameplayTags, and DataRegistry
[中文](pcg-physics-tags-dataregistry.md) · English

[Documentation](README.en.md) · [Parameter reference](tool-reference.md)

<a id="pcg-参数闭环ue-58"></a>
## PCG parameter read/write loop (UE 5.8+)

Enable the host's PCG domain plugin and recompile. GetEditorPCG / ControlEditorPCG call PCG engine APIs directly without PCGToolset or official Registry dispatch. UE 5.9 PCG still internally requires Registry; see [transitive dependency boundaries](toolset-backends.en.md). Backend is Independent; follow output Schema for result containers. Start once, then query GetEditorOperation by OperationId. Completed means the call ended; inspect business fields such as bApplied and Error. Writes provide no transactional rollback; avoid automatic replay after disconnects.

First obtain object paths and node refPath through asset search and GetAssetPCGGraph / GraphStructure, then call:

- GetEditorPCG(Action=NodeInfo, NodePath=...) reads node settings; ControlEditorPCG(Action=UpdateNode, NodePath=..., Parameters={...}) writes native settings properties. NodeTitle is optional; Parameters must be nonempty. Property names/value structures follow NativeNodeSchema / NodeInfo.
- SetGraphParameters: AssetPath and ParameterDefinitions (1–64). Each requires Name/Type; optional Description, ContainerType=None|Array, DefaultValueJson. The last is JSON-encoded text, e.g. number 1.5, boolean true, string "value". Only PCG types enumerated in Schema are supported. Read back through GetEditorPCG GraphSchema.
- RemoveGraphParameters: AssetPath and ParameterNames (1–64 discovered names).
- SetInstanceParameters: ActorPath and nonempty Parameters. ResetInstanceParameters: ActorPath and ParameterNames. Read back with GetEditorPCG(Action=InstanceParameters), then Execute and read NodeData to verify generation.
- GetEditorPCG(Action=AvailableSubgraphs) discovers subgraphs. ControlEditorPCG(Action=AddSubgraph) specifies AssetPath, SubgraphPath, unique NodeName, optional Parameters/NodeTitle. Self-reference, recursive subgraphs, and existing names are rejected.

Spawn also accepts optional Parameters as initial instance values. Explicitly SaveAsset after graph asset edits. Instance changes belong to the editor level state; save levels through the project workflow. UE 5.4–5.7 retains basic ManageAssetPCGGraph node/link operations; parameter types/actions depend on actual engine APIs. Discover availability and inputs using GetProjectToolsets Actions.

<a id="physicsasset-精细编辑"></a>
## Detailed PhysicsAsset editing

CreateAssetPhysicsAsset(AssetPath, SkeletalMeshPath, bAssignToMesh=false) uses engine automatic collision generation and saves a new asset, rejecting overwrites. Optional assignment saves/changes the source Mesh; default does not assign.

First GetAssetPhysicsAsset for Bodies, Shapes, Constraints, then use ManageAssetPhysicsAsset Operations:

- AddBody / RemoveBody: BoneName. Removal also removes associated constraints and maintains engine Body/collision indices. With a preview Mesh, added bodies must belong to its skeleton.
- UpdateShape / RemoveShape: BoneName, ShapeType=Sphere|Capsule|Box, ShapeIndex within that type's array. Optional edits: Radius, HalfHeight, ExtentX/Y/Z, CenterX/Y/Z, RotationPitch/Yaw/Roll. Rotation applies to Box/Capsule. Dimensions must be positive/finite. HalfHeight retains the existing half cylinder-length definition; Box Extent is half-size.
- SetMassScale: BoneName, positive MassScale.
- SetConstraintLimits: JointName; optionally Swing1Motion, Swing2Motion, TwistMotion (Free/Limited/Locked) and corresponding Limit (0–180 degrees). Unspecified axes retain values.

Readback includes Shape type/index/local dimensions/transform, MassScale, and constraint motion/angles. Removal may change indices; requery before more edits. Existing AddSphere / AddCapsule / AddBox / SetPhysicsType / AddConstraint / RemoveConstraint remain available. Read after writes and SaveAsset as needed.

<a id="gameplaytags-配置管理"></a>
## GameplayTags configuration

Enable GameplayTagsEditor. GetGameplayTags(Sections=["Info"], Tag=...) queries explicit state, sources, and comments. Sources lists ordinary INI TagList sources with optional NameFilter.

ManageGameplayTags(Tag=..., Operations=[...]) handles one target tag:

- Add: requires a simple TagSource .ini filename; optional Comment. The engine creates missing sources. Directory paths are not accepted.
- Rename: requires NewTag and uses engine rename/Redirect mechanisms. Later operations in the same call follow the new name. Target must neither exist nor be in the old tag subtree.
- Remove: explicit leaf tags in ordinary INI sources only; children or asset references reject removal. Inspect/handle Referencers first.

This dedicated configuration-write capability has no Dangerous tag. The engine writes Config immediately, independent of bSaveToDisk or SaveAsset. CallCapability.calls[] and transaction rollback are unsupported; earlier successful operations survive later failure. Native code tags and restricted sources reject writes. Query Info/Hierarchy afterward.

<a id="dataregistry-查询ue-50"></a>
## DataRegistry queries (UE 5.0+)

Enable DataRegistry in the host and recompile. GetProjectDataRegistry defaults to Registries and queries only registered types, without loading/activating extra registries for discovery. The capability is not registered when disabled.

With RegistryName, query Info, Schema, Items, DataSources, RuntimeSources. Discovery supports NameFilter, Offset, Limit (up to 500). Info returns asset path, description, item Struct, count, availability; Schema returns field names/types.

Values requires explicit ItemNames (1–100). Each returns Name/bCached. Hits return the actual Struct JSON as Value; misses return NotCached. Queries do not Acquire, wait for loading, or return default placeholder rows. The consuming gameplay system must acquire first; then read cache. Source entries include ClassPath, source AssetPath, Description, initialization/transient state, Availability. Query configured and runtime sources separately.
