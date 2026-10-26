// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusGetRuntimeActorPropertyCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformProcess.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?[
// Actor ????????? helpers
// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?a

// Preset ?????��?? AI ???? diagnose / view ???????????
static bool IsDiagnosePreset(const FString& V)
{
	return V == TEXT("Visibility") || V == TEXT("Transform") || V == TEXT("WorldTransform")
		|| V == TEXT("RotationChain") || V == TEXT("Defaults");
}
static bool IsViewPreset(const FString& V)
{
	return V == TEXT("Components") || V == TEXT("AttachHierarchy") || V == TEXT("All");
}

static void WriteActorComponentsSection(AActor* Actor, TSharedPtr<FJsonObject>& Detail)
{
	TArray<TSharedPtr<FJsonValue>> CompArr;
	TArray<UActorComponent*> AllComponents;
	Actor->GetComponents(AllComponents);
	for (UActorComponent* Comp : AllComponents)
	{
		if (!Comp) continue;
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Comp->GetName());
		C->SetStringField(TEXT("type"), Comp->GetClass()->GetName());
		if (!Comp->IsRegistered()) { C->SetBoolField(TEXT("bRegistered"), false); }
		if (!Comp->IsActive())     { C->SetBoolField(TEXT("bIsActive"),   false); }
		if (USceneComponent* Scene = Cast<USceneComponent>(Comp))
		{
			if (!Scene->GetVisibleFlag()) { C->SetBoolField(TEXT("bVisible"),       false); }
			if (Scene->bHiddenInGame)     { C->SetBoolField(TEXT("bHiddenInGame"), true);  }
		}
		CompArr.Add(MakeShared<FJsonValueObject>(C));
	}

	Detail->SetArrayField(TEXT("Components"), CompArr);
}

static void WriteActorAttachHierarchySection(AActor* Actor, TSharedPtr<FJsonObject>& Detail)
{
	TArray<TSharedPtr<FJsonValue>> HierArr;
	TArray<UActorComponent*> AllComponents;
	Actor->GetComponents(AllComponents);
	for (UActorComponent* Comp : AllComponents)
	{
		USceneComponent* Scene = Cast<USceneComponent>(Comp);
		if (!Scene) continue;

		TSharedPtr<FJsonObject> H = MakeShared<FJsonObject>();
		H->SetStringField(TEXT("name"), Scene->GetName());
		H->SetStringField(TEXT("type"), Scene->GetClass()->GetName());
		if (USceneComponent* Parent = Scene->GetAttachParent())
		{
			H->SetStringField(TEXT("AttachParent"), Parent->GetName());
			H->SetStringField(TEXT("AttachSocket"), Scene->GetAttachSocketName().ToString());
		}

		const FVector& Loc = Scene->GetRelativeLocation();
		const FRotator& Rot = Scene->GetRelativeRotation();
		H->SetStringField(TEXT("RelativeLocation"),
			FString::Printf(TEXT("X=%.2f Y=%.2f Z=%.2f"), Loc.X, Loc.Y, Loc.Z));
		H->SetStringField(TEXT("RelativeRotation"),
			FString::Printf(TEXT("P=%.2f Y=%.2f R=%.2f"), Rot.Pitch, Rot.Yaw, Rot.Roll));

		HierArr.Add(MakeShared<FJsonValueObject>(H));
	}

	Detail->SetArrayField(TEXT("Hierarchy"), HierArr);
}

// ????????????��???????��?? Detail->"Results" ???�
static void ResolveBatchActorProperties(
	AActor* Actor,
	const TArray<FString>& Paths,
	TSharedPtr<FJsonObject>& Detail)
{
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FString& Path : Paths)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("PropertyPath"), Path);

		TSharedPtr<FJsonObject> PropResult = MakeShared<FJsonObject>();
		FString Error;
		if (FNexusRuntimeUtils::ResolveActorPropertyPath(Actor, Path, PropResult, Error))
		{
			for (const auto& Pair : PropResult->Values)
			{
				Entry->SetField(Pair.Key, Pair.Value);
			}
		}
		else
		{
			Entry->SetStringField(TEXT("Error"), Error);
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Detail->SetArrayField(TEXT("Results"), Results);
}

// 分发：diagnose / view / PropertyPaths[]；否则展开可编辑属性与组件列表
static bool ReadActorPropertyDispatch(
	AActor* Actor,
	const TSharedPtr<FJsonObject>& Arguments,
	TSharedPtr<FJsonObject>& Detail,
	FString& Error)
{
	Detail->SetStringField(TEXT("ActorName"),  Actor->GetName());
	Detail->SetStringField(TEXT("ActorClass"), Actor->GetClass()->GetName());

	const FNexusArgs A(Arguments);
	if (A.Str(TEXT("View")).Equals(TEXT("Context"), ESearchCase::IgnoreCase))
	{
		UWorld* World = Actor->GetWorld();
		APawn* Pawn = Cast<APawn>(Actor);
		AController* Controller = Pawn ? Pawn->GetController() : Cast<AController>(Actor);
		if (!Pawn && Controller) Pawn = Controller->GetPawn();
		TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
		Context->SetNumberField(TEXT("ProcessId"), FPlatformProcess::GetCurrentProcessId());
		Context->SetNumberField(TEXT("WorldId"), World->GetUniqueID());
		Context->SetStringField(TEXT("WorldIdentity"), FNexusRuntimeUtils::GetObjectIdentity(World));
		Context->SetStringField(TEXT("ActorId"), FNexusRuntimeUtils::GetObjectIdentity(Actor));
		Context->SetStringField(TEXT("ActorName"), Actor->GetName());
		Context->SetStringField(TEXT("ActorClass"), Actor->GetClass()->GetPathName());
		Context->SetStringField(TEXT("PawnId"), FNexusRuntimeUtils::GetObjectIdentity(Pawn));
		Context->SetStringField(TEXT("ControllerId"), FNexusRuntimeUtils::GetObjectIdentity(Controller));
		Context->SetBoolField(TEXT("bIsPawn"), Actor->IsA<APawn>());
		Context->SetBoolField(TEXT("bIsController"), Actor->IsA<AController>());
		Context->SetBoolField(TEXT("bAuthority"), Actor->HasAuthority());
		Context->SetBoolField(TEXT("bLocallyControlled"), Pawn ? Pawn->IsLocallyControlled() : Controller && Controller->IsLocalController());
		Context->SetNumberField(TEXT("NetMode"), static_cast<int32>(World->GetNetMode()));
		Context->SetNumberField(TEXT("LocalRole"), static_cast<int32>(Actor->GetLocalRole()));
		Detail->SetObjectField(TEXT("Context"), Context);
		return true;
	}

	// ?? A????????
	if (Arguments->HasField(TEXT("Diagnose")))
	{
		FString Diagnose = A.Str(TEXT("Diagnose")).ToLower();
		Detail->SetStringField(TEXT("Diagnose"), Diagnose);

		TArray<FString> Paths;
		TArray<UActorComponent*> AllComponents;
		Actor->GetComponents(AllComponents);

		if (Diagnose == TEXT("Visibility"))
		{
			Paths.Add(TEXT("bHidden"));
			for (UActorComponent* Comp : AllComponents)
			{
				if (!Comp || !Cast<USceneComponent>(Comp)) continue;
				FString CN = Comp->GetName();
				Paths.Add(CN + TEXT(".bVisible"));
				Paths.Add(CN + TEXT(".bHiddenInGame"));
			}
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				Paths.Add(Root->GetName() + TEXT(".RelativeScale3D"));
			}
		}
		else if (Diagnose == TEXT("Transform"))
		{
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				FString RN = Root->GetName();
				Paths.Add(RN + TEXT(".RelativeLocation"));
				Paths.Add(RN + TEXT(".RelativeRotation"));
				Paths.Add(RN + TEXT(".RelativeScale3D"));
			}
		}
		else if (Diagnose == TEXT("WorldTransform"))
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			for (UActorComponent* Comp : AllComponents)
			{
				USceneComponent* Scene = Cast<USceneComponent>(Comp);
				if (!Scene) continue;

				const FTransform WT = Scene->GetComponentTransform();
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("Component"), Scene->GetName());

				const FVector& Loc = WT.GetLocation();
				Entry->SetStringField(TEXT("WorldLocation"),
					FString::Printf(TEXT("X=%.4f Y=%.4f Z=%.4f"), Loc.X, Loc.Y, Loc.Z));

				const FRotator Rot = WT.Rotator();
				Entry->SetStringField(TEXT("WorldRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), Rot.Pitch, Rot.Yaw, Rot.Roll));

				const FVector& Scl = WT.GetScale3D();
				Entry->SetStringField(TEXT("WorldScale"),
					FString::Printf(TEXT("X=%.4f Y=%.4f Z=%.4f"), Scl.X, Scl.Y, Scl.Z));

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Detail->SetArrayField(TEXT("Results"), Results);
			return true;
		}
		else if (Diagnose == TEXT("RotationChain"))
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			for (UActorComponent* Comp : AllComponents)
			{
				USceneComponent* Scene = Cast<USceneComponent>(Comp);
				if (!Scene) continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("Component"), Scene->GetName());
				if (Scene->IsUsingAbsoluteRotation())
					Entry->SetBoolField(TEXT("bAbsoluteRotation"), true);

				const FRotator& Rel = Scene->GetRelativeRotation();
				Entry->SetStringField(TEXT("RelativeRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), Rel.Pitch, Rel.Yaw, Rel.Roll));

				const FRotator World = Scene->GetComponentRotation();
				Entry->SetStringField(TEXT("WorldRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), World.Pitch, World.Yaw, World.Roll));

				if (USceneComponent* Parent = Scene->GetAttachParent())
					Entry->SetStringField(TEXT("AttachParent"), Parent->GetName());

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Detail->SetArrayField(TEXT("Results"), Results);
			return true;
		}
		else if (Diagnose == TEXT("Defaults"))
		{
			UObject* CDO = Actor->GetClass()->GetDefaultObject();
			TArray<TSharedPtr<FJsonValue>> Diffs;

			for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
				const void* RuntimePtr = It->ContainerPtrToValuePtr<void>(Actor);
				const void* DefaultPtr = It->ContainerPtrToValuePtr<void>(CDO);
				FString RuntimeVal, DefaultVal;
				FNexusPropertyUtils::ExportText(*It, RuntimeVal, RuntimePtr);
				FNexusPropertyUtils::ExportText(*It, DefaultVal, DefaultPtr);
				if (RuntimeVal != DefaultVal)
				{
					TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
					D->SetStringField(TEXT("Path"), It->GetName());
					D->SetStringField(TEXT("default"), DefaultVal);
					D->SetStringField(TEXT("Runtime"), RuntimeVal);
					Diffs.Add(MakeShared<FJsonValueObject>(D));
				}
			}

			for (UActorComponent* Comp : AllComponents)
			{
				if (!Comp) continue;
				UObject* CompCDO = Comp->GetClass()->GetDefaultObject();
				for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
				{
					if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
					const void* RuntimePtr = It->ContainerPtrToValuePtr<void>(Comp);
					const void* DefaultPtr = It->ContainerPtrToValuePtr<void>(CompCDO);
					FString RuntimeVal, DefaultVal;
					FNexusPropertyUtils::ExportText(*It, RuntimeVal, RuntimePtr);
					FNexusPropertyUtils::ExportText(*It, DefaultVal, DefaultPtr);
					if (RuntimeVal != DefaultVal)
					{
						TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
						D->SetStringField(TEXT("Path"), Comp->GetName() + TEXT(".") + It->GetName());
						D->SetStringField(TEXT("default"), DefaultVal);
						D->SetStringField(TEXT("Runtime"), RuntimeVal);
						Diffs.Add(MakeShared<FJsonValueObject>(D));
						if (Diffs.Num() >= 100) break;
					}
				}
				if (Diffs.Num() >= 100) break;
			}

		Detail->SetArrayField(TEXT("Diffs"), Diffs);
		if (Diffs.Num() >= 100) Detail->SetBoolField(TEXT("bTruncated"), true);
			return true;
		}
		else
		{
			Error = FString::Printf(TEXT("Unknown diagnose preset: '%s' (supports: visibility/transform/WorldTransform/RotationChain/defaults)"), *Diagnose);
		if (IsViewPreset(Diagnose))
		{
			Error += FString::Printf(TEXT("(Did you mean View='%s'?)"), *Diagnose);
		}
			return false;
		}

		ResolveBatchActorProperties(Actor, Paths, Detail);
		return true;
	}

	// ?? B??????????
	if (Arguments->HasField(TEXT("View")))
	{
		FString View = A.Str(TEXT("View")).ToLower();
		if (View == TEXT("Components"))
		{
			WriteActorComponentsSection(Actor, Detail);
			return true;
		}
		if (View == TEXT("AttachHierarchy"))
		{
			WriteActorAttachHierarchySection(Actor, Detail);
			return true;
		}
		if (View == TEXT("All"))
		{
			WriteActorComponentsSection(Actor, Detail);
			WriteActorAttachHierarchySection(Actor, Detail);
			TArray<TSharedPtr<FJsonValue>> Covered;
			Covered.Add(MakeShared<FJsonValueString>(TEXT("Components")));
			Covered.Add(MakeShared<FJsonValueString>(TEXT("AttachHierarchy")));
			Detail->SetArrayField(TEXT("Views"), Covered);
			return true;
		}
		Error = FString::Printf(TEXT("Unknown view: '%s' (supports: components/AttachHierarchy/all)"), *View);
		if (IsDiagnosePreset(View))
		{
			Error += FString::Printf(TEXT("(Did you mean Diagnose='%s'?)"), *View);
		}
		return false;
	}

	// 分支 C：仅 PropertyPaths[]；缺省则展开可编辑属性与组件列表
	if (Arguments->HasField(TEXT("PropertyPaths")))
	{
		const TArray<TSharedPtr<FJsonValue>>& PathsArr = Arguments->GetArrayField(TEXT("PropertyPaths"));
		TArray<FString> Paths;
		for (const TSharedPtr<FJsonValue>& V : PathsArr) { Paths.Add(V->AsString()); }
		if (Paths.Num() > 0)
		{
			ResolveBatchActorProperties(Actor, Paths, Detail);
			return true;
		}
	}

	{
		TArray<TSharedPtr<FJsonValue>> Children;
		FNexusPropertyUtils::CollectEditableProperties(Actor, Children);

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (!Comp) continue;
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("name"), Comp->GetName());
			C->SetStringField(TEXT("type"), Comp->GetClass()->GetName());
			C->SetStringField(TEXT("Kind"), TEXT("Component"));
			Children.Add(MakeShared<FJsonValueObject>(C));
		}
		Detail->SetArrayField(TEXT("Children"), Children);
		return true;
	}
}

// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?[
// Capability ???
// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?a

void FNexusGetRuntimeActorPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetRuntimeActorProperty");
	Out.Description = TEXT("Query runtime Actor fields, component tree and identity via View=Context.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("Target"),        FNexusSchema::Enum(TEXT("Dispatch target (auto-inferred)"),
		                                                 { TEXT("Actor"), TEXT("Widget"), TEXT("Asset") }))
		.Prop(TEXT("ActorName"),     FNexusSchema::Str(TEXT("Actor name/tag (ListRuntimeActors first)")))
		.Prop(TEXT("PropertyPaths"), FNexusSchema::StrArr(TEXT("Dot-separated paths (batch)")))
		.Prop(TEXT("View"),          FNexusSchema::Enum(TEXT("Actor view; Context returns lifecycle identity and authority"),
		                                                 { TEXT("Components"), TEXT("AttachHierarchy"), TEXT("All"), TEXT("Context") }))
		.Prop(TEXT("Diagnose"),      FNexusSchema::Enum(TEXT("Actor diagnostic preset"),
		                                                 { TEXT("Visibility"), TEXT("Transform"),
		                                                   TEXT("WorldTransform"), TEXT("RotationChain"),
		                                                   TEXT("Defaults") }))
		.Required({ TEXT("ActorName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("Components"), TEXT("Hierarchy"), TEXT("field"), TEXT("Transform"), TEXT("character") };
	Out.RelatedCapabilities = { TEXT("SetRuntimeActorProperty"), TEXT("ListRuntimeActors") };
	Out.Prerequisites = { TEXT("PIE") };
	Out.WhenToUse = TEXT("Read-only fields; no modifications");
}

FCapabilityResult FNexusGetRuntimeActorPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;
		const FString Name = A.Str(TEXT("ActorName"));

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, Name);
		if (!Actor)
		{
			OutEntry->SetStringField(TEXT("ActorName"), Name);
			OutEntry->SetStringField(TEXT("Error"), TEXT("Actor not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FString Err;
		if (!ReadActorPropertyDispatch(Actor, Arguments, OutEntry, Err))
		{
			OutEntry->SetStringField(TEXT("Error"), Err);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetRuntimeActorPropertyCapability)
