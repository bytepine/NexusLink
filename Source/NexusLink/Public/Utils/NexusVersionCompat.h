// Copyright byteyang. All Rights Reserved.

#pragma once

// Utils 层：Common（纯宏文件，无类定义，§7.2 唯一例外）
// ── 跨版本编译兼容宏 ──
// 将 ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION 数值化，使版本比较只需一行。
// 业务代码用法：#if NX_UE_HAS_<语义>（禁止在 NexusVersionCompat.h 外写 NX_UE_AT_LEAST）

#ifndef ENGINE_MAJOR_VERSION
#include "Runtime/Launch/Resources/Version.h"
#endif

#define NX_UE_VERSION  (ENGINE_MAJOR_VERSION * 100 + ENGINE_MINOR_VERSION)

#define NX_UE_AT_LEAST(Major, Minor)  (NX_UE_VERSION >= (Major) * 100 + (Minor))

// ── 语义别名：按 API 变更点命名 ──

#define NX_UE_HAS_UNLOAD_PACKAGES_DIRTY_FLAG  NX_UE_AT_LEAST(5, 0)  // UPackageTools::UnloadPackages(Packages, Err, bUnloadDirtyPackages) 第三参数仅 5.0+（4.26/4.27 只有 2 参重载）
#define NX_UE_HAS_APP_STYLE            NX_UE_AT_LEAST(5, 0)  // FEditorStyle::Get() → FAppStyle::Get()（Styling/AppStyle.h）
#define NX_UE_HAS_FTSTICKER            NX_UE_AT_LEAST(5, 0)  // FTicker → FTSTicker
#define NX_UE_HAS_SAVE_PACKAGE_ARGS    NX_UE_AT_LEAST(5, 0)  // UPackage::SavePackage(FSavePackageArgs)
#define NX_UE_HAS_MARK_AS_GARBAGE      NX_UE_AT_LEAST(5, 0)  // MarkPendingKill() → MarkAsGarbage()
#define NX_UE_HAS_FIND_FIRST_OBJECT    NX_UE_AT_LEAST(5, 1)  // ANY_PACKAGE → FindFirstObject
#define NX_UE_HAS_SKELETAL_BODY_SETUP_HEADER  NX_UE_AT_LEAST(5, 5)  // PhysicsEngine/SkeletalBodySetup.h 单独头文件仅 5.5+
#define NX_UE_HAS_CPF_BLUEPRINT_READWRITE  0                      // CPF_BlueprintReadWrite 在 UE 5.0 移除，所有版本均走 else 分支
#define NX_UE_HAS_CLASS_PATHS          NX_UE_AT_LEAST(5, 1)  // FARFilter::ClassNames → ClassPaths
#define NX_UE_HAS_IMPORT_TEXT_DIRECT   NX_UE_AT_LEAST(5, 1)  // FProperty::ImportText → ImportText_Direct
#define NX_UE_HAS_EXPORT_TEXT_ITEM_DIR NX_UE_AT_LEAST(5, 1)  // ExportTextItem → ExportTextItem_Direct
#define NX_UE_HAS_PROGRESS_GET_PERCENT NX_UE_AT_LEAST(5, 1)  // UProgressBar::Percent → GetPercent()
#define NX_UE_HAS_FTSTICKER_HANDLE     NX_UE_AT_LEAST(5, 0)  // FDelegateHandle → FTSTicker::FDelegateHandle
#define NX_UE_HAS_HTTP_DELEGATE        NX_UE_AT_LEAST(5, 4)  // FHttpRequestHandler 改为 TDelegate
#define NX_UE_HAS_EXPORT_TEXT_DIRECT   NX_UE_AT_LEAST(5, 5)  // ExportTextItem_Direct → ExportText_Direct
#define NX_UE_HAS_STRUCT_UTILS_HEADER  NX_UE_AT_LEAST(5, 5)  // Engine/UserDefinedStruct.h → StructUtils/
#define NX_UE_HAS_MATERIAL_EDITOR_ONLY_DATA NX_UE_AT_LEAST(5, 1) // UMaterial::Expressions → GetEditorOnlyData()
#define NX_UE_HAS_SCOPED_MATERIAL_DOMAIN   NX_UE_AT_LEAST(5, 3) // MD_Surface → EMaterialDomain::Surface
#define NX_UE_HAS_MATERIAL_DOMAIN_HEADER   NX_UE_AT_LEAST(5, 3) // EMaterialDomain 移至独立 MaterialDomain.h（UE5.3+；UE4 中由 Material.h 直接提供）
#define NX_UE_HAS_ALLOW_SHRINKING_ENUM     NX_UE_AT_LEAST(5, 5) // TArray::Pop(bool) → Pop(EAllowShrinking)
#define NX_UE_HAS_ANIM_SEGMENT_ACCESSOR    NX_UE_AT_LEAST(5, 1) // FAnimSegment::AnimReference 在 5.1 开始 deprecated（5.6 变 protected），需用 GetAnimReference()/SetAnimReference()
#define NX_UE_HAS_STATIC_MESH_ACCESSORS    NX_UE_AT_LEAST(4, 27) // UStaticMesh::StaticMaterials/BodySetup → GetStaticMaterials()/GetBodySetup()
#define NX_UE_HAS_SKELETAL_MESH_ACCESSORS  NX_UE_HAS_STATIC_MESH_ACCESSORS // USkeletalMesh::Materials/PhysicsAsset → GetMaterials()/GetPhysicsAsset()
#define NX_UE_HAS_SKELETAL_MESH_SKELETON_ACCESSOR NX_UE_AT_LEAST(4, 27) // USkeletalMesh::Skeleton → GetSkeleton()（4.27 起 deprecated 直访）
#define NX_UE_HAS_UMG_SLOT_GETTERS               NX_UE_AT_LEAST(5, 1)  // UPanelSlot 布局字段 getter（5.1 起 deprecated 直访）
#define NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS   NX_UE_AT_LEAST(5, 0)  // SBoxPanel::FSlot GetPadding/GetHorizontalAlignment；SHorizontalBox::GetSlot
#define NX_UE_HAS_SKELETAL_MATERIAL_COMMON_HEADER NX_UE_AT_LEAST(5, 3) // FSkeletalMaterial 完整定义迁至 SkinnedAssetCommon.h
#define NX_UE_HAS_ANIM_SEQUENCE_LOOP_FIELD NX_UE_AT_LEAST(5, 1) // UAnimSequence::bLoop（UE5.0 无此公开字段）
#define NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL  NX_UE_AT_LEAST(5, 6) // 帧数/帧率走 IAnimationDataModel
// UE 5.5+ RawCurveData 移至 protected，需通过反射访问
#define NX_UE_HAS_RAW_CURVE_DATA_PUBLIC    (!NX_UE_AT_LEAST(5, 5))
// UE 5.3+ FAnimCurveBase::Name (FSmartName) → CurveName (FName)
#define NX_UE_HAS_FLOAT_CURVE_SMART_NAME   (!NX_UE_AT_LEAST(5, 3))
// UE5.0–5.5：GetFrameRate() 已移除，改用 GetSamplingFrameRate() / GetNumberOfSampledKeys()
#define NX_UE_HAS_ANIM_SEQUENCE_SAMPLING_API  (NX_UE_VERSION >= 500 && NX_UE_VERSION < 506)
#define NX_UE_HAS_ANIM_SEQUENCE_ROOT_MOTION_MODE  NX_UE_AT_LEAST(5, 0) // UAnimSequence::RootMotionMode 枚举（UE4 为 bEnableRootMotion*）
#define NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR NX_UE_AT_LEAST(4, 27) // USoundNodeWavePlayer::SoundWave → GetSoundWave()
#define NX_UE_HAS_NIAGARA_EMITTER_HANDLES_API NX_UE_AT_LEAST(5, 0) // UNiagaraSystem::GetNumEmitters/GetEmitterHandle → GetEmitterHandles()
#define NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS NX_UE_AT_LEAST(5, 0) // UNiagaraSystem::GetExposedParameters() 用户参数 Store
#define NX_UE_HAS_TEXTURE_PLATFORM_ACCESSOR NX_UE_AT_LEAST(5, 0) // UTexture2D::PlatformData → GetPlatformData()
#define NX_UE_HAS_TEXTURE_SURFACE_SIZE   NX_UE_AT_LEAST(5, 1) // 贴图尺寸：PlatformData::SizeX/Y → GetSurfaceWidth()/GetSurfaceHeight()
#define NX_UE_HAS_JSON_TSHAREDSTRING_KEY   NX_UE_AT_LEAST(5, 8) // FJsonObject::Values 键类型 FString → UE::FSharedString；迭代 KV.Key 需 FString(*KV.Key)
#define NX_UE_HAS_GET_OBJECTS_FLAGS_ENUM   NX_UE_AT_LEAST(5, 8) // GetObjectsWithOuter(bool) → GetObjectsWithOuter(EGetObjectsFlags)
#define NX_UE_HAS_POST_ENGINE_INIT_ACCESSOR NX_UE_AT_LEAST(5, 8) // FCoreDelegates::OnPostEngineInit → FCoreDelegates::GetOnPostEngineInit()
#define NX_UE_HAS_CONTENT_BROWSER_ITEM_PATH  NX_UE_AT_LEAST(5, 0) // IContentBrowserSingleton::GetCurrentPath() → FContentBrowserItemPath
#define NX_UE_HAS_ASSET_SOFT_OBJECT_PATH     NX_UE_AT_LEAST(5, 1) // FAssetData::GetSoftObjectPath()（UE4 用 ObjectPath）
#define NX_UE_HAS_TAG_SEARCHABLE_REFERENCERS NX_UE_AT_LEAST(5, 0) // AssetRegistry GetReferencers(SearchableName) 按 GameplayTag 查引用
#define NX_UE_HAS_K2_PIN_REAL              NX_UE_AT_LEAST(5, 0) // UEdGraphSchema_K2::PC_Real（UE4 仅 PC_Float）

// ── SoundSubmix 版本兼容 ──
// UE5.1 移除 OutputVolume/WetLevel/DryLevel 直接字段，改用 OutputVolumeModulation/WetLevelModulation/DryLevelModulation
#define NX_UE_HAS_SUBMIX_LINEAR_VOLUME_FIELDS  (!NX_UE_AT_LEAST(5, 1))

// ── Curve/Enum 版本兼容 ──
// UE4.26 FindCurve 返回 FRealCurve*，参数三参：(Name, Context, bTreatMissingAsZero)；UE5 改为 FindCurveUnchecked(Name)
#define NX_UE_HAS_CURVE_TABLE_FIND_UNCHECKED   NX_UE_AT_LEAST(5, 0)
// UE4.26 UserDefinedEnum SetEnumeratorUserDefinedDisplayName 可能不存在，改用反射写 DisplayNameMap
#define NX_UE_HAS_ENUM_EDITOR_SET_DISPLAY_NAME NX_UE_AT_LEAST(4, 27)
// UE4.26 AnimSegment GetAnimReference()/SetAnimReference() accessor
// 注：NX_UE_HAS_ANIM_SEGMENT_ACCESSOR 已在上方定义

// ── StateTree 版本兼容 ──
// UE5.5 起 UStateTreeEditorData 新增 GlobalTasks 字段（全局任务节点列表）
#define NX_UE_HAS_STATETREE_GLOBAL_TASKS       NX_UE_AT_LEAST(5, 5)
// UE5.5 起 UMVVMBlueprintView 新增 Events/Conditions 字段
#define NX_UE_HAS_MVVM_EVENTS_CONDITIONS       NX_UE_AT_LEAST(5, 5)

// ── BlendSpace 版本兼容 ──
// UE 5.0 已废弃 UBlendSpaceBase / BlendSpaceBase.h（5.1+ 正式移除合并）
// 仅 4.26 需要包含该头；5.0 起 BlendSpace.h 已足够
#define NX_UE_HAS_BLEND_SPACE_BASE  (!NX_UE_AT_LEAST(5, 0))
// FBlendSample::bIsValid 是 WITH_EDITORONLY_DATA 字段，Game 目标（WITH_EDITORONLY_DATA=0）不可访问
#define NX_UE_HAS_BLEND_SAMPLE_IS_VALID  (WITH_EDITORONLY_DATA)
// UE 5.5+ UBlendSpace::SampleData 变为 protected，需用 GetBlendSamples() 读，写仍走反射
#define NX_UE_HAS_BLEND_SPACE_SAMPLE_DATA_PUBLIC  (!NX_UE_AT_LEAST(5, 5))
// UE 5.0+ BlendParameters 为 protected，提供 GetBlendParameter(int32) 公开访问器
#define NX_UE_HAS_BLEND_SPACE_GET_BLEND_PARAMETER  NX_UE_AT_LEAST(5, 0)

// ── ControlRig 版本兼容 ──
// UE 5.0+ ControlRig 作为独立插件，UE4.26 为 Experimental 且 API 不稳定
#define NX_UE_HAS_CONTROL_RIG_STABLE  NX_UE_AT_LEAST(5, 0)
// UE 5.4+ URigHierarchy API 变更：GetBones()/GetControls() → ForEach
#define NX_UE_HAS_RIG_HIERARCHY_FOREACH  NX_UE_AT_LEAST(5, 4)

// ── IKRig 版本兼容 ──
// IKRig 插件仅存在于 UE 5.0+
#define NX_UE_HAS_IK_RIG  NX_UE_AT_LEAST(5, 0)

// ── MetaSound 版本兼容 ──
// UE 5.0+ 提供 MetaSound；5.3+ Frontend API 引入 FMetaSoundFrontendDocument
#define NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT  NX_UE_AT_LEAST(5, 3)
// UE 5.1+ 提供 UMetaSoundPatch（可复用子图资产）
#define NX_UE_HAS_METASOUND_PATCH  NX_UE_AT_LEAST(5, 1)

// ── DataLayer / World Partition 版本兼容 ──
// UE 5.1+ 提供 UDataLayerAsset（WorldPartition/DataLayer/DataLayerAsset.h）
#define NX_UE_HAS_DATA_LAYER_ASSET  NX_UE_AT_LEAST(5, 1)
// UE 5.5+ UDataLayerAsset 继承自 UDataAsset（5.4 及更低为 UObject）
#define NX_UE_HAS_DATA_LAYER_ASSET_BASE  NX_UE_AT_LEAST(5, 5)

// ── PCG 版本兼容 ──
// PCG 插件 UE 5.2+ 存在；5.4+ API 趋稳
#define NX_UE_HAS_PCG_STABLE  NX_UE_AT_LEAST(5, 4)
// UE 5.5+ UPCGGraph 节点列表 API 变更
#define NX_UE_HAS_PCG_GRAPH_NODES_ARRAY  (!NX_UE_AT_LEAST(5, 5))

// ── PoseSearch 版本兼容 ──
// PoseSearch 插件 UE 5.4+ 稳定
#define NX_UE_HAS_POSE_SEARCH_STABLE  NX_UE_AT_LEAST(5, 4)

// ── Curve 版本兼容 ──
// UE 4.25+ UCurveTable::GetRowMap() 返回 TMap<FName, FRealCurve*>（UE4 也已迁移）
// 所有受支持版本（4.26+）均用 static_cast<FRichCurve*> 处理
#define NX_UE_HAS_CURVE_TABLE_REAL_CURVE  1
// UCurveTable::FindCurveUnchecked UE5.0+ 新增；UE4 用 FindCurve(FName, FString, bool)
#define NX_UE_HAS_CURVE_TABLE_FIND_UNCHECKED  NX_UE_AT_LEAST(5, 0)

// ── Enum 版本兼容 ──
// FEnumEditorUtils::SetEnumeratorUserDefinedDisplayName UE4.27+ 新增
#define NX_UE_HAS_ENUM_EDITOR_SET_DISPLAY_NAME  NX_UE_AT_LEAST(4, 27)

// ── MovieScene 版本兼容 ──
// UE 5.5 将 Master Tracks API（GetMasterTracks/AddMasterTrack/FindMasterTrack/RemoveMasterTrack）重构
#define NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS  (!NX_UE_AT_LEAST(5, 5))

// ── UnLua 版本兼容 ──
// UNLUA_VERSION_MAJOR 由 NexusLink.Build.cs 从 UnLua.uplugin VersionName 自动注入
// 1.X: FLuaContext + GLuaCxt，无 HotReload
// 2.X: UnLua::FLuaEnv，支持 HotReload
#define NX_UNLUA_HAS_LUA_ENV       (WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2) // FLuaEnv API
#define NX_UNLUA_HAS_HOT_RELOAD    (WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2) // FLuaEnv::HotReload()
