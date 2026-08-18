// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusSetRuntimeActorPropertyCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

static void WriteActorPropertyImpl(
	UWorld* World,
	const FString& ActorName,
	const FString& PropertyPath,
	const FString& NewValue,
	TSharedPtr<FJsonObject>& OutEntry)
{
	OutEntry->SetStringField(TEXT("actorName"),    ActorName);
	OutEntry->SetStringField(TEXT("propertyPath"), PropertyPath);

	if (ActorName.IsEmpty())    { OutEntry->SetStringField(TEXT("error"), TEXT("Missing actorName")); return; }
	if (PropertyPath.IsEmpty()) { OutEntry->SetStringField(TEXT("error"), TEXT("Missing propertyPath")); return; }

	AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
	if (!Actor) { OutEntry->SetStringField(TEXT("error"), TEXT("Actor not found")); return; }

	TArray<FString> Segs;
	PropertyPath.ParseIntoArray(Segs, TEXT("."), true);
	if (Segs.Num() == 0) { OutEntry->SetStringField(TEXT("error"), TEXT("propertyPath is empty")); return; }

	// 组件前缀路由
	UObject* Target = Actor;
	int32 StartSeg = 0;
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
		if (Comp && Comp->GetName() == Segs[0]) { Target = Comp; StartSeg = 1; break; }

	if (StartSeg >= Segs.Num())
	{
		OutEntry->SetStringField(TEXT("error"), TEXT("Cannot assign component itself; specify sub-property"));
		return;
	}

	FString OldVal, ActualVal, Error;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(Target, Segs, StartSeg, NewValue, OldVal, ActualVal, Error))
	{
		OutEntry->SetStringField(TEXT("error"), Error);
		return;
	}
	OutEntry->SetStringField(TEXT("resolvedActor"), Actor->GetName());
	if (!OldVal.IsEmpty())    OutEntry->SetStringField(TEXT("oldValue"),    OldVal);
	if (!ActualVal.IsEmpty()) OutEntry->SetStringField(TEXT("newValue"), ActualVal);
}

void FSetRuntimeActorPropertyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("set_runtime_actor_property");
	Out.Description = TEXT("Batch modify runtime Actor editable fields. One result per updates[] item.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedRef<FJsonObject> ItemProps = MakeShared<FJsonObject>();
		ItemProps->SetObjectField(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Dot-separated path")));
		ItemProps->SetObjectField(TEXT("value"),        FNexusSchema::Str(TEXT("New value string")));
		ItemProps->SetObjectField(TEXT("actorName"),    FNexusSchema::Str(TEXT("Actor name (actor target)")));
		ItemProps->SetObjectField(TEXT("widgetName"),   FNexusSchema::Str(TEXT("Widget name (widget/asset target)")));
		ItemProps->SetObjectField(TEXT("ownerClass"),   FNexusSchema::Str(TEXT("UserWidget filter (widget target)")));

		TSharedRef<FJsonObject> ItemSchema = MakeShared<FJsonObject>();
		ItemSchema->SetStringField(TEXT("type"), TEXT("object"));
		ItemSchema->SetObjectField(TEXT("properties"), ItemProps);
		TArray<TSharedPtr<FJsonValue>> ItemReq;
		ItemReq.Add(MakeShared<FJsonValueString>(TEXT("propertyPath")));
		ItemReq.Add(MakeShared<FJsonValueString>(TEXT("value")));
		ItemSchema->SetArrayField(TEXT("required"), ItemReq);

		return FNexusSchema::Object()
		.Prop(TEXT("target"),  FNexusSchema::Enum(TEXT("Dispatch target (auto-inferred)"),
		                                          { TEXT("actor"), TEXT("widget"), TEXT("asset") }))
		.Prop(TEXT("updates"), FNexusSchema::ArrayOf(TEXT("Batch update"), ItemSchema))
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("write"), TEXT("field"), TEXT("change"), TEXT("value"), TEXT("character") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_property") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("Modify runtime Actor live fields");
}

FCapabilityResult FSetRuntimeActorPropertyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		const TArray<TSharedPtr<FJsonValue>>* UpdatesArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("updates"), UpdatesArr) || !UpdatesArr)
		{
			OutError = TEXT("Missing updates");
			return;
		}

		for (const TSharedPtr<FJsonValue>& Val : *UpdatesArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid update item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			FString PropertyPath, NewValue, ActorName;
			Item->TryGetStringField(TEXT("propertyPath"), PropertyPath);
			Item->TryGetStringField(TEXT("value"),        NewValue);
			Item->TryGetStringField(TEXT("actorName"),    ActorName);

			if (PropertyPath.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Each update requires propertyPath"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			WriteActorPropertyImpl(World, ActorName, PropertyPath, NewValue, OutEntry);
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}
	
	});
}

REGISTER_MCP_CAPABILITY(FSetRuntimeActorPropertyCapability)
