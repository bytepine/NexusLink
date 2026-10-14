// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Localization/NexusGetAssetStringTableCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "NexusMcpTool.h"

void FNexusGetAssetStringTableCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetStringTable");
	Out.SearchAssetTypes = {TEXT("StringTable")};
	Out.Description = TEXT("Read StringTable: namespace/keys/source string summary.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("StringTable asset path")))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Max entries to return"), 50))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("localization"), TEXT("loc"), TEXT("Key"), TEXT("i18n") };
	Out.RelatedCapabilities = { TEXT("ManageAssetStringTable"), TEXT("CreateAssetStringTable") };
}

FCapabilityResult FNexusGetAssetStringTableCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("AssetPath"), AssetPath, OutEntries, {})) return;

		UStringTable* Table = FNexusAssetUtils::LoadAssetWithFallback<UStringTable>(AssetPath);
		if (!Table)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("Path"), AssetPath}},
				FString::Printf(TEXT("Failed to load StringTable: %s"), *AssetPath));
			return;
		}

		int32 Limit = 50;
		if (Arguments.IsValid() && Arguments->HasField(TEXT("Limit")))
		{
			Limit = FMath::Max(1, static_cast<int32>(A.Num(TEXT("Limit"))));
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Table->GetName());
		Entry->SetStringField(TEXT("Path"), Table->GetPathName());
		Entry->SetStringField(TEXT("Namespace"), Table->GetMutableStringTable()->GetNamespace());

		TArray<TSharedPtr<FJsonValue>> KeysArr;
		int32 Total = 0;
		Table->GetMutableStringTable()->EnumerateSourceStrings(
			[&](const FString& Key, const FString& Source) -> bool
			{
				if (Total < Limit)
				{
					TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
					Row->SetStringField(TEXT("Key"), Key);
					Row->SetStringField(TEXT("Source"), Source);
					KeysArr.Add(MakeShared<FJsonValueObject>(Row));
				}
				++Total;
				return true;
			});
		Entry->SetNumberField(TEXT("KeyCount"), Total);
		Entry->SetArrayField(TEXT("Keys"), KeysArr);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetStringTableCapability)
