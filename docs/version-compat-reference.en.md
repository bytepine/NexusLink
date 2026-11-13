<a id="nexuslink-版本兼容宏参考"></a>
# NexusLink version compatibility macro reference
[中文](version-compat-reference.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

> Source: NexusLink/Public/Utils/NexusVersionCompat.h

<a id="基础宏"></a>
## Basic macros

| Macro | Definition | Description |
|---|---|---|
| NX_UE_VERSION | ENGINE_MAJOR_VERSION * 100 + ENGINE_MINOR_VERSION | Numeric current UE version |
| NX_UE_AT_LEAST(Major, Minor) | NX_UE_VERSION >= Major * 100 + Minor | Version comparison helper |

<a id="语义别名宏"></a>
## Semantic alias macros

| Macro | Minimum version | Description |
|---|---|---|
| `NX_UE_HAS_FTSTICKER` | 5.0 | `FTicker` → `FTSTicker` |
| `NX_UE_HAS_FTSTICKER_HANDLE` | 5.0 | `FDelegateHandle` → `FTSTicker::FDelegateHandle` |
| NX_UE_HAS_SAVE_PACKAGE_ARGS | 5.0 | UPackage::SavePackage uses FSavePackageArgs |
| `NX_UE_HAS_MARK_AS_GARBAGE` | 5.0 | `MarkPendingKill()` → `MarkAsGarbage()` |
| NX_UE_HAS_SET_PIE_WORLDS_PAUSED | 5.0 | UEditorEngine::SetPIEWorldsPaused; 4.26/4.27 use PlayWorld->bDebugPauseExecution |
| NX_UE_HAS_FIND_FIRST_OBJECT | 5.1 | ANY_PACKAGE lookup → FindFirstObject |
| `NX_UE_HAS_CLASS_PATHS` | 5.1 | `FARFilter::ClassNames` → `ClassPaths` |
| NX_UE_HAS_BP_INTERFACE_ASSET_PATH | 5.1 | ImplementNewInterface/RemoveInterface use FTopLevelAssetPath; short-name FName overload emits C4996 |
| `NX_UE_HAS_IMPORT_TEXT_DIRECT` | 5.1 | `FProperty::ImportText` → `ImportText_Direct` |
| `NX_UE_HAS_EXPORT_TEXT_ITEM_DIR` | 5.1 | `ExportTextItem` → `ExportTextItem_Direct` |
| `NX_UE_HAS_PROGRESS_GET_PERCENT` | 5.1 | `UProgressBar::Percent` → `GetPercent()` |
| `NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA` | 5.1 | `UMaterial::Expressions` → `GetEditorOnlyData()` |
| `NX_UE_HAS_ANIM_SEGMENT_ACCESSOR` | 5.1 | `FAnimSegment::AnimReference` deprecated → `GetAnimReference()`/`SetAnimReference()` |
| NX_UE_HAS_SCOPED_MATERIAL_DOMAIN | 5.2 | Same threshold as MATERIAL_DOMAIN_HEADER; historical alias, enum values remain MD_* |
| NX_UE_HAS_MATERIAL_DOMAIN_HEADER | 5.2 | EMaterialDomain moves to MaterialDomain.h; Material.h fully supplies it through 5.1 |
| NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER | 5.2 | FSkeletalMaterial is fully defined in SkinnedAssetCommon.h; SkeletalMesh.h only forward-declares it in 5.2+ |
| NX_UE_HAS_TS_ON_CONFIG_SECTIONS_CHANGED | 5.2 | FCoreDelegates::TSOnConfigSectionsChanged enables FHttpServerConfig cache invalidation for live LAN rebinding |
| NX_UE_HAS_HTTP_DELEGATE | 5.4 | FHttpRequestHandler becomes TDelegate |
| `NX_UE_HAS_EXPORT_TEXT_DIRECT` | 5.5 | `ExportTextItem_Direct` → `ExportText_Direct` |
| `NX_UE_HAS_STRUCT_UTILS_HEADER` | 5.5 | `Engine/UserDefinedStruct.h` → `StructUtils/` |
| `NX_UE_HAS_ALLOW_SHRINKING_ENUM` | 5.5 | `TArray::Pop(bool)` → `Pop(EAllowShrinking)` |
| NX_UE_HAS_EDGRAPH_PERFORM_ACTION_VECTOR2F | 5.6 | FEdGraphSchemaAction::PerformAction takes FVector2f; FVector2D overload emits C4996 |
| `NX_UE_HAS_IK_RIG_SOLVER_STRUCTS` | 5.6 | `UIKRigDefinition::GetSolverArray()` → `GetSolverStructs()`（`FIKRigSolverBase`） |
| NX_UE_HAS_IK_RIG_ADD_CHAIN_GOAL | 5.2 | AddRetargetChain(..., GoalName); 5.0 uses three arguments without Goal |
| NX_UE_HAS_IK_RIG_ADD_CHAIN_BONE_CHAIN_ONLY | 5.1 only | AddRetargetChain accepts only FBoneChain; 5.0 has three arguments, 5.2+ four |
| NX_UE_HAS_IK_RIG_RIG_SUBDIR | 5.3 | Public IKRig headers move into Rig/: IKRigDefinition.h → Rig/IKRigDefinition.h, Solvers/ → Rig/Solvers/ |
| NX_UE_HAS_IK_RETARGET_SETTINGS_HEADER | 5.2 | Retargeter/IKRetargetSettings.h; related structs are in IKRetargeter.h in 5.0/5.1 |
| NX_UE_HAS_IK_RIG_CONTROLLER_GET_CONTROLLER | 5.2 | UIKRigController::GetController; 5.0/5.1 use GetIKRigController |
| NX_UE_HAS_IK_RETARGETER_GET_CHAIN_MAP_BY_NAME | 5.1 | UIKRetargeter::GetChainMapByName; 5.0 only supports iterating GetAllChainSettings() |
| `NX_UE_HAS_IK_RETARGETER_CONTROLLER_SET_IKRIG` | 5.2 | `UIKRetargeterController::SetIKRig(ERetargetSourceOrTarget)`；5.0 `SetSourceIKRig`/`SetTargetIKRig` |
| NX_UE_HAS_IK_RETARGETER_CONTROLLER_SET_TARGET_IKRIG | 5.0 only | SetTargetIKRig removed in 5.1 without public replacement (private TargetIKRigAsset); SetIKRig takes over in 5.2+ |
| NX_UE_HAS_IK_RETARGETER_GET_IKRIG | 5.4 | UIKRetargeter::GetIKRig(ERetargetSourceOrTarget); older versions use GetSourceIKRig/GetTargetIKRig |
| `NX_UE_HAS_IK_RETARGETER_SET_SOURCE_CHAIN` | 5.4 | `UIKRetargeterController::SetSourceChain` |
| `NX_UE_HAS_IK_RETARGETER_CHAIN_MAPPING` | 5.6 | `GetChainMapping()` / `FRetargetChainPair` |
| `NX_UE_HAS_CONTROL_RIG_BLUEPRINT_LEGACY_HEADER` | 5.7 | `ControlRigBlueprint.h` → `ControlRigBlueprintLegacy.h` |
| NX_UE_HAS_CONTROL_RIG_BLUEPRINT_GET_HIERARCHY | 5.4 | UControlRigBlueprint::GetHierarchy(); 5.3 and earlier access Hierarchy directly |
| NX_UE_HAS_RIG_ELEMENT_GET_FNAME | 5.4 | FRigBaseElement::GetFName(); GetName() now returns FString, while 5.3 GetName() returns FName |
| `NX_UE_HAS_METASOUND_CLASS_GET_DEFAULT_INTERFACE` | 5.6 | `FMetasoundFrontendClass::Interface` → `Get/SetDefaultInterface` |
| NX_UE_HAS_METASOUND_GET_CONST_DOCUMENT | 5.4 | IMetaSoundDocumentInterface::GetConstDocument(); 5.3 uses const GetDocument() |
| NX_UE_HAS_METASOUND_GRAPH_PAGES | 5.5 | FMetasoundFrontendGraphClass::IterateGraphPages; 5.3/5.4 have only one Graph |
| NX_UE_HAS_POSE_SEARCH_NUM_ANIMATION_ASSETS | 5.5 | UPoseSearchDatabase::GetNumAnimationAssets(); 5.4 uses GetAnimationAssets().Num() |
| NX_UE_HAS_IMC_UNMAP_ALL_KEYS_FROM_ACTION | 5.1 | UInputMappingContext::UnmapAllKeysFromAction; 5.0 uses UnmapAction |
| `NX_UE_HAS_STATIC_MESH_CUSTOM_COLLISION_ACCESSOR` | 5.7 | `UStaticMesh::bCustomizedCollision` → `SetCustomizedCollision` |
| `NX_UE_HAS_STATIC_MESH_AUTO_LOD_SCREENSIZE_ACCESSOR` | 5.7 | `UStaticMesh::bAutoComputeLODScreenSize` → `SetAutoComputeLODScreenSize` |
| NX_UE_HAS_JSON_TSHAREDSTRING_KEY | 5.8 | FJsonObject::Values key type FString → UE::FSharedString |
| `NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM` | 5.8 | `GetObjectsWithOuter(bool)` → `GetObjectsWithOuter(EGetObjectsFlags)` |
| `NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR` | 5.8 | `FCoreDelegates::OnPostEngineInit` → `FCoreDelegates::GetOnPostEngineInit()` |
| NX_UE_HAS_STRING_TABLE_SOURCE_DEV_NOTES | 5.8 Editor | SetSourceString has three arguments (key, source, devNotes); Game / 5.7 and earlier retain two, gated by WITH_EDITORONLY_DATA |

<a id="特殊常量宏"></a>
## Special constant macros

| Macro | Value | Description |
|---|---|---|
| NX_UE_HAS_CPF_BLUEPRINT_READWRITE | 0 | CPF_BlueprintReadWrite removed in UE 5.0; all versions use the else branch |
| NX_UE_HAS_WIDGET_ANIM_SET_MOVIE_SCENE | 0 | UWidgetAnimation exposes MovieScene in every version, without SetMovieScene |
| NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS | !5.2 | Before 5.2 GetMasterTracks/AddMasterTrack; 5.2+ GetTracks/AddTrack; old APIs removed in 5.5 |
| NX_UE_HAS_NIAGARA_ADD_EMITTER_VERSION | 5.1 | AddEmitterHandle third argument FGuid EmitterVersion; empty Guid means exposed version |
| NX_UE_HAS_NIAGARA_VERSIONED_EMITTER | 5.1 | Emitter GraphSource/script properties move to FVersionedNiagaraEmitterData; SetSource → SetLatestSource |
| `NX_UE_HAS_NIAGARA_SCRIPT_SET_LATEST_SOURCE` | 5.0 | `UNiagaraScript::SetLatestSource` |
| NX_UE_HAS_NIAGARA_SCRIPT_SET_SOURCE_GUID | 4.27 only | UNiagaraScript::SetSource(Source, VersionGuid) |

<a id="使用规范"></a>
## Usage rules

1. **Do not** use ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION directly for conditional compilation.
2. **Prefer** existing NX_* macros.
3. Add new macros to NexusVersionCompat.h in ascending version order, with inline comments explaining the API change.
4. Name macros NX_UE_HAS_<semantic-description> after the API change rather than the version.

## Unreal Insights Trace

ListTraces / GetTrace are available in UE 4.26+ Editor; Game/DS do not link analysis modules. Semantic macros are centralized in NexusVersionCompat.h; FNexusEditorTraceUtils adapts implementations:

- UE4 uses Trace types; UE5 uses TraceServices / UE::Trace.
- CPU aggregation: 4.26–5.2 use multiple arguments, 5.3–5.5 FCreateAggreationParams, 5.6+ FCreateAggregationParams. All exclude GPU.
- Store paths: 5.3+ use URIs; 5.0–5.2 combine directory/name. UE4 has no physical paths: list Path and read TracePath remain empty; pass TraceId to read a size-bounded stream snapshot.
- UE 5.8 GetTimerReader / Store Create remain adapted through existing semantic macros. Summary fields missing in older versions are omitted.
