// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusAssetEditorUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPackageLedger.h"
#include "Utils/NexusEditorTransaction.h"
#include "NexusEditorServices.h"
#include "Logging/LogMacros.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusAssetEditorUtils, Log, All);

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "EdGraph/EdGraph.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectHash.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

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
#endif // WITH_EDITOR

UWidgetBlueprint* FNexusAssetEditorUtils::LoadWidgetBP(const FString& AssetPath)
{
#if WITH_EDITOR
	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	return Cast<UWidgetBlueprint>(BP);
#else
	(void)AssetPath;
	return nullptr;
#endif
}

UWidget* FNexusAssetEditorUtils::FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
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

bool FNexusAssetEditorUtils::SaveDirtyPackage(UPackage* Package, const FString& PackagePath, const FString& AssetPathHint, bool& bOutDeferred, FString& OutNote)
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
	// 显式 SaveAsset：对象级 Dirty 未必反映到 Package->IsDirty()，统一先标脏再落盘
	Package->MarkPackageDirty();
	if (Asset)
	{
		Asset->MarkPackageDirty();
	}
	return FNexusAssetUtils::SaveNewAsset(Package, Asset, PackagePath, &OutNote);
#endif
}

bool FNexusAssetEditorUtils::CompileAndSaveBlueprint(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath)
{
#if WITH_EDITOR
	if (!Package || !Blueprint) return false;
	if (!CompileWithDiagnostics(Blueprint, MakeShared<FJsonObject>())) return false;
	return FNexusAssetUtils::SaveNewAsset(Package, Blueprint, PackagePath);
#else
	return false;
#endif
}

bool FNexusAssetEditorUtils::CompileWithDiagnostics(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Result)
{
#if WITH_EDITOR
	if (!Blueprint || !Result) return false;
	TArray<FName> NewDefaults;
	if (Blueprint->GeneratedClass) for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		if (!Variable.DefaultValue.IsEmpty() && !Blueprint->GeneratedClass->FindPropertyByName(Variable.VarName)) NewDefaults.Add(Variable.VarName);
	FCompilerResultsLog Log;
	Log.bSilentMode = true;
	const EBlueprintCompileOptions Options = FNexusEditorTransaction::IsTransactionActive()
		? EBlueprintCompileOptions::SkipGarbageCollection : EBlueprintCompileOptions::None;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, Options, &Log);
	const bool bSucceeded = Blueprint->Status != BS_Error && Log.NumErrors == 0;
	if (bSucceeded && NewDefaults.Num() > 0 && Blueprint->GeneratedClass)
	{
		// UE4 重建已有子类时可能给新字段留下类型初始值。仅补新字段的初始值，不覆盖已有字段或非默认值。
		UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		TArray<UObject*> Instances;
		GetObjectsOfClass(Blueprint->GeneratedClass, Instances, true, RF_NoFlags);
		TSet<UBlueprintGeneratedClass*> ChangedClasses;
		TSet<FString> DirtyPackages;
		int32 Changed = 0;
		for (const FName& Name : NewDefaults)
		{
			FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(Name);
			if (!Property) continue;
			FDefaultConstructedPropertyElement Initial(Property);
			void* Value = Property->ContainerPtrToValuePtr<void>(Defaults);
			if (Property->Identical(Value, Initial.GetObjAddress(), PPF_None)) continue;
			for (UObject* Instance : Instances)
			{
				if (!IsValid(Instance) || Instance == Defaults || Instance->GetClass()->FindPropertyByName(Name) != Property) continue;
				void* InstanceValue = Property->ContainerPtrToValuePtr<void>(Instance);
				if (!Property->Identical(InstanceValue, Initial.GetObjAddress(), PPF_None)) continue;
				Instance->SetFlags(RF_Transactional); Instance->Modify();
				Property->CopyCompleteValue(InstanceValue, Value);
				Instance->MarkPackageDirty(); DirtyPackages.Add(Instance->GetOutermost()->GetName()); ++Changed;
				if (Instance->HasAnyFlags(RF_ClassDefaultObject))
					if (UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Instance->GetClass())) ChangedClasses.Add(Class);
			}
		}
		for (UBlueprintGeneratedClass* Class : ChangedClasses) Class->UpdateCustomPropertyListForPostConstruction();
		if (Changed > 0)
		{
			Result->SetNumberField(TEXT("NewVariableDefaultPropagations"), Changed);
			TArray<TSharedPtr<FJsonValue>> Packages;
			for (const FString& Package : DirtyPackages) if (Packages.Num() < 32) Packages.Add(MakeShared<FJsonValueString>(Package));
			Result->SetArrayField(TEXT("NewVariableDefaultPackages"), Packages);
			Result->SetStringField(TEXT("DefaultPropagationNote"), TEXT("New inherited fields updated in loaded defaults/instances; child/level packages remain dirty and require explicit SaveAsset. Package list is bounded to 32."));
		}
	}
	Result->SetBoolField(TEXT("bCompiled"), bSucceeded);
	Result->SetBoolField(TEXT("bHasCompilerErrors"), !bSucceeded);
	Result->SetNumberField(TEXT("CompilerErrorCount"), Log.NumErrors);
	Result->SetNumberField(TEXT("CompilerWarningCount"), Log.NumWarnings);
	Result->SetStringField(TEXT("Status"), FString::FromInt(static_cast<int32>(Blueprint->Status)));
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const auto& Message : Log.Messages)
	{
		if (Messages.Num() >= 128) break;
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Severity"), Message->GetSeverity() <= EMessageSeverity::Error ? TEXT("Error") : (Message->GetSeverity() <= EMessageSeverity::Warning ? TEXT("Warning") : TEXT("Info")));
		Entry->SetStringField(TEXT("Message"), Message->ToText().ToString());
		for (const auto& Token : Message->GetMessageTokens())
		{
			if (Token->GetType() != EMessageToken::EdGraph) continue;
			const auto GraphToken = StaticCastSharedRef<FEdGraphToken>(Token);
			const UEdGraphNode* Node = Cast<UEdGraphNode>(GraphToken->GetGraphObject());
			if (!Node && GraphToken->GetPin()) Node = GraphToken->GetPin()->GetOwningNode();
			if (!Node) continue;
			Entry->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
			if (Node->GetGraph()) Entry->SetStringField(TEXT("GraphName"), Node->GetGraph()->GetName());
			break;
		}
		Messages.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("CompilerMessages"), Messages);
	Result->SetBoolField(TEXT("bCompilerMessagesTruncated"), Log.Messages.Num() > Messages.Num());
	if (!bSucceeded) Result->SetStringField(TEXT("CompileError"), TEXT("Blueprint compilation failed; package was not saved"));
	return bSucceeded;
#else
	return false;
#endif
}

void FNexusAssetEditorUtils::ApplyManageFinalize(
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
	OutTop->SetStringField(TEXT("FinalizeError"), TEXT("manage finalize only available in editor mode"));
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
		UBlueprint* BP = FNexusAssetUtils::LoadAssetTracked<UBlueprint>(AssetPath);
		if (!BP)
		{
			OutTop->SetBoolField(TEXT("bCompiled"), false);
			OutTop->SetStringField(TEXT("CompileError"),
				FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
		}
		else
		{
			CompileWithDiagnostics(BP, OutTop);
		}
		if (OutTop->HasField(TEXT("CompileError"))) return;
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
			OutTop->SetBoolField(TEXT("bSaved"), false);
			OutTop->SetStringField(TEXT("SaveError"),
				FString::Printf(TEXT("Package not found: %s"), *PackagePath));
		}
		else
		{
			bool bDeferred = false;
			FString Note;
			const bool bOk = SaveDirtyPackage(Pkg, PackagePath, AssetPath, bDeferred, Note);
			if (bDeferred)
			{
				OutTop->SetBoolField(TEXT("bDeferred"), true);
				if (!Note.IsEmpty())
				{
					OutTop->SetStringField(TEXT("Note"), Note);
				}
			}
			else
			{
				OutTop->SetBoolField(TEXT("bSaved"), bOk);
				if (!bOk)
				{
					OutTop->SetStringField(TEXT("SaveError"),
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

bool FNexusAssetEditorUtils::NotifyCompileAndSave(UPackage* Package, UBlueprint* Blueprint, const FString& PackagePath)
{
	if (!Package || !Blueprint) return false;
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Blueprint);
	return CompileAndSaveBlueprint(Package, Blueprint, PackagePath);
}

FNexusAssetEditorUtils::FAssetCreateOutcome FNexusAssetEditorUtils::CreateBlueprintAsset(
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
		Out.Error = TEXT("AssetPath is empty");
		return Out;
	}
	if (FPackageName::DoesPackageExist(AssetPath))
	{
		Out.Error = FString::Printf(TEXT("Blueprint already exists: %s"), *AssetPath);
		return Out;
	}

	UClass* ParentClass = FNexusAssetUtils::FindClassWithUPrefix(ParentClassPath);
	if (!ParentClass)
	{
		ParentClass = FNexusAssetUtils::FindClassWithUPrefix(TEXT("A") + ParentClassPath);
	}
	if (!ParentClass && ParentClassPath.Contains(TEXT("/")))
	{
		if (UBlueprint* ParentBP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(ParentClassPath))
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

#if WITH_EDITOR
void NexusInstallAssetEditorUtilsHooks()
{
	FNexusEditorServices::ApplyManageFinalize = &FNexusAssetEditorUtils::ApplyManageFinalize;
}
#else
void NexusInstallAssetEditorUtilsHooks() {}
#endif
