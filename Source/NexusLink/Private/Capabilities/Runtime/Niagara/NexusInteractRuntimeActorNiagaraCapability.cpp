// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Niagara/NexusInteractRuntimeActorNiagaraCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/Actor.h"
#include "NexusMcpTool.h"

void FInteractRuntimeActorNiagaraCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("interact_runtime_actor_niagara");
	Out.Description = TEXT("运行时激活/关闭 Niagara。action=activate|deactivate。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("activate"), TEXT("deactivate") }))
		.Prop(TEXT("actorName"), FNexusSchema::Str(TEXT("Actor 名")))
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("可选：NiagaraSystem 路径（activate 时生成）")))
		.Required({ TEXT("action") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system") };
	Out.Prerequisites = { TEXT("pie") };
	Out.WhenToUse = TEXT("PIE 中开关 Niagara 组件或按资产路径生成");
}

FCapabilityResult FInteractRuntimeActorNiagaraCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Action, ActorName, AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("action"), Action) || Action.IsEmpty())
		{
			OutError = TEXT("缺少 action");
			return;
		}
		Arguments->TryGetStringField(TEXT("actorName"), ActorName);
		Arguments->TryGetStringField(TEXT("assetPath"), AssetPath);
		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("action"), Action);

		AActor* Actor = nullptr;
		if (!ActorName.IsEmpty())
		{
			Actor = FNexusRuntimeUtils::FindActorByName(World, ActorName);
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 未找到: %s"), *ActorName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}

		if (Action.Equals(TEXT("activate"), ESearchCase::IgnoreCase))
		{
			if (!AssetPath.IsEmpty())
			{
				UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
				if (!Sys)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraSystem 未找到: %s"), *AssetPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					return;
				}
				UNiagaraComponent* Comp = Actor
					? UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, Actor->GetRootComponent(), NAME_None,
						FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true)
					: UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sys, FVector::ZeroVector);
				if (!Comp)
				{
					Entry->SetStringField(TEXT("error"), TEXT("SpawnSystem 失败"));
				}
				else
				{
					Entry->SetBoolField(TEXT("activated"), true);
				}
			}
			else if (Actor)
			{
				TArray<UNiagaraComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UNiagaraComponent* C : Comps)
				{
					if (C) C->Activate(true);
				}
				Entry->SetNumberField(TEXT("componentCount"), Comps.Num());
				Entry->SetBoolField(TEXT("activated"), true);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), TEXT("activate 需要 actorName 或 assetPath"));
			}
		}
		else if (Action.Equals(TEXT("deactivate"), ESearchCase::IgnoreCase))
		{
			if (!Actor)
			{
				Entry->SetStringField(TEXT("error"), TEXT("deactivate 需要 actorName"));
			}
			else
			{
				TArray<UNiagaraComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UNiagaraComponent* C : Comps)
				{
					if (C) C->Deactivate();
				}
				Entry->SetBoolField(TEXT("deactivated"), true);
			}
		}
		else
		{
			Entry->SetStringField(TEXT("error"), TEXT("仅支持 activate/deactivate"));
		}
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FInteractRuntimeActorNiagaraCapability)

#endif
