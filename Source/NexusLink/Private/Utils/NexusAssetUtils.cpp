// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPackageLedger.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusAssetUtils, Log, All);

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
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
#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectGlobals.h"
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
	/** Live Coding 会话中 SavePackage 已知会崩溃，需降级（仅 Windows 有 LiveCoding 模块）。 */
	static bool IsLiveCodingSessionActive()
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
	static UObject* ResolvePackageAsset(UPackage* Package, const FString& AssetPathHint)
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
		UE_LOG(LogNexusAssetUtils, Warning, TEXT("[NexusLink] SaveNewAsset failed: %s"), *FilePath);
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
		OutNote = TEXT("SaveDirtyPackage must be called on GameThread");
		return false;
	}
	if (IsLiveCodingSessionActive())
	{
		Package->MarkPackageDirty();
		bOutDeferred = true;
		OutNote = TEXT("Live Coding enabled; marked Dirty. Disable Live Coding and retry or save manually");
		return false;
	}

	FString PackageFileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		OutNote = TEXT("Path conversion failed");
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

void FNexusAssetUtils::ApplyManageFinalize(
	const FString& AssetPath,
	bool bCompile,
	bool bSaveToDisk,
	TSharedPtr<FJsonObject>& OutTop)
{
	if (!OutTop.IsValid() || AssetPath.IsEmpty() || (!bCompile && !bSaveToDisk))
	{
		return;
	}

#if !WITH_EDITOR
	OutTop->SetStringField(TEXT("finalizeError"), TEXT("manage finalize only available in editor mode"));
	return;
#else
	FString PackagePath = AssetPath;
	int32 DotIdx;
	if (PackagePath.FindLastChar(TEXT('.'), DotIdx))
	{
		PackagePath = PackagePath.Left(DotIdx);
	}

	if (bCompile)
	{
		UBlueprint* BP = LoadAssetTracked<UBlueprint>(AssetPath);
		if (!BP)
		{
			OutTop->SetBoolField(TEXT("compiled"), false);
			OutTop->SetStringField(TEXT("compileError"),
				FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		}
		else
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			OutTop->SetBoolField(TEXT("compiled"), true);
			OutTop->SetBoolField(TEXT("hasCompilerErrors"), BP->Status == BS_Error);
			OutTop->SetStringField(TEXT("status"),
				FString::Printf(TEXT("%d"), static_cast<int32>(BP->Status)));
		}
	}

	if (bSaveToDisk)
	{
		UPackage* Pkg = FindPackage(nullptr, *PackagePath);
		if (!Pkg)
		{
			Pkg = LoadPackage(nullptr, *PackagePath, LOAD_None);
		}
		if (!Pkg)
		{
			OutTop->SetBoolField(TEXT("saved"), false);
			OutTop->SetStringField(TEXT("saveError"),
				FString::Printf(TEXT("Package not found: %s"), *PackagePath));
		}
		else
		{
			bool bDeferred = false;
			FString Note;
			const bool bOk = SaveDirtyPackage(Pkg, PackagePath, AssetPath, bDeferred, Note);
			if (bDeferred)
			{
				OutTop->SetBoolField(TEXT("deferred"), true);
				if (!Note.IsEmpty())
				{
					OutTop->SetStringField(TEXT("note"), Note);
				}
			}
			else
			{
				OutTop->SetBoolField(TEXT("saved"), bOk);
				if (!bOk)
				{
					OutTop->SetStringField(TEXT("saveError"),
						Note.IsEmpty()
							? FString::Printf(TEXT("SavePackage failed: %s"), *PackagePath)
							: Note);
				}
			}
			FNexusPackageLedger::MaybeFlush();
		}
	}
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
	EObjectFlags Flags)
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

	NotifyAndSaveCreated(Package, Asset, AssetPath);
	Out.Asset = Asset;
	return Out;
}

FNexusAssetUtils::FAssetCreateOutcome FNexusAssetUtils::CreateBlueprintAsset(
	const FString& AssetPath,
	const FString& ParentClassPath,
	UClass* ExpectedBase,
	UClass* BlueprintClass,
	UClass* GeneratedClass,
	bool bCompileAndSave)
{
	FAssetCreateOutcome Out;
#if !WITH_EDITOR
	(void)AssetPath;
	(void)ParentClassPath;
	(void)ExpectedBase;
	(void)BlueprintClass;
	(void)GeneratedClass;
	(void)bCompileAndSave;
	Out.Error = TEXT("Blueprint creation is editor-only");
	return Out;
#else
	if (AssetPath.IsEmpty())
	{
		Out.Error = TEXT("assetPath is empty");
		return Out;
	}
	if (FPackageName::DoesPackageExist(AssetPath))
	{
		Out.Error = FString::Printf(TEXT("Blueprint already exists: %s"), *AssetPath);
		return Out;
	}

	UClass* ParentClass = FindClassWithUPrefix(ParentClassPath);
	if (!ParentClass)
	{
		ParentClass = FindClassWithUPrefix(TEXT("A") + ParentClassPath);
	}
	if (!ParentClass && ParentClassPath.Contains(TEXT("/")))
	{
		if (UBlueprint* ParentBP = LoadAssetWithFallback<UBlueprint>(ParentClassPath))
		{
			ParentClass = ParentBP->GeneratedClass;
			if (!ParentClass)
			{
				Out.Error = FString::Printf(TEXT("Parent blueprint has no GeneratedClass: %s"), *ParentClassPath);
				return Out;
			}
		}
	}
	if (!ParentClass)
	{
		Out.Error = FString::Printf(TEXT("Parent class not found: %s"), *ParentClassPath);
		return Out;
	}
	if (!ParentClass->IsChildOf(UObject::StaticClass()))
	{
		Out.Error = FString::Printf(TEXT("Invalid parent class (not a UObject): %s"), *ParentClass->GetName());
		return Out;
	}
	if (ExpectedBase && !ParentClass->IsChildOf(ExpectedBase))
	{
		Out.Error = FString::Printf(
			TEXT("Parent class '%s' is not a subclass of %s"),
			*ParentClass->GetName(), *ExpectedBase->GetName());
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

	if (!BlueprintClass)
	{
		BlueprintClass = UBlueprint::StaticClass();
	}
	if (!GeneratedClass)
	{
		GeneratedClass = UBlueprintGeneratedClass::StaticClass();
	}

	const FString AssetName = FPaths::GetBaseFilename(AssetPath);
	const bool bIsInterface = ParentClass->HasAnyClassFlags(CLASS_Interface);
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, Package, *AssetName,
		bIsInterface ? BPTYPE_Interface : BPTYPE_Normal,
		BlueprintClass, GeneratedClass);
	if (!NewBP)
	{
		Out.Error = FString::Printf(TEXT("Failed to create Blueprint: %s"), *AssetPath);
		return Out;
	}

	if (bCompileAndSave)
	{
		NotifyCompileAndSave(Package, NewBP, AssetPath);
	}
	Out.Asset = NewBP;
	return Out;
#endif
}
