# NexusLink 版本兼容宏参考

> 源文件：`NexusLink/Public/Utils/NexusVersionCompat.h`

## 基础宏

| 宏 | 定义 | 说明 |
|---|---|---|
| `NX_UE_VERSION` | `ENGINE_MAJOR_VERSION * 100 + ENGINE_MINOR_VERSION` | 当前 UE 版本数值化 |
| `NX_UE_AT_LEAST(Major, Minor)` | `NX_UE_VERSION >= Major * 100 + Minor` | 版本比较辅助 |

## 语义别名宏

| 宏名 | 最低版本 | 描述 |
|---|---|---|
| `NX_UE_HAS_FTSTICKER` | 5.0 | `FTicker` → `FTSTicker` |
| `NX_UE_HAS_FTSTICKER_HANDLE` | 5.0 | `FDelegateHandle` → `FTSTicker::FDelegateHandle` |
| `NX_UE_HAS_SAVE_PACKAGE_ARGS` | 5.0 | `UPackage::SavePackage` 改用 `FSavePackageArgs` 参数 |
| `NX_UE_HAS_MARK_AS_GARBAGE` | 5.0 | `MarkPendingKill()` → `MarkAsGarbage()` |
| `NX_UE_HAS_SET_PIE_WORLDS_PAUSED` | 5.0 | `UEditorEngine::SetPIEWorldsPaused`（4.26/4.27 用 `PlayWorld->bDebugPauseExecution`） |
| `NX_UE_HAS_FIND_FIRST_OBJECT` | 5.1 | `ANY_PACKAGE` 查找方式 → `FindFirstObject` |
| `NX_UE_HAS_CLASS_PATHS` | 5.1 | `FARFilter::ClassNames` → `ClassPaths` |
| `NX_UE_HAS_BP_INTERFACE_ASSET_PATH` | 5.1 | `ImplementNewInterface`/`RemoveInterface` 走 `FTopLevelAssetPath`（`FName` 短名 overload 会 C4996） |
| `NX_UE_HAS_IMPORT_TEXT_DIRECT` | 5.1 | `FProperty::ImportText` → `ImportText_Direct` |
| `NX_UE_HAS_EXPORT_TEXT_ITEM_DIR` | 5.1 | `ExportTextItem` → `ExportTextItem_Direct` |
| `NX_UE_HAS_PROGRESS_GET_PERCENT` | 5.1 | `UProgressBar::Percent` → `GetPercent()` |
| `NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA` | 5.1 | `UMaterial::Expressions` → `GetEditorOnlyData()` |
| `NX_UE_HAS_ANIM_SEGMENT_ACCESSOR` | 5.1 | `FAnimSegment::AnimReference` deprecated → `GetAnimReference()`/`SetAnimReference()` |
| `NX_UE_HAS_SCOPED_MATERIAL_DOMAIN` | 5.2 | 与 `MATERIAL_DOMAIN_HEADER` 同门槛（历史别名；枚举值仍为 `MD_*`） |
| `NX_UE_HAS_MATERIAL_DOMAIN_HEADER` | 5.2 | `EMaterialDomain` 移至独立 `MaterialDomain.h`（5.1 及更早由 `Material.h` 完整提供） |
| `NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER` | 5.2 | `FSkeletalMaterial` 完整定义在 `SkinnedAssetCommon.h`；5.2+ `SkeletalMesh.h` 仅前向声明 |
| `NX_UE_HAS_HTTP_DELEGATE` | 5.4 | `FHttpRequestHandler` 改为 `TDelegate` |
| `NX_UE_HAS_EXPORT_TEXT_DIRECT` | 5.5 | `ExportTextItem_Direct` → `ExportText_Direct` |
| `NX_UE_HAS_STRUCT_UTILS_HEADER` | 5.5 | `Engine/UserDefinedStruct.h` → `StructUtils/` |
| `NX_UE_HAS_ALLOW_SHRINKING_ENUM` | 5.5 | `TArray::Pop(bool)` → `Pop(EAllowShrinking)` |
| `NX_UE_HAS_EDGRAPH_PERFORM_ACTION_VECTOR2F` | 5.6 | `FEdGraphSchemaAction::PerformAction` 改 `FVector2f`（`FVector2D`  overload C4996） |
| `NX_UE_HAS_STATIC_MESH_CUSTOM_COLLISION_ACCESSOR` | 5.7 | `UStaticMesh::bCustomizedCollision` → `SetCustomizedCollision` |
| `NX_UE_HAS_STATIC_MESH_AUTO_LOD_SCREENSIZE_ACCESSOR` | 5.7 | `UStaticMesh::bAutoComputeLODScreenSize` → `SetAutoComputeLODScreenSize` |
| `NX_UE_HAS_JSON_TSHAREDSTRING_KEY` | 5.8 | `FJsonObject::Values` 键类型 `FString` → `UE::FSharedString` |
| `NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM` | 5.8 | `GetObjectsWithOuter(bool)` → `GetObjectsWithOuter(EGetObjectsFlags)` |
| `NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR` | 5.8 | `FCoreDelegates::OnPostEngineInit` → `FCoreDelegates::GetOnPostEngineInit()` |
| `NX_UE_HAS_STRING_TABLE_SOURCE_DEV_NOTES` | 5.8 Editor | `SetSourceString` 仅 3 参（key, source, devNotes）；Game / 5.7- 仍为 2 参（`WITH_EDITORONLY_DATA`） |

## 特殊常量宏

| 宏名 | 值 | 说明 |
|---|---|---|
| `NX_UE_HAS_CPF_BLUEPRINT_READWRITE` | `0` | `CPF_BlueprintReadWrite` 在 UE 5.0 移除，所有版本均走 else 分支 |
| `NX_UE_HAS_WIDGET_ANIM_SET_MOVIE_SCENE` | `0` | `UWidgetAnimation` 全版本公开 `MovieScene` 字段，无 `SetMovieScene` |
| `NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS` | `!5.2` | 5.2 前 `GetMasterTracks`/`AddMasterTrack`；5.2+ `GetTracks`/`AddTrack`（5.5 起旧 API 删除） |
| `NX_UE_HAS_NIAGARA_ADD_EMITTER_VERSION` | 5.1 | `AddEmitterHandle` 第三参 `FGuid EmitterVersion`（空 Guid = 暴露版本） |
| `NX_UE_HAS_NIAGARA_VERSIONED_EMITTER` | 5.1 | 发射器 `GraphSource`/脚本属性迁入 `FVersionedNiagaraEmitterData`；`SetSource` → `SetLatestSource` |

## UnLua 兼容宏

| 宏名 | 条件 | 说明 |
|---|---|---|
| `NX_UNLUA_HAS_LUA_ENV` | `WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2` | UnLua 2.x `FLuaEnv` API 可用 |
| `NX_UNLUA_HAS_HOT_RELOAD` | `WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2` | `FLuaEnv::HotReload()` 可用 |

## 使用规范

1. **禁止**直接使用 `ENGINE_MAJOR_VERSION` / `ENGINE_MINOR_VERSION` 做条件编译
2. **优先**复用现有 `NX_*` 宏
3. 若需新增：在 `NexusVersionCompat.h` 中按版本号升序追加，行内注释标明变更内容
4. 宏名格式：`NX_UE_HAS_<语义描述>` — 以 API 变更点而非版本号命名
