// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusListRuntimeActorsCapability.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FListRuntimeActorsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("list_runtime_actors");
	Out.Description = TEXT("枚举 PIE/Game 中 Actor。按类/标签/名过滤；返回引用非属性。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("classFilter"), FNexusSchema::Str(TEXT("Actor 类名子串匹配（可选）")))
		.Prop(TEXT("nameFilter"),  FNexusSchema::Str(TEXT("Actor 名或标签子串匹配（可选）")))
		.Prop(TEXT("tagFilter"),   FNexusSchema::Str(TEXT("仅含此标签的 Actor（可选）")))
		.Prop(TEXT("offset"),      FNexusSchema::Int(TEXT("分页偏移（默认 0）")))
		.Prop(TEXT("limit"),       FNexusSchema::Int(TEXT("最大条数 1~500（默认 100）")))
		.Prop(TEXT("detail"),      FNexusSchema::Enum(TEXT("响应详细度：minimal/standard/full"),
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

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString ClassFilter, NameFilter, TagFilter;
		FString DetailMode = TEXT("standard");
		int32 Offset = 0, Limit = 100;

		if (Arguments.IsValid())
		{
			if (Arguments->HasField(TEXT("classFilter"))) ClassFilter = Arguments->GetStringField(TEXT("classFilter"));
			if (Arguments->HasField(TEXT("nameFilter")))  NameFilter  = Arguments->GetStringField(TEXT("nameFilter"));
			if (Arguments->HasField(TEXT("tagFilter")))   TagFilter   = Arguments->GetStringField(TEXT("tagFilter"));
			if (Arguments->HasField(TEXT("offset")))      Offset = FMath::Max(0, static_cast<int32>(Arguments->GetNumberField(TEXT("offset"))));
			if (Arguments->HasField(TEXT("limit")))       Limit  = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("limit"))), 1, 500);
			if (Arguments->HasField(TEXT("detail")))      DetailMode = Arguments->GetStringField(TEXT("detail")).ToLower();
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
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FListRuntimeActorsCapability)

