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

void FListRuntimeActorsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("list_runtime_actors");
	Out.Description = TEXT("List Actors in PIE/Game. Filter by class/tag/name; returns refs not properties.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("classFilter"), FNexusSchema::Str(TEXT("Actor class name substring match (optional)")))
		.Prop(TEXT("nameFilter"),  FNexusSchema::Str(TEXT("Actor name or tag substring match (optional)")))
		.Prop(TEXT("tagFilter"),   FNexusSchema::Str(TEXT("Only Actors with this tag (optional)")))
		.Prop(TEXT("offset"),      FNexusSchema::Int(TEXT("Pagination offset (default 0)")))
		.Prop(TEXT("limit"),       FNexusSchema::Int(TEXT("Max count 1-500 (default 100)")))
		.Prop(TEXT("detail"),      FNexusSchema::Enum(TEXT("Response verbosity: minimal/standard/full"),
			{ TEXT("minimal"), TEXT("standard"), TEXT("full") }, TEXT("standard")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("scene"), TEXT("world"), TEXT("filter"), TEXT("find"), TEXT("tag") };
	Out.RelatedCapabilities = { TEXT("get_runtime_actor_property"), TEXT("diff_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FListRuntimeActorsCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString ClassFilter, NameFilter, TagFilter;
		FString DetailMode = TEXT("standard");
		int32 Offset = 0, Limit = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("classFilter"))) ClassFilter = A.Str(TEXT("classFilter"));
			if (Arguments->HasField(TEXT("nameFilter")))  NameFilter  = A.Str(TEXT("nameFilter"));
			if (Arguments->HasField(TEXT("tagFilter")))   TagFilter   = A.Str(TEXT("tagFilter"));
			if (Arguments->HasField(TEXT("offset")))      Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))       Limit  = FMath::Clamp(static_cast<int32>(A.Num(TEXT("limit"))), 1, 500);
			if (Arguments->HasField(TEXT("detail")))      DetailMode = A.Str(TEXT("detail")).ToLower();
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

		const bool bMinimal = (DetailMode == TEXT("minimal"));
		TArray<TSharedPtr<FJsonValue>> Page;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), All[i].Name);
			if (!bMinimal && !All[i].Label.IsEmpty()) O->SetStringField(TEXT("label"), All[i].Label);
			O->SetStringField(TEXT("class"), All[i].Class);
			if (!bMinimal) O->SetStringField(TEXT("location"), All[i].Location);
			Page.Add(MakeShared<FJsonValueObject>(O));
		}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("world"), World->GetName());
	Entry->SetNumberField(TEXT("totalCount"), Total);
	Entry->SetNumberField(TEXT("offset"), Start);
	Entry->SetNumberField(TEXT("limit"), Limit);
	Entry->SetArrayField(TEXT("actors"), Page);
		if (!ClassFilter.IsEmpty() && Page.Num() > 0)
		{
			FNexusResponseCompactorUtils ActorCompactor;
			ActorCompactor.AddForcedDefaultIfUnanimous(TEXT("class"), Page);
			ActorCompactor.CompactArray(Page);
			ActorCompactor.Emit(Entry, TEXT("actors"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FListRuntimeActorsCapability)

