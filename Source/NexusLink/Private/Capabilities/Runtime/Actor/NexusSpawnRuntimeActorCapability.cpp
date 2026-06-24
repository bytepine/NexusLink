// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Runtime/Actor/NexusSpawnRuntimeActorCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusRuntimeUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "NexusMcpTool.h"

void FSpawnRuntimeActorCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("spawn_runtime_actor");
	Out.Description = TEXT("在 PIE 实例化 Actor。blueprintPath 或 className；可设位置/旋转。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("blueprintPath"), FNexusSchema::Str(TEXT("蓝图路径（与 className 二选一）")))
		.Prop(TEXT("className"),     FNexusSchema::Str(TEXT("原生类名（与 blueprintPath 二选一）")))
		.Prop(TEXT("locationX"),     FNexusSchema::Num(TEXT("生成 X"), 0.0))
		.Prop(TEXT("locationY"),     FNexusSchema::Num(TEXT("生成 Y"), 0.0))
		.Prop(TEXT("locationZ"),     FNexusSchema::Num(TEXT("生成 Z"), 0.0))
		.Prop(TEXT("rotationPitch"), FNexusSchema::Num(TEXT("俯仰角（度）"), 0.0))
		.Prop(TEXT("rotationYaw"),   FNexusSchema::Num(TEXT("偏航角（度）"),   0.0))
		.Prop(TEXT("rotationRoll"),  FNexusSchema::Num(TEXT("翻滚角（度）"),  0.0))
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Runtime };
	Out.ExtraSearchKeywords = { TEXT("instantiate"), TEXT("place"), TEXT("create"), TEXT("level"), TEXT("world") };
	Out.RelatedCapabilities = { TEXT("destroy_runtime_actor"), TEXT("list_runtime_actors") };
	Out.Prerequisites = { TEXT("pie") };
}

FCapabilityResult FSpawnRuntimeActorCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid()) { OutError = TEXT("缺少参数"); return; }

		UWorld* World = FNexusRuntimeUtils::RequirePlayWorld(OutError);
		if (!World) return;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		FVector Location(
			Arguments->HasField(TEXT("locationX")) ? Arguments->GetNumberField(TEXT("locationX")) : 0.0,
			Arguments->HasField(TEXT("locationY")) ? Arguments->GetNumberField(TEXT("locationY")) : 0.0,
			Arguments->HasField(TEXT("locationZ")) ? Arguments->GetNumberField(TEXT("locationZ")) : 0.0
		);
		FRotator Rotation(
			Arguments->HasField(TEXT("rotationPitch")) ? Arguments->GetNumberField(TEXT("rotationPitch")) : 0.0,
			Arguments->HasField(TEXT("rotationYaw"))   ? Arguments->GetNumberField(TEXT("rotationYaw"))   : 0.0,
			Arguments->HasField(TEXT("rotationRoll"))  ? Arguments->GetNumberField(TEXT("rotationRoll"))  : 0.0
		);

		UClass* SpawnClass = nullptr;
		if (Arguments->HasField(TEXT("blueprintPath")))
		{
			const FString BpPath = Arguments->GetStringField(TEXT("blueprintPath"));
			Entry->SetStringField(TEXT("blueprintPath"), BpPath);
			UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(BpPath);
			if (!BP || !BP->GeneratedClass)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint 未找到或未编译: %s"), *BpPath));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
			SpawnClass = BP->GeneratedClass;
		}
		else if (Arguments->HasField(TEXT("className")))
		{
			const FString ClassName = Arguments->GetStringField(TEXT("className"));
			Entry->SetStringField(TEXT("className"), ClassName);
			SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
			if (!SpawnClass)
			{
				const FString Prefixed = TEXT("A") + ClassName;
				SpawnClass = FNexusAssetUtils::FindClassWithUPrefix(Prefixed);
			}
			if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 类未找到: %s"), *ClassName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				return;
			}
		}
		else
		{
			OutError = TEXT("需要 blueprintPath 或 className");
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* NewActor = World->SpawnActor<AActor>(SpawnClass, Location, Rotation, Params);
		if (!NewActor)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor 生成失败（类: %s）"), *SpawnClass->GetName()));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"),     NewActor->GetName());
		Entry->SetStringField(TEXT("class"),    NewActor->GetClass()->GetName());
		Entry->SetStringField(TEXT("location"), FString::Printf(TEXT("%.1f, %.1f, %.1f"), Location.X, Location.Y, Location.Z));
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FSpawnRuntimeActorCapability)
