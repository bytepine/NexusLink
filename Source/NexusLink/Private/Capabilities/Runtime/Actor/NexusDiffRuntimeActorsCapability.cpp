// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusDiffRuntimeActorsCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

struct FComparePairResultCap
{
	TArray<TSharedPtr<FJsonValue>> Diffs;
	bool bTruncated = false;
};

static FComparePairResultCap ComparePairCap(AActor* ActorA, AActor* ActorB, const TSharedPtr<FJsonObject>& Arguments)
{
	FComparePairResultCap R;
	static constexpr int32 MaxDiffs = 50;

	auto AddDiff = [&R](const FString& Path, const FString& Type, const FString& ValA, const FString& ValB)
	{
		TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
		Diff->SetStringField(TEXT("path"), Path);
		if (!Type.IsEmpty()) { Diff->SetStringField(TEXT("type"), Type); }
		Diff->SetStringField(TEXT("valueA"), ValA);
		Diff->SetStringField(TEXT("valueB"), ValB);
		R.Diffs.Add(MakeShared<FJsonValueObject>(Diff));
	};

	if (Arguments->HasField(TEXT("propertyPaths")))
	{
		const TArray<TSharedPtr<FJsonValue>>& PathsArr = Arguments->GetArrayField(TEXT("propertyPaths"));
		for (const TSharedPtr<FJsonValue>& PathVal : PathsArr)
		{
			if (R.Diffs.Num() >= MaxDiffs) { R.bTruncated = true; break; }

			FString Path = PathVal->AsString();
			TSharedPtr<FJsonObject> ResA = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> ResB = MakeShared<FJsonObject>();
			FString ErrorA, ErrorB;

			bool bOkA = FNexusRuntimeUtils::ResolveActorPropertyPath(ActorA, Path, ResA, ErrorA);
			bool bOkB = FNexusRuntimeUtils::ResolveActorPropertyPath(ActorB, Path, ResB, ErrorB);

			FString ValueA, ValueB;
			if (bOkA)
			{
				ValueA = ResA->HasField(TEXT("value"))
					? ResA->GetStringField(TEXT("value"))
					: FNexusPropertyUtils::SerializeJson(ResA);
			}
			else { ValueA = FString::Printf(TEXT("[error] %s"), *ErrorA); }
			if (bOkB)
			{
				ValueB = ResB->HasField(TEXT("value"))
					? ResB->GetStringField(TEXT("value"))
					: FNexusPropertyUtils::SerializeJson(ResB);
			}
			else { ValueB = FString::Printf(TEXT("[error] %s"), *ErrorB); }

			if (ValueA != ValueB || !bOkA || !bOkB)
			{
				FString Type;
				if (bOkA && ResA->HasField(TEXT("type"))) { Type = ResA->GetStringField(TEXT("type")); }
				AddDiff(Path, Type, ValueA, ValueB);
			}
		}
		return R;
	}

	for (TFieldIterator<FProperty> It(ActorA->GetClass()); It; ++It)
	{
		if (R.Diffs.Num() >= MaxDiffs) { R.bTruncated = true; break; }
		if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;

		FString PropName = It->GetName();
		FProperty* PropB = ActorB->GetClass()->FindPropertyByName(*PropName);
		if (!PropB) continue;

		const void* PtrA = It->ContainerPtrToValuePtr<void>(ActorA);
		const void* PtrB = PropB->ContainerPtrToValuePtr<void>(ActorB);
		FString VA, VB;
		FNexusPropertyUtils::ExportText(*It, VA, PtrA);
		FNexusPropertyUtils::ExportText(PropB, VB, PtrB);

		if (VA != VB) { AddDiff(PropName, It->GetCPPType(), VA, VB); }
	}

	TArray<UActorComponent*> CompsA, CompsB;
	ActorA->GetComponents(CompsA);
	ActorB->GetComponents(CompsB);

	TMap<FString, UActorComponent*> CompMapB;
	for (UActorComponent* C : CompsB) { if (C) CompMapB.Add(C->GetName(), C); }

	for (UActorComponent* CompA : CompsA)
	{
		if (!CompA) continue;
		if (R.Diffs.Num() >= MaxDiffs) { R.bTruncated = true; break; }
		FString CompName = CompA->GetName();
		UActorComponent** CompBPtr = CompMapB.Find(CompName);
		if (!CompBPtr)
		{
			AddDiff(CompName, TEXT("component"), TEXT("exists"), TEXT("missing"));
			continue;
		}
		CompMapB.Remove(CompName);

		UActorComponent* CompB = *CompBPtr;
		for (TFieldIterator<FProperty> It(CompA->GetClass()); It; ++It)
		{
			if (R.Diffs.Num() >= MaxDiffs) { R.bTruncated = true; break; }
			if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;

			FString PN = It->GetName();
			FProperty* PB = CompB->GetClass()->FindPropertyByName(*PN);
			if (!PB) continue;

			const void* PA = It->ContainerPtrToValuePtr<void>(CompA);
			const void* PBV = PB->ContainerPtrToValuePtr<void>(CompB);
			FString VA, VB;
			FNexusPropertyUtils::ExportText(*It, VA, PA);
			FNexusPropertyUtils::ExportText(PB, VB, PBV);

			if (VA != VB) { AddDiff(CompName + TEXT(".") + PN, It->GetCPPType(), VA, VB); }
		}
	}

	for (const auto& Pair : CompMapB)
	{
		if (R.Diffs.Num() >= MaxDiffs) { R.bTruncated = true; break; }
		AddDiff(Pair.Key, TEXT("component"), TEXT("missing"), TEXT("exists"));
	}

	return R;
}

static TSharedPtr<FJsonObject> MakeActorInfoCap(AActor* Actor)
{
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("name"), Actor->GetName());
	Info->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	return Info;
}

void FDiffRuntimeActorsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("diff_runtime_actors");
	Out.Description = TEXT("Diff two runtime Actor properties. Max 50; filter by propertyPaths.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("actorNameA"),    FNexusSchema::Str(TEXT("First Actor name")))
		.Prop(TEXT("actorNameB"),    FNexusSchema::Str(TEXT("Second Actor name")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("Dot-separated paths; omit=all editable properties")))
		.Required({ TEXT("actorNameA"), TEXT("actorNameB") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("compare"), TEXT("difference"), TEXT("contrast"), TEXT("equal"), TEXT("mismatch") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_property"), TEXT("list_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FDiffRuntimeActorsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{


		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString NameA, NameB;
		if (!Arguments->TryGetStringField(TEXT("actorNameA"), NameA) ||
		    !Arguments->TryGetStringField(TEXT("actorNameB"), NameB))
		{
			OutError = TEXT("actorNameA and actorNameB required");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		AActor* ActorA = FNexusRuntimeUtils::FindActorByName(World, NameA);
		if (!ActorA)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Actor specified by actorNameA not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		AActor* ActorB = FNexusRuntimeUtils::FindActorByName(World, NameB);
		if (!ActorB)
		{
			Entry->SetStringField(TEXT("error"), TEXT("Actor specified by actorNameB not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetObjectField(TEXT("actorA"), MakeActorInfoCap(ActorA));
		Entry->SetObjectField(TEXT("actorB"), MakeActorInfoCap(ActorB));

		FComparePairResultCap Pair = ComparePairCap(ActorA, ActorB, Arguments.ToSharedRef());
	Entry->SetArrayField(TEXT("diffs"), Pair.Diffs);
	if (Pair.bTruncated) { Entry->SetBoolField(TEXT("truncated"), true); }
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FDiffRuntimeActorsCapability)
