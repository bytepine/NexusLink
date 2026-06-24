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
| `NX_UE_HAS_FIND_FIRST_OBJECT` | 5.1 | `ANY_PACKAGE` 查找方式 → `FindFirstObject` |
| `NX_UE_HAS_CLASS_PATHS` | 5.1 | `FARFilter::ClassNames` → `ClassPaths` |
| `NX_UE_HAS_IMPORT_TEXT_DIRECT` | 5.1 | `FProperty::ImportText` → `ImportText_Direct` |
| `NX_UE_HAS_EXPORT_TEXT_ITEM_DIR` | 5.1 | `ExportTextItem` → `ExportTextItem_Direct` |
| `NX_UE_HAS_PROGRESS_GET_PERCENT` | 5.1 | `UProgressBar::Percent` → `GetPercent()` |
| `NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA` | 5.1 | `UMaterial::Expressions` → `GetEditorOnlyData()` |
| `NX_UE_HAS_ANIM_SEGMENT_ACCESSOR` | 5.1 | `FAnimSegment::AnimReference` deprecated → `GetAnimReference()`/`SetAnimReference()` |
| `NX_UE_HAS_SCOPED_MATERIAL_DOMAIN` | 5.3 | `MD_Surface` → `EMaterialDomain::Surface` |
| `NX_UE_HAS_MATERIAL_DOMAIN_HEADER` | 5.3 | `EMaterialDomain` 移至独立 `MaterialDomain.h` |
| `NX_UE_HAS_HTTP_DELEGATE` | 5.4 | `FHttpRequestHandler` 改为 `TDelegate` |
| `NX_UE_HAS_EXPORT_TEXT_DIRECT` | 5.5 | `ExportTextItem_Direct` → `ExportText_Direct` |
| `NX_UE_HAS_STRUCT_UTILS_HEADER` | 5.5 | `Engine/UserDefinedStruct.h` → `StructUtils/` |
| `NX_UE_HAS_ALLOW_SHRINKING_ENUM` | 5.5 | `TArray::Pop(bool)` → `Pop(EAllowShrinking)` |
| `NX_UE_HAS_JSON_TSHAREDSTRING_KEY` | 5.8 | `FJsonObject::Values` 键类型 `FString` → `UE::FSharedString` |
| `NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM` | 5.8 | `GetObjectsWithOuter(bool)` → `GetObjectsWithOuter(EGetObjectsFlags)` |
| `NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR` | 5.8 | `FCoreDelegates::OnPostEngineInit` → `FCoreDelegates::GetOnPostEngineInit()` |

## 特殊常量宏

| 宏名 | 值 | 说明 |
|---|---|---|
| `NX_UE_HAS_CPF_BLUEPRINT_READWRITE` | `0` | `CPF_BlueprintReadWrite` 在 UE 5.0 移除，所有版本均走 else 分支 |

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
