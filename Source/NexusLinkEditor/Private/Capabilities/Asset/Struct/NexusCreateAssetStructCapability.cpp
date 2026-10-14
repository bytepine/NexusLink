// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusCreateAssetStructCapability.h"

#if WITH_EDITOR

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#include "Kismet2/StructureEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetStructCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetStruct");
	Out.Description = TEXT("Create UDS file, auto-compile; add fields via ManageAssetStructField.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("New struct package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = { TEXT("uds"), TEXT("Fields"), TEXT("Schema"), TEXT("record"), TEXT("new") };
	Out.RelatedCapabilities = { TEXT("ManageAssetStructField"), TEXT("GetAssetStruct") };
	Out.WhenToUse = TEXT("Create empty UDS; add fields via manage");
}

FCapabilityResult FNexusCreateAssetStructCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);

		if (LoadObject<UUserDefinedStruct>(nullptr, *AssetPath))
		{ Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Struct already exists: %s"), *AssetPath)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { Entry->SetStringField(TEXT("Error"), TEXT("Failed to create package")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(
			Package, FName(*AssetName), RF_Public | RF_Standalone);
		if (!NewStruct) { Entry->SetStringField(TEXT("Error"), TEXT("Struct Createfailed")); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		FNexusAssetUtils::NotifyAndSaveCreated(Package, NewStruct, AssetPath);

		FNexusCapabilityResultBuilder::AddCreatedEntry(OutEntries, NewStruct);
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetStructCapability)

#endif // WITH_EDITOR
