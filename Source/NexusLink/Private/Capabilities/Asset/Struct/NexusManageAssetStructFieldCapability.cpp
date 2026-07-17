// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusManageAssetStructFieldCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
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
	Out.Description = TEXT("批量编辑 UDS 字段：add/remove/modify；修改后自动编译。");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("字段操作"), { TEXT("add"), TEXT("remove"), TEXT("set") }))
		.Prop(TEXT("fieldName"),    FNexusSchema::Str(TEXT("字段显示名")))
		.Prop(TEXT("fieldType"),    FNexusSchema::Str(TEXT("字段类型（add）")))
		.Prop(TEXT("defaultValue"), FNexusSchema::Str(TEXT("默认值（add/set）")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("新显示名（set）")))
		.Prop(TEXT("newType"),      FNexusSchema::Str(TEXT("新字段类型（set）")))
		.Required({ TEXT("action"), TEXT("fieldName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("UserDefinedStruct 资产路径（共用）")))
		.Prop(TEXT("fields"),    FNexusSchema::ArrayOf(TEXT("批量字段操作"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("fields") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("field"), TEXT("member"), TEXT("schema"), TEXT("type")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_struct"), TEXT("create_asset_struct"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("写操作：增删/修改 UDS 字段");
}

FCapabilityResult FManageAssetStructFieldCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
#if WITH_EDITOR
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		if (!Arguments.IsValid())
		{
			OutError = TEXT("参数无效");
			return;
		}

		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UUserDefinedStruct* Struct = FNexusAssetUtils::LoadAssetWithFallback<UUserDefinedStruct>(AssetPath);
		if (!Struct) { OutError = FString::Printf(TEXT("UserDefinedStruct 未找到: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* FieldsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("fields"), FieldsArr) || !FieldsArr)
		{
			OutError = TEXT("缺少 fields");
			return;
		}
		if (FieldsArr->Num() == 0)
		{
			OutError = TEXT("fields 不能为空");
			return;
		}

		bool bDidMutate = false;
		for (const TSharedPtr<FJsonValue>& Val : *FieldsArr)
		{
			TSharedPtr<FJsonObject> Item = Val->AsObject();
			TSharedPtr<FJsonObject> OutEntry = MakeShared<FJsonObject>();

			if (!Item.IsValid())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("无效的 field 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(OutEntry));
				continue;
			}

			const FString Action    = Item->HasField(TEXT("action"))    ? Item->GetStringField(TEXT("action")).ToLower() : TEXT("");
			const FString FieldName = Item->HasField(TEXT("fieldName")) ? Item->GetStringField(TEXT("fieldName"))        : TEXT("");
			OutEntry->SetStringField(TEXT("action"),    Action);
			OutEntry->SetStringField(TEXT("fieldName"), FieldName);

			if (Action.IsEmpty() || FieldName.IsEmpty())
			{
				OutEntry->SetStringField(TEXT("error"), TEXT("action 与 fieldName 必填"));
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
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("字段未找到: %s"), *FieldName));
				else if (!FStructureEditorUtils::RemoveVariable(Struct, Target))
					OutEntry->SetStringField(TEXT("error"), TEXT("删除字段失败"));
				else
					bItemMutated = true;
			}
			else if (Action == TEXT("add"))
			{
				if (!Item->HasField(TEXT("fieldType")))
				{
					OutEntry->SetStringField(TEXT("error"), TEXT("action=add 时 fieldType 必填"));
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
						OutEntry->SetStringField(TEXT("error"), TEXT("添加字段失败"));
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
					OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("字段未找到: %s"), *FieldName));
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
				OutEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
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
		OutError = TEXT("manage_asset_struct_field 仅在编辑器构建可用");
	});
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetStructFieldCapability)
