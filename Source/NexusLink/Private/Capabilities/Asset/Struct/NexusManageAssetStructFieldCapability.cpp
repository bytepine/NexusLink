// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusManageAssetStructFieldCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPinTypeUtils.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#if WITH_EDITOR
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#endif
#include "NexusMcpTool.h"

void FManageAssetStructFieldCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_struct_field");
	Out.SearchAssetTypes = {TEXT("Struct")};
	Out.Description = TEXT("Batch edit UDS fields: add/remove/modify; auto-compile after changes.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Field operation"), { TEXT("add"), TEXT("remove"), TEXT("set") }))
		.Prop(TEXT("fieldName"),    FNexusSchema::Str(TEXT("Field display name")))
		.Prop(TEXT("fieldType"),    FNexusSchema::Str(TEXT("Field type (add)")))
		.Prop(TEXT("defaultValue"), FNexusSchema::Str(TEXT("Default value (add/set)")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("New display name (set)")))
		.Prop(TEXT("newType"),      FNexusSchema::Str(TEXT("New field type (set)")))
		.Required({ TEXT("action"), TEXT("fieldName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("UserDefinedStruct asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch field ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("field"), TEXT("member"), TEXT("schema"), TEXT("type")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_struct"), TEXT("create_asset_struct"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/modify UDS fields");
}

FCapabilityResult FManageAssetStructFieldCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);


		const FString AssetPath = A.Str(TEXT("assetPath"));

		UUserDefinedStruct* Struct = FNexusAssetUtils::LoadAssetWithFallback<UUserDefinedStruct>(AssetPath);
		if (!Struct) { OutError = FString::Printf(TEXT("UserDefinedStruct not found: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("Missing or empty operations");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : Ops)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("Invalid operation item"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action    = Item->HasField(TEXT("action"))    ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString FieldName = Item->HasField(TEXT("fieldName")) ? Item->GetStringField(TEXT("fieldName"))        : TEXT("");
			OutEntry->SetStringField(TEXT("action"),    Action);
			OutEntry->SetStringField(TEXT("fieldName"), FieldName);

			if (Action.IsEmpty() || FieldName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("action and fieldName is required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			auto FindGuid = [Struct](const FString& Name) -> FGuid
			{
				for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
				{ if (Var.FriendlyName == Name) return Var.VarGuid; }
				return FGuid();
			};

			bool bItemMutated = false;
			if (Action == TEXT("remove"))
			{
				const FGuid Target = FindGuid(FieldName);
				if (!Target.IsValid())
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
				else if (!FStructureEditorUtils::RemoveVariable(Struct, Target))
					OutEntry->SetStringField(TEXT("error"), TEXT("Failed to delete field"));
				else
					bItemMutated = true;
			}
			else if (Action == TEXT("add"))
			{
				if (!Item->HasField(TEXT("fieldType")))
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("fieldType is required when action=add"));
				}
				else
				{
					FEdGraphPinType PinType; FString TypeErr;
					if (!FNexusPinTypeUtils::ParsePinType(Item->GetStringField(TEXT("fieldType")), PinType, TypeErr))
					{
						OutEntry->SetStringField(TEXT("error"), TypeErr);
					}
					else if (!FStructureEditorUtils::AddVariable(Struct, PinType))
					{
						OutEntry->SetStringField(TEXT("error"), TEXT("Failed to add field"));
					}
					else
					{
						const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
						if (VarDescs.Num() > 0)
						{
							const FGuid NewGuid = VarDescs.Last().VarGuid;
							FStructureEditorUtils::RenameVariable(Struct, NewGuid, FieldName);
							if (Item->HasField(TEXT("defaultValue")))
								FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewGuid, Item->GetStringField(TEXT("defaultValue")));
						}
						OutEntry->SetStringField(TEXT("fieldType"), Item->GetStringField(TEXT("fieldType")));
						bItemMutated = true;
					}
				}
			}
			else if (Action == TEXT("set"))
			{
				const FGuid Target = FindGuid(FieldName);
				if (!Target.IsValid())
				{
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
				}
				else
				{
					bool bChanged = false;
					if (Item->HasField(TEXT("newType")))
					{
						FEdGraphPinType PinType; FString TypeErr;
						if (!FNexusPinTypeUtils::ParsePinType(Item->GetStringField(TEXT("newType")), PinType, TypeErr))
							OutEntry->SetStringField(TEXT("error"), TypeErr);
						else { FStructureEditorUtils::ChangeVariableType(Struct, Target, PinType); bChanged = true; }
					}
					if (Item->HasField(TEXT("defaultValue")))
					{
						FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Target, Item->GetStringField(TEXT("defaultValue")));
						bChanged = true;
					}
					if (Item->HasField(TEXT("newName")))
					{
						FStructureEditorUtils::RenameVariable(Struct, Target, Item->GetStringField(TEXT("newName")));
						bChanged = true;
					}
					if (!bChanged)
						OutEntry->SetStringField(TEXT("error"), TEXT("Provide at least one of newName / newType / defaultValue"));
					else
						bItemMutated = true;
				}
			}
			else
			{
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}

			// 仅在该条真正修改过结构体时才 compile；避免错误路径误改脏
			if (bItemMutated)
			{
				FStructureEditorUtils::CompileStructure(Struct);
				bDidMutate = true;
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
		}

		if (bDidMutate) Struct->MarkPackageDirty();
	
	});
#else
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		OutError = TEXT("manage_asset_struct_field only available in editor builds");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetStructFieldCapability)
