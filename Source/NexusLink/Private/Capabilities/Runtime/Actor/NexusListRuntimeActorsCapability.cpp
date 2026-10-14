// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusListRuntimeActorsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusResponseCompactorUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FNexusListRuntimeActorsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ListRuntimeActors");
	Out.Description = TEXT("List Actors in PIE/Game. Filter by class/tag/name; returns refs not properties.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("ClassFilter"), FNexusSchema::Str(TEXT("Actor class name substring match (optional)")))
		.Prop(TEXT("NameFilter"),  FNexusSchema::Str(TEXT("Actor name or tag substring match (optional)")))
		.Prop(TEXT("TagFilter"),   FNexusSchema::Str(TEXT("Only Actors with this tag (optional)")))
		.Prop(TEXT("Offset"),      FNexusSchema::Int(TEXT("Pagination offset (default 0)")))
		.Prop(TEXT("Limit"),       FNexusSchema::Int(TEXT("Max count 1-500 (default 100)")))
		.Prop(TEXT("Detail"),      FNexusSchema::Enum(TEXT("Response verbosity: minimal/standard/full"),
			{ TEXT("Minimal"), TEXT("Standard"), TEXT("Full") }, TEXT("Standard")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("scene"), TEXT("World"), TEXT("filter"), TEXT("find"), TEXT("Tag") };
	Out.RelatedCapabilities = { TEXT("GetRuntimeActorProperty"), TEXT("DiffRuntimeActors") };
	Out.Prerequisites = { TEXT("PIE") };
}

FCapabilityResult FNexusListRuntimeActorsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString ClassFilter, NameFilter, TagFilter;
		FString DetailMode = TEXT("Standard");
		int32 Offset = 0, Limit = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("ClassFilter"))) ClassFilter = A.Str(TEXT("ClassFilter"));
			if (Arguments->HasField(TEXT("NameFilter")))  NameFilter  = A.Str(TEXT("NameFilter"));
			if (Arguments->HasField(TEXT("TagFilter")))   TagFilter   = A.Str(TEXT("TagFilter"));
			if (Arguments->HasField(TEXT("Offset")))      Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("Offset"))));
			if (Arguments->HasField(TEXT("Limit")))       Limit  = FMath::Clamp(static_cast<int32>(A.Num(TEXT("Limit"))), 1, 500);
			if (Arguments->HasField(TEXT("Detail")))      DetailMode = A.Str(TEXT("Detail")).ToLower();
		}

		struct FActorEntry { FString Name; FString Label; FString Class; FString Location; };
		TArray<FActorEntry> All;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;

			const FString ClassName  = Actor->GetClass()->GetName();
			const FString ActorName  = Actor->GetName();
			const FString ActorLabel = FNexusRuntimeUtils::GetActorLabelOrName(Actor);

			if (!ClassFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(ClassName, ClassFilter)) continue;
			if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(ActorName, NameFilter) && !FNexusStringMatchUtils::Matches(ActorLabel, NameFilter)) continue;
			if (!TagFilter.IsEmpty())
			{
				bool bHasTag = false;
				for (const FName& Tag : Actor->Tags)
				{
					if (Tag.ToString() == TagFilter) { bHasTag = true; break; }
				}
				if (!bHasTag) continue;
			}

			FActorEntry E;
			E.Name  = ActorName;
			E.Label = ActorLabel;
			E.Class = ClassName;
			const FVector Loc = Actor->GetActorLocation();
			E.Location = FString::Printf(TEXT("%.1f, %.1f, %.1f"), Loc.X, Loc.Y, Loc.Z);
			All.Add(E);
		}

		const int32 Total = All.Num();
		int32 Start, End; FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, Start, End);

		const bool bMinimal = (DetailMode == TEXT("Minimal"));
		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), All[i].Name);
			if (!bMinimal && !All[i].Label.IsEmpty()) O->SetStringField(TEXT("Label"), All[i].Label);
			O->SetStringField(TEXT("Class"), All[i].Class);
			if (!bMinimal) O->SetStringField(TEXT("Location"), All[i].Location);
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("World"), World->GetName());
	Entry->SetNumberField(TEXT("TotalCount"), Total);
	Entry->SetNumberField(TEXT("Offset"), Start);
	Entry->SetNumberField(TEXT("Limit"), Limit);
	Entry->SetArrayField(TEXT("Actors"), Page);
		if (!ClassFilter.IsEmpty() && Page.Num() > 0)
		{
			FNexusResponseCompactorUtils ActorCompactor;
			ActorCompactor.AddForcedDefaultIfUnanimous(TEXT("Class"), Page);
			ActorCompactor.CompactArray(Page);
			ActorCompactor.Emit(Entry, TEXT("Actors"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FNexusListRuntimeActorsCapability)

