// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusGetRuntimeActorPropertyCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?[
// Actor ????????? helpers
// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?a

// Preset ?????��?? AI ???? diagnose / view ???????????
static bool IsDiagnosePreset(const FString& V)
{
	return V == TEXT("visibility") || V == TEXT("transform") || V == TEXT("world_transform")
		|| V == TEXT("rotation_chain") || V == TEXT("defaults");
}
static bool IsViewPreset(const FString& V)
{
	return V == TEXT("components") || V == TEXT("attach_hierarchy") || V == TEXT("all");
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

	Detail->SetArrayField(TEXT("components"), CompArr);
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
			H->SetStringField(TEXT("attachParent"), Parent->GetName());
			H->SetStringField(TEXT("attachSocket"), Scene->GetAttachSocketName().ToString());
		}

		const FVector& Loc = Scene->GetRelativeLocation();
		const FRotator& Rot = Scene->GetRelativeRotation();
		H->SetStringField(TEXT("relativeLocation"),
			FString::Printf(TEXT("X=%.2f Y=%.2f Z=%.2f"), Loc.X, Loc.Y, Loc.Z));
		H->SetStringField(TEXT("relativeRotation"),
			FString::Printf(TEXT("P=%.2f Y=%.2f R=%.2f"), Rot.Pitch, Rot.Yaw, Rot.Roll));

		HierArr.Add(MakeShared<FJsonValueObject>(H));
	}

	Detail->SetArrayField(TEXT("hierarchy"), HierArr);
}

// ????????????��???????��?? Detail->"results" ???�
static void ResolveBatchActorProperties(
	AActor* Actor,
	const TArray<FString>& Paths,
	TSharedPtr<FJsonObject>& Detail)
{
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FString& Path : Paths)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("propertyPath"), Path);

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
			Entry->SetStringField(TEXT("error"), Error);
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Detail->SetArrayField(TEXT("results"), Results);
}

// ????????? diagnose / section / propertyPaths / propertyPath / ?????????????????
static bool ReadActorPropertyDispatch(
	AActor* Actor,
	const TSharedPtr<FJsonObject>& Arguments,
	TSharedPtr<FJsonObject>& Detail,
	FString& Error)
{
	Detail->SetStringField(TEXT("actorName"),  Actor->GetName());
	Detail->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());

	// ?? A????????
	if (Arguments->HasField(TEXT("diagnose")))
	{
		FString Diagnose = Arguments->GetStringField(TEXT("diagnose")).ToLower();
		Detail->SetStringField(TEXT("diagnose"), Diagnose);

		TArray<FString> Paths;
		TArray<UActorComponent*> AllComponents;
		Actor->GetComponents(AllComponents);

		if (Diagnose == TEXT("visibility"))
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
		else if (Diagnose == TEXT("transform"))
		{
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				FString RN = Root->GetName();
				Paths.Add(RN + TEXT(".RelativeLocation"));
				Paths.Add(RN + TEXT(".RelativeRotation"));
				Paths.Add(RN + TEXT(".RelativeScale3D"));
			}
		}
		else if (Diagnose == TEXT("world_transform"))
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			for (UActorComponent* Comp : AllComponents)
			{
				USceneComponent* Scene = Cast<USceneComponent>(Comp);
				if (!Scene) continue;

				const FTransform WT = Scene->GetComponentTransform();
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("component"), Scene->GetName());

				const FVector& Loc = WT.GetLocation();
				Entry->SetStringField(TEXT("worldLocation"),
					FString::Printf(TEXT("X=%.4f Y=%.4f Z=%.4f"), Loc.X, Loc.Y, Loc.Z));

				const FRotator Rot = WT.Rotator();
				Entry->SetStringField(TEXT("worldRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), Rot.Pitch, Rot.Yaw, Rot.Roll));

				const FVector& Scl = WT.GetScale3D();
				Entry->SetStringField(TEXT("worldScale"),
					FString::Printf(TEXT("X=%.4f Y=%.4f Z=%.4f"), Scl.X, Scl.Y, Scl.Z));

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Detail->SetArrayField(TEXT("results"), Results);
			return true;
		}
		else if (Diagnose == TEXT("rotation_chain"))
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			for (UActorComponent* Comp : AllComponents)
			{
				USceneComponent* Scene = Cast<USceneComponent>(Comp);
				if (!Scene) continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("component"), Scene->GetName());
				if (Scene->IsUsingAbsoluteRotation())
					Entry->SetBoolField(TEXT("bAbsoluteRotation"), true);

				const FRotator& Rel = Scene->GetRelativeRotation();
				Entry->SetStringField(TEXT("relativeRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), Rel.Pitch, Rel.Yaw, Rel.Roll));

				const FRotator World = Scene->GetComponentRotation();
				Entry->SetStringField(TEXT("worldRotation"),
					FString::Printf(TEXT("P=%.4f Y=%.4f R=%.4f"), World.Pitch, World.Yaw, World.Roll));

				if (USceneComponent* Parent = Scene->GetAttachParent())
					Entry->SetStringField(TEXT("attachParent"), Parent->GetName());

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Detail->SetArrayField(TEXT("results"), Results);
			return true;
		}
		else if (Diagnose == TEXT("defaults"))
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
					D->SetStringField(TEXT("path"), It->GetName());
					D->SetStringField(TEXT("default"), DefaultVal);
					D->SetStringField(TEXT("runtime"), RuntimeVal);
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
						D->SetStringField(TEXT("path"), Comp->GetName() + TEXT(".") + It->GetName());
						D->SetStringField(TEXT("default"), DefaultVal);
						D->SetStringField(TEXT("runtime"), RuntimeVal);
						Diffs.Add(MakeShared<FJsonValueObject>(D));
						if (Diffs.Num() >= 100) break;
					}
				}
				if (Diffs.Num() >= 100) break;
			}

		Detail->SetArrayField(TEXT("diffs"), Diffs);
		if (Diffs.Num() >= 100) Detail->SetBoolField(TEXT("truncated"), true);
			return true;
		}
		else
		{
			Error = FString::Printf(TEXT("未知 diagnose 预设: '%s'（支持: visibility/transform/world_transform/rotation_chain/defaults）"), *Diagnose);
		if (IsViewPreset(Diagnose))
		{
			Error += FString::Printf(TEXT("（是否指 view='%s'？）"), *Diagnose);
		}
			return false;
		}

		ResolveBatchActorProperties(Actor, Paths, Detail);
		return true;
	}

	// ?? B??????????
	if (Arguments->HasField(TEXT("view")))
	{
		FString View = Arguments->GetStringField(TEXT("view")).ToLower();
		if (View == TEXT("components"))
		{
			WriteActorComponentsSection(Actor, Detail);
			return true;
		}
		if (View == TEXT("attach_hierarchy"))
		{
			WriteActorAttachHierarchySection(Actor, Detail);
			return true;
		}
		if (View == TEXT("all"))
		{
			WriteActorComponentsSection(Actor, Detail);
			WriteActorAttachHierarchySection(Actor, Detail);
			TArray<TSharedPtr<FJsonValue>> Covered;
			Covered.Add(MakeShared<FJsonValueString>(TEXT("components")));
			Covered.Add(MakeShared<FJsonValueString>(TEXT("attach_hierarchy")));
			Detail->SetArrayField(TEXT("views"), Covered);
			return true;
		}
		Error = FString::Printf(TEXT("未知 view: '%s'（支持: components/attach_hierarchy/all）"), *View);
		if (IsDiagnosePreset(View))
		{
			Error += FString::Printf(TEXT("（是否指 diagnose='%s'？）"), *View);
		}
		return false;
	}

	// ?? C??????????��??
	if (Arguments->HasField(TEXT("propertyPaths")))
	{
		const TArray<TSharedPtr<FJsonValue>>& PathsArr = Arguments->GetArrayField(TEXT("propertyPaths"));
		TArray<FString> Paths;
		for (const TSharedPtr<FJsonValue>& V : PathsArr) { Paths.Add(V->AsString()); }
		ResolveBatchActorProperties(Actor, Paths, Detail);
		return true;
	}

	// ?? D????��?? / ?????
	FString PropertyPath;
	if (Arguments->HasField(TEXT("propertyPath")))
	{
		PropertyPath = Arguments->GetStringField(TEXT("propertyPath"));
	}

	if (PropertyPath.IsEmpty())
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
			C->SetStringField(TEXT("kind"), TEXT("component"));
			Children.Add(MakeShared<FJsonValueObject>(C));
		}
		Detail->SetArrayField(TEXT("children"), Children);
		return true;
	}

	TSharedPtr<FJsonObject> PropResult = MakeShared<FJsonObject>();
	FString PropError;
	if (!FNexusRuntimeUtils::ResolveActorPropertyPath(Actor, PropertyPath, PropResult, PropError))
	{
		Error = PropError;
		return false;
	}
	for (const auto& Pair : PropResult->Values)
	{
		Detail->SetField(Pair.Key, Pair.Value);
	}
	return true;
}

// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?[
// Capability ???
// ?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?T?a

void FGetRuntimeActorPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_runtime_actor_property");
	Out.Description = TEXT("查询运行时 Actor 字段。支持批量 propertyPaths 与组件树。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("target"),        FNexusSchema::Enum(TEXT("分发目标（自动推断）"),
		                                                 { TEXT("actor"), TEXT("widget"), TEXT("asset") }))
		.Prop(TEXT("actorName"),     FNexusSchema::Str(TEXT("Actor 名/标签（先 list_runtime_actors）")))
		.Prop(TEXT("propertyPath"),  FNexusSchema::Str(TEXT("点分路径（单个）")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("点分路径（批量）")))
		.Prop(TEXT("view"),          FNexusSchema::Enum(TEXT("Actor 树视图"),
		                                                 { TEXT("components"), TEXT("attach_hierarchy"), TEXT("all") }))
		.Prop(TEXT("diagnose"),      FNexusSchema::Enum(TEXT("Actor 诊断预设"),
		                                                 { TEXT("visibility"), TEXT("transform"),
		                                                   TEXT("world_transform"), TEXT("rotation_chain"),
		                                                   TEXT("defaults") }))
		.Required({ TEXT("actorName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("components"), TEXT("hierarchy"), TEXT("field"), TEXT("transform"), TEXT("character") };
	Out.RelatedCapabilities = { TEXT("set_runtime_actor_property"), TEXT("list_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("只读字段，不做修改");
}

FCapabilityResult FGetRuntimeActorPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;
		FString Name;
		if (!Arguments->TryGetStringField(TEXT("actorName"), Name) || Name.IsEmpty())
		{
			OutError = TEXT("缺少 actorName");
			return;
		}

		TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();
		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, Name);
		if (!Actor)
		{
			OutEntry->SetStringField(TEXT("actorName"), Name);
			OutEntry->SetStringField(TEXT("error"), TEXT("Actor 未找到"));
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
			return;
		}

		FString Err;
		if (!ReadActorPropertyDispatch(Actor, Arguments, OutEntry, Err))
		{
			OutEntry->SetStringField(TEXT("error"), Err);
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetRuntimeActorPropertyCapability)
