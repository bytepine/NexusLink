// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Enum/NexusGetAssetEnumCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/UserDefinedEnum.h"
#include "NexusMcpTool.h"

void FNexusGetAssetEnumCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetEnum");
	Out.SearchAssetTypes = {TEXT("UserDefinedEnum")};
	Out.Description = TEXT("Read UserDefinedEnum entries (name/DisplayName/value).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Enum asset package path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("enum"), TEXT("Entries"), TEXT("blueprint"), TEXT("Read") };
	Out.RelatedCapabilities = { TEXT("CreateAssetEnum"), TEXT("ManageAssetEnum") };
}

FCapabilityResult FNexusGetAssetEnumCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
		if (!Enum)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("Failed to load UserDefinedEnum: %s"), *AssetPath));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Enum->GetName());
		Entry->SetStringField(TEXT("Path"), Enum->GetPathName());

		TArray<TSharedPtr<FJsonValue>> EntriesArr;
		// NumEnums() 包含内部 _MAX 项，跳过最后一项
		const int32 Count = Enum->NumEnums() - 1;
		for (int32 i = 0; i < Count; ++i)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("Index"),       i);
			Item->SetStringField(TEXT("name"),        Enum->GetNameStringByIndex(i));
			Item->SetStringField(TEXT("DisplayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
			Item->SetNumberField(TEXT("Value"),       (double)Enum->GetValueByIndex(i));
			EntriesArr.Add(MakeShared<FJsonValueObject>(Item));
		}

		Entry->SetNumberField(TEXT("EntryCount"), Count);
		Entry->SetArrayField(TEXT("Entries"), EntriesArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetEnumCapability)
