// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAssetUtils.h"
#include "NexusCapabilityRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#endif
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif
#include "UObject/UObjectHash.h"
#endif

#if WITH_EDITOR
namespace
{
	/** Live Coding 会话中 SavePackage 已知会崩溃，需降级（仅 Windows 有 LiveCoding 模块）。 */
	bool IsLiveCodingSessionActive()
	{
#if PLATFORM_WINDOWS
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
		{
			return false;
		}
		ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding"));
		return LiveCoding && LiveCoding->IsEnabledForSession();
#else
		return false;
#endif
	}

	/** 按路径提示或包内 RF_Public|RF_Standalone 对象解析主资产。 */
	UObject* ResolvePackageAsset(UPackage* Package, const FString& AssetPathHint)
	{
		if (!Package)
		{
			return nullptr;
		}

		if (!AssetPathHint.IsEmpty())
		{
			UObject* Asset = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPathHint);
			if (Asset && Asset->GetOutermost() == Package)
			{
				return Asset;
			}
		}

		const EObjectFlags RequiredFlags = RF_Public | RF_Standalone;
		UObject* Found = nullptr;
		ForEachObjectWithPackage(Package, [&](UObject* Obj)
		{
			if (!Obj || Obj->IsA(UPackage::StaticClass()) || Obj->HasAnyFlags(RF_Transient))
			{
				return true;
			}
			if (Obj->HasAllFlags(RequiredFlags))
			{
				Found = Obj;
				return false;
			}
			return true;
		});
		return Found;
	}
}
#endif

void FNexusAssetUtils::GetTexture2DSurfaceSize(const UTexture2D* Texture, int32& OutWidth, int32& OutHeight)
{
	OutWidth = 0;
	OutHeight = 0;
	if (!Texture)
	{
		return;
	}
#if NX_UE_HAS_TEXTURE_SURFACE_SIZE
	OutWidth = Texture->GetSurfaceWidth();
	OutHeight = Texture->GetSurfaceHeight();
#elif NX_UE_HAS_TEXTURE_PLATFORM_ACCESSOR
	if (const FTexturePlatformData* PlatformData = Texture->GetPlatformData())
	{
		OutWidth = PlatformData->SizeX;
		OutHeight = PlatformData->SizeY;
	}
#else
	if (Texture->PlatformData)
	{
		OutWidth = Texture->PlatformData->SizeX;
		OutHeight = Texture->PlatformData->SizeY;
	}
#endif
}

void FNexusAssetUtils::AppendAnimSequenceMetadataFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry)
{
	if (!Seq || !Entry.IsValid())
	{
		return;
	}
	// UE4 部分 Anim API 非 const；只读快照，不修改资产。
	UAnimSequence* SeqMut = const_cast<UAnimSequence*>(Seq);
	Entry->SetNumberField(TEXT("length"), SeqMut->GetPlayLength());
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	if (const IAnimationDataModel* Model = SeqMut->GetDataModel())
	{
		Entry->SetNumberField(TEXT("numFrames"), static_cast<double>(Model->GetNumberOfFrames()));
		Entry->SetNumberField(TEXT("frameRate"), Model->GetFrameRate().AsDecimal());
	}
#elif NX_UE_HAS_ANIM_SEQUENCE_SAMPLING_API
	Entry->SetNumberField(TEXT("numFrames"), static_cast<double>(SeqMut->GetNumberOfSampledKeys()));
	Entry->SetNumberField(TEXT("frameRate"), SeqMut->GetSamplingFrameRate().AsDecimal());
#elif WITH_EDITOR
	Entry->SetNumberField(TEXT("numFrames"), SeqMut->GetNumberOfFrames());
	Entry->SetNumberField(TEXT("frameRate"), SeqMut->GetFrameRate());
#endif
#if NX_UE_HAS_ANIM_SEQUENCE_LOOP_FIELD
	Entry->SetBoolField(TEXT("loop"), SeqMut->bLoop);
#endif
}

void FNexusAssetUtils::AppendAnimSequenceNotifyFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry)
{
	if (!Seq || !Entry.IsValid())
	{
		return;
	}
	UAnimSequence* SeqMut = const_cast<UAnimSequence*>(Seq);
	TArray<TSharedPtr<FJsonValue>> NotifyArr;
	const int32 MaxNotifies = FMath::Min(SeqMut->Notifies.Num(), 128);
	for (int32 i = 0; i < MaxNotifies; ++i)
	{
		const FAnimNotifyEvent& Ev = SeqMut->Notifies[i];
		TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
		N->SetNumberField(TEXT("index"), static_cast<double>(i));
		N->SetStringField(TEXT("name"), Ev.NotifyName.ToString());
		N->SetNumberField(TEXT("time"), static_cast<double>(Ev.GetTime()));
		N->SetNumberField(TEXT("duration"), static_cast<double>(Ev.GetDuration()));
		if (Ev.Notify)
		{
			N->SetStringField(TEXT("notifyClass"), Ev.Notify->GetClass()->GetName());
		}
		else if (Ev.NotifyStateClass)
		{
			N->SetStringField(TEXT("notifyClass"), Ev.NotifyStateClass->GetClass()->GetName());
			N->SetBoolField(TEXT("isState"), true);
		}
		NotifyArr.Add(MakeShared<FJsonValueObject>(N));
	}
	Entry->SetArrayField(TEXT("notifies"), NotifyArr);
	if (SeqMut->Notifies.Num() > MaxNotifies)
	{
		Entry->SetNumberField(TEXT("notifiesTruncated"), static_cast<double>(SeqMut->Notifies.Num() - MaxNotifies));
	}
}

const TArray<FStaticMaterial>& FNexusAssetUtils::GetStaticMeshMaterials(const UStaticMesh& Mesh)
{
#if NX_UE_HAS_STATIC_MESH_ACCESSORS
	return Mesh.GetStaticMaterials();
#else
	return Mesh.StaticMaterials;
#endif
}

UBodySetup* FNexusAssetUtils::GetStaticMeshBodySetup(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
#if NX_UE_HAS_STATIC_MESH_ACCESSORS
	return Mesh->GetBodySetup();
#else
	return Mesh->BodySetup;
#endif
}

const TArray<FSkeletalMaterial>& FNexusAssetUtils::GetSkeletalMeshMaterials(const USkeletalMesh& Mesh)
{
#if NX_UE_HAS_SKELETAL_MESH_ACCESSORS
	return Mesh.GetMaterials();
#else
	return Mesh.Materials;
#endif
}

UPhysicsAsset* FNexusAssetUtils::GetSkeletalMeshPhysicsAsset(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
#if NX_UE_HAS_SKELETAL_MESH_ACCESSORS
	return Mesh->GetPhysicsAsset();
#else
	return Mesh->PhysicsAsset;
#endif
}

UClass* FNexusAssetUtils::FindClassWithUPrefix(const FString& ClassName)
{
	if (ClassName.IsEmpty()) return nullptr;

#if NX_UE_HAS_FIND_FIRST_OBJECT
	UClass* Cls = FindFirstObject<UClass>(*ClassName);
#else
	UClass* Cls = FindObject<UClass>(ANY_PACKAGE, *ClassName);
#endif

	// 裸名查不到：加 "U" 前缀再试（UE 反射里大多数 UObject 子类名以 U 开头）
	if (!Cls && !ClassName.StartsWith(TEXT("U")))
	{
		const FString Prefixed = TEXT("U") + ClassName;
#if NX_UE_HAS_FIND_FIRST_OBJECT
		Cls = FindFirstObject<UClass>(*Prefixed);
#else
		Cls = FindObject<UClass>(ANY_PACKAGE, *Prefixed);
#endif
	}

	// 最后按资产路径加载（允许 "/Game/BP_Foo.BP_Foo_C" 之类的完整 path）
	if (!Cls)
	{
		Cls = LoadObject<UClass>(nullptr, *ClassName);
	}
	return Cls;
}

UWidgetBlueprint* FNexusAssetUtils::LoadWidgetBP(const FString& AssetPath)
{
#if WITH_EDITOR
	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	return Cast<UWidgetBlueprint>(BP);
#else
	(void)AssetPath;
	return nullptr;
#endif
}

UWidget* FNexusAssetUtils::FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
{
#if WITH_EDITOR
	if (!WBP || !WBP->WidgetTree) return nullptr;
	UWidget* Found = nullptr;
	WBP->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (W && W->GetName() == WidgetName)
		{
			Found = W;
		}
	});
	return Found;
#else
	(void)WBP; (void)WidgetName;
	return nullptr;
#endif
}

bool FNexusAssetUtils::SaveNewAsset(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	if (!Package) return false;

	const FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	bool bSaved = false;
#if NX_UE_HAS_SAVE_PACKAGE_ARGS
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	bSaved = UPackage::SavePackage(Package, Asset, *FilePath, SaveArgs);
#else
	bSaved = UPackage::SavePackage(Package, Asset, RF_Public | RF_Standalone, *FilePath, GError);
#endif
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NexusLink] SaveNewAsset failed: %s"), *FilePath);
	}
	return bSaved;
}

bool FNexusAssetUtils::SaveDirtyPackage(UPackage* Package, const FString& PackagePath, const FString& AssetPathHint, bool& bOutDeferred, FString& OutNote)
{
#if !WITH_EDITOR
	(void)Package;
	(void)PackagePath;
	(void)AssetPathHint;
	bOutDeferred = false;
	OutNote.Reset();
	return false;
#else
	bOutDeferred = false;
	OutNote.Reset();
	if (!Package)
	{
		return false;
	}
	if (!IsInGameThread())
	{
		OutNote = TEXT("SaveDirtyPackage 必须在 GameThread 调用");
		return false;
	}
	if (IsLiveCodingSessionActive())
	{
		Package->MarkPackageDirty();
		bOutDeferred = true;
		OutNote = TEXT("Live Coding 已开启，已标记 Dirty，请关闭 Live Coding 后重试或手动保存");
		return false;
	}

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		OutNote = TEXT("路径转换失败");
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);

	UObject* Asset = ResolvePackageAsset(Package, AssetPathHint);
	// 显式 save_asset：对象级 Dirty 未必反映到 Package->IsDirty()，统一先标脏再落盘
	Package->MarkPackageDirty();
	if (Asset)
	{
		Asset->MarkPackageDirty();
	}
	return SaveNewAsset(Package, Asset, PackagePath);
#endif
}

bool FNexusAssetUtils::CompileAndSaveBlueprint(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath)
{
#if WITH_EDITOR
	if (!Package || !Blueprint) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return SaveNewAsset(Package, Blueprint, PackagePath);
#else
	return false;
#endif
}

FString FNexusAssetUtils::PackagePathOf(const UObject* Obj)
{
	return (Obj && Obj->GetOutermost()) ? Obj->GetOutermost()->GetName() : FString();
}

bool FNexusAssetUtils::NotifyAndSaveCreated(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	if (!Package || !Asset) return false;
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
	return SaveNewAsset(Package, Asset, PackagePath);
}

bool FNexusAssetUtils::NotifyCompileAndSave(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath)
{
	if (!Package || !Blueprint) return false;
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Blueprint);
	return CompileAndSaveBlueprint(Package, Blueprint, PackagePath);
}

void FNexusAssetUtils::ResolveRecommendedCapabilities(
	const FString& AssetType,
	FString& OutRecommendedGet,
	FString& OutRecommendedManage)
{
	FNexusCapabilityRegistry::Get().ResolveSearchAssetRoute(
		AssetType, OutRecommendedGet, OutRecommendedManage);
}

UUserDefinedStruct* FNexusAssetUtils::FindStructByName(const FString& StructName)
{
	if (StructName.IsEmpty()) return nullptr;

	// 如果包含路径分隔符，尝试直接作为资产路径加载
	if (StructName.Contains(TEXT("/")))
	{
		if (UUserDefinedStruct* S = LoadObject<UUserDefinedStruct>(nullptr, *StructName))
			return S;
		// fallback：尝试 Path.ShortName 形式
		const FString WithSuffix = StructName + TEXT(".") + FPackageName::GetShortName(StructName);
		return LoadObject<UUserDefinedStruct>(nullptr, *WithSuffix);
	}

	// 短名查找：先尝试原名，再尝试去掉 F 前缀（UDS 内部名通常无 F 前缀）
	FString SearchName = StructName;
	if (SearchName.StartsWith(TEXT("F")))
		SearchName = SearchName.Mid(1);

	for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
	{
		if (It->GetName() == SearchName || It->GetName() == StructName)
			return *It;
	}
	return nullptr;
}
