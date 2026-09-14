// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPackageLedger.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusAssetUtils, Log, All);

#include "Engine/Blueprint.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Texture2D.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimCurveTypes.h"
#include "UObject/UnrealType.h"
#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
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

static FName NexusGetFloatCurveName(const FFloatCurve& FC)
{
#if NX_UE_HAS_FLOAT_CURVE_SMART_NAME
	return FC.Name.DisplayName;
#else
	return FC.GetName();
#endif
}

static void AppendFloatCurvesToJson(const TArray<FFloatCurve>& FloatCurves, TSharedPtr<FJsonObject>& Entry)
{
	TArray<TSharedPtr<FJsonValue>> CurvesArr;
	constexpr int32 MaxCurves = 64;
	constexpr int32 MaxKeysPerCurve = 64;
	const int32 CurveCount = FMath::Min(FloatCurves.Num(), MaxCurves);
	for (int32 Ci = 0; Ci < CurveCount; ++Ci)
	{
		const FFloatCurve& FC = FloatCurves[Ci];
		TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("name"), NexusGetFloatCurveName(FC).ToString());

		TArray<float> Times, Values;
		const_cast<FFloatCurve&>(FC).GetKeys(Times, Values);
		const int32 KeyCount = FMath::Min(Times.Num(), MaxKeysPerCurve);
		CObj->SetNumberField(TEXT("keyCount"), static_cast<double>(Times.Num()));

		TArray<TSharedPtr<FJsonValue>> KeysArr;
		for (int32 Ki = 0; Ki < KeyCount; ++Ki)
		{
			TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
			K->SetNumberField(TEXT("time"), static_cast<double>(Times[Ki]));
			K->SetNumberField(TEXT("value"), static_cast<double>(Values.IsValidIndex(Ki) ? Values[Ki] : 0.f));
			KeysArr.Add(MakeShared<FJsonValueObject>(K));
		}
		CObj->SetArrayField(TEXT("keys"), KeysArr);
		if (Times.Num() > MaxKeysPerCurve)
		{
			CObj->SetNumberField(TEXT("keysTruncated"), static_cast<double>(Times.Num() - MaxKeysPerCurve));
		}
		CurvesArr.Add(MakeShared<FJsonValueObject>(CObj));
	}
	Entry->SetArrayField(TEXT("curves"), CurvesArr);
	if (FloatCurves.Num() > MaxCurves)
	{
		Entry->SetNumberField(TEXT("curvesTruncated"), static_cast<double>(FloatCurves.Num() - MaxCurves));
	}
}

void FNexusAssetUtils::AppendAnimSequenceCurveFields(const UAnimSequence* Seq, TSharedPtr<FJsonObject>& Entry)
{
	if (!Seq || !Entry.IsValid())
	{
		return;
	}
	UAnimSequence* SeqMut = const_cast<UAnimSequence*>(Seq);

#if NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL && WITH_EDITOR
	if (const IAnimationDataModel* Model = SeqMut->GetDataModel())
	{
		AppendFloatCurvesToJson(Model->GetFloatCurves(), Entry);
		return;
	}
#endif

	// UE4 / UE5.5 前公开、5.5+ protected：统一反射取 RawCurveData
	FStructProperty* StructProp = FindFProperty<FStructProperty>(SeqMut->GetClass(), TEXT("RawCurveData"));
	if (!StructProp)
	{
		Entry->SetArrayField(TEXT("curves"), TArray<TSharedPtr<FJsonValue>>());
		return;
	}
	FRawCurveTracks* Tracks = StructProp->ContainerPtrToValuePtr<FRawCurveTracks>(SeqMut);
	if (!Tracks)
	{
		Entry->SetArrayField(TEXT("curves"), TArray<TSharedPtr<FJsonValue>>());
		return;
	}
	AppendFloatCurvesToJson(Tracks->FloatCurves, Entry);
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
		UE_LOG(LogNexusAssetUtils, Warning, TEXT("[NexusLink] SaveNewAsset failed: %s"), *FilePath);
	}
	return bSaved;
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

void FNexusAssetUtils::AppendBlueprintMetaFields(const UBlueprint* BP, TSharedPtr<FJsonObject>& OutEntry)
{
	if (!BP || !OutEntry.IsValid()) return;

	FString TypeStr;
	switch (BP->BlueprintType)
	{
	case BPTYPE_Normal:          TypeStr = TEXT("normal"); break;
	case BPTYPE_Const:           TypeStr = TEXT("const"); break;
	case BPTYPE_MacroLibrary:    TypeStr = TEXT("macroLibrary"); break;
	case BPTYPE_Interface:       TypeStr = TEXT("interface"); break;
	case BPTYPE_LevelScript:     TypeStr = TEXT("levelScript"); break;
	case BPTYPE_FunctionLibrary: TypeStr = TEXT("functionLibrary"); break;
	default:                     TypeStr = TEXT("unknown"); break;
	}
	OutEntry->SetStringField(TEXT("blueprintType"), TypeStr);

	TArray<TSharedPtr<FJsonValue>> Ifaces;
#if WITH_EDITOR
	for (const FBPInterfaceDescription& Desc : BP->ImplementedInterfaces)
	{
		UClass* Iface = Desc.Interface;
		if (Iface)
		{
			Ifaces.Add(MakeShared<FJsonValueString>(Iface->GetName()));
		}
	}
#endif
	OutEntry->SetArrayField(TEXT("implementedInterfaces"), Ifaces);
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

FNexusAssetUtils::FAssetCreateOutcome FNexusAssetUtils::CreatePlainAsset(
	const FString& AssetPath,
	UClass* AssetClass,
	EObjectFlags Flags,
	bool bNotifyAndSave)
{
	FAssetCreateOutcome Out;
	if (!AssetClass)
	{
		Out.Error = TEXT("Asset class is null");
		return Out;
	}
	if (AssetPath.IsEmpty())
	{
		Out.Error = TEXT("assetPath is empty");
		return Out;
	}
	if (StaticFindObject(AssetClass, nullptr, *AssetPath)
		|| FPackageName::DoesPackageExist(AssetPath))
	{
		Out.Error = FString::Printf(TEXT("%s already exists: %s"), *AssetClass->GetName(), *AssetPath);
		return Out;
	}

	FText PackageNameError;
	if (!FPackageName::IsValidLongPackageName(AssetPath, false, &PackageNameError))
	{
		Out.Error = FString::Printf(TEXT("Invalid package path '%s': %s"), *AssetPath, *PackageNameError.ToString());
		return Out;
	}

	UPackage* Package = CreatePackage(*AssetPath);
	if (!Package)
	{
		Out.Error = FString::Printf(TEXT("Failed to create package: %s"), *AssetPath);
		return Out;
	}

	const FString AssetName = FPaths::GetBaseFilename(AssetPath);
	UObject* Asset = NewObject<UObject>(Package, AssetClass, *AssetName, Flags);
	if (!Asset)
	{
		Out.Error = FString::Printf(TEXT("Failed to create %s: %s"), *AssetClass->GetName(), *AssetPath);
		return Out;
	}

	if (bNotifyAndSave)
	{
		NotifyAndSaveCreated(Package, Asset, AssetPath);
	}
	Out.Asset = Asset;
	return Out;
}

