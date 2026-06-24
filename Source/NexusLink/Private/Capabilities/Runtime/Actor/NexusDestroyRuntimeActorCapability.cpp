// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusDestroyRuntimeActorCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "NexusMcpTool.h"

void FDestroyRuntimeActorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("destroy_runtime_actor");
	Out.Description = TEXT("从 PIE/Game 移除运行时 Actor，标记 Level 已修改。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("要销毁的 Actor 名或标签")))
		.Required({ TEXT("actorName") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("kill"), TEXT("despawn"), TEXT("remove"), TEXT("drop"), TEXT("delete") };
	Out.RelatedCapabilities = { TEXT("spawn_runtime_actor"), TEXT("list_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FDestroyRuntimeActorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		FString ActorName;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("actorName"), ActorName) || ActorName.IsEmpty())
		{
			OutError = TEXT("缺少 actorName");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actorName"), ActorName);

		AActor* Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
		if (!IsValid(Actor))
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 未找到: %s"), *ActorName));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		const FString Name  = Actor->GetName();
		const FString Class = Actor->GetClass()->GetName();

	#if WITH_EDITOR
		const bool bDestroyed = Actor->Destroy();
		if (bDestroyed && World->GetCurrentLevel())
			World->GetCurrentLevel()->MarkPackageDirty();
	#else
		const bool bDestroyed = Actor->Destroy();
	#endif

		if (!bDestroyed)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("销毁失败: %s"), *Name));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),  Name);
		Entry->SetStringField(TEXT("class"), Class);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FDestroyRuntimeActorCapability)
