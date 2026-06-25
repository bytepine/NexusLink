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

#define NX_UE_HAS_APP_STYLE            NX_UE_AT_LEAST(5, 0)  // FEditorStyle::Get() → FAppStyle::Get()（Styling/AppStyle.h）
#define NX_UE_HAS_FTSTICKER            NX_UE_AT_LEAST(5, 0)  // FTicker → FTSTicker
#define NX_UE_HAS_SAVE_PACKAGE_ARGS    NX_UE_AT_LEAST(5, 0)  // UPackage::SavePackage(FSavePackageArgs)
#define NX_UE_HAS_MARK_AS_GARBAGE      NX_UE_AT_LEAST(5, 0)  // MarkPendingKill() → MarkAsGarbage()
#define NX_UE_HAS_FIND_FIRST_OBJECT    NX_UE_AT_LEAST(5, 1)  // ANY_PACKAGE → FindFirstObject
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

// ── StateTree 版本兼容 ──
// UE5.5 起 UStateTreeEditorData 新增 GlobalTasks 字段（全局任务节点列表）
#define NX_UE_HAS_STATETREE_GLOBAL_TASKS       NX_UE_AT_LEAST(5, 5)
// UE5.5 起 UMVVMBlueprintView 新增 Events/Conditions 字段
#define NX_UE_HAS_MVVM_EVENTS_CONDITIONS       NX_UE_AT_LEAST(5, 5)

// ── UnLua 版本兼容 ──
// UNLUA_VERSION_MAJOR 由 NexusLink.Build.cs 从 UnLua.uplugin VersionName 自动注入
// 1.X: FLuaContext + GLuaCxt，无 HotReload
// 2.X: UnLua::FLuaEnv，支持 HotReload
#define NX_UNLUA_HAS_LUA_ENV       (WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2) // FLuaEnv API
#define NX_UNLUA_HAS_HOT_RELOAD    (WITH_UNLUA && UNLUA_VERSION_MAJOR >= 2) // FLuaEnv::HotReload()
