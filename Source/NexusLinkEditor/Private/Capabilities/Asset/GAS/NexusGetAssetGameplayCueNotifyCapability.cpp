// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GAS/NexusGetAssetGameplayCueNotifyCapability.h"

#if WITH_GAS

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayCueNotify_Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "NexusMcpTool.h"

void FNexusGetAssetGameplayCueNotifyCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetGameplayCueNotify");
	Out.SearchAssetTypes = {TEXT("GameplayCueNotify_Static")};
	Out.Description = TEXT("Read GameplayCueNotify: CueName/class name/is Blueprint.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Cue Notify asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Gas };
	Out.ExtraSearchKeywords = { TEXT("cue"), TEXT("notify"), TEXT("gc") };
	Out.RelatedCapabilities = {
		TEXT("ManageAssetGameplayCueNotify"), TEXT("CreateAssetGameplayCueNotify"),
		TEXT("ManageAssetBlueprint")
	};
}

FCapabilityResult FNexusGetAssetGameplayCueNotifyCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UObject* Loaded = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
		if (!Loaded)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Load failed: %s"), *AssetPath));
			return;
		}

		FName CueName;
		FString ClassName;
		bool bIsBlueprint = false;

		if (UGameplayCueNotify_Static* StaticNotify = Cast<UGameplayCueNotify_Static>(Loaded))
		{
			CueName = StaticNotify->GameplayCueName;
			ClassName = StaticNotify->GetClass()->GetName();
		}
		else if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
		{
			bIsBlueprint = true;
			ClassName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("Blueprint");
			UClass* Gen = BP->GeneratedClass;
			if (Gen)
			{
				if (UGameplayCueNotify_Static* CDO = Cast<UGameplayCueNotify_Static>(Gen->GetDefaultObject()))
				{
					CueName = CDO->GameplayCueName;
				}
				else if (AGameplayCueNotify_Actor* ActorCDO = Cast<AGameplayCueNotify_Actor>(Gen->GetDefaultObject()))
				{
					CueName = ActorCDO->GameplayCueName;
				}
			}
		}
		else
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}}, TEXT("Not a GameplayCueNotify"));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Loaded->GetName());
		Entry->SetStringField(TEXT("Path"), Loaded->GetPathName());
		Entry->SetStringField(TEXT("ClassName"), ClassName);
		Entry->SetBoolField(TEXT("bIsBlueprint"), bIsBlueprint);
		Entry->SetStringField(TEXT("CueName"), CueName.ToString());
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetGameplayCueNotifyCapability)

#endif
