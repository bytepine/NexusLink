// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCapabilityIndexUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusCapability.h"
#include "NexusMcpTool.h"
#include "Utils/NexusDangerousCapGate.h"
#include "Utils/NexusHostUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

ENexusCapabilityLookupStatus FNexusCapabilityIndexUtils::ResolveByName(
	const FString& Name, const UNexusLinkSettings* Settings, const FCapRecord*& OutRecord)
{
	OutRecord = Settings ? FNexusCapabilityRegistry::Get().FindRecordByName(Name) : nullptr;
	if (!OutRecord)
	{
		return ENexusCapabilityLookupStatus::NotFound;
	}
	if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*OutRecord))
	{
		return ENexusCapabilityLookupStatus::Unavailable;
	}
	if (!Settings->IsCapabilityEnabled(OutRecord->Def.Name))
	{
		return ENexusCapabilityLookupStatus::Disabled;
	}
	return ENexusCapabilityLookupStatus::Enabled;
}

static void CollectRequiredNames(const TSharedPtr<FJsonObject>& ObjectSchema, TSet<FString>& OutRequired)
{
	OutRequired.Reset();
	if (!ObjectSchema.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* ReqArr = nullptr;
	if (ObjectSchema->TryGetArrayField(TEXT("required"), ReqArr) && ReqArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ReqArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				OutRequired.Add(S);
			}
		}
	}
}

static void AppendEnumField(const TSharedPtr<FJsonObject>& PropDef, TSharedPtr<FJsonObject>& Param)
{
	const TArray<TSharedPtr<FJsonValue>>* EnumArr = nullptr;
	if (!PropDef.IsValid() || !PropDef->TryGetArrayField(TEXT("enum"), EnumArr) || !EnumArr)
	{
		return;
	}
	Param->SetStringField(TEXT("type"), TEXT("string (enum)"));
	Param->SetArrayField(TEXT("enum"), *EnumArr);
}

// 数组参数的 enum 挂在 items.enum 下（见 FNexusSchema::EnumArr，如 sections[]）；
// 不能复用 AppendEnumField——那会把 type 硬改成 "string (enum)"，掩盖数组类型
static void AppendArrayItemEnumField(const TSharedPtr<FJsonObject>& PropDef, TSharedPtr<FJsonObject>& Param)
{
	const TSharedPtr<FJsonObject>* ItemsObj = nullptr;
	if (!PropDef.IsValid() || !PropDef->TryGetObjectField(TEXT("items"), ItemsObj) || !ItemsObj || !ItemsObj->IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* EnumArr = nullptr;
	if (!(*ItemsObj)->TryGetArrayField(TEXT("enum"), EnumArr) || !EnumArr)
	{
		return;
	}
	Param->SetArrayField(TEXT("enum"), *EnumArr);
}

static void AppendParamFromProp(const FString& QualifiedName, const TSharedPtr<FJsonObject>& PropDef,
	                              bool bRequired, TArray<TSharedPtr<FJsonValue>>& OutParams)
{
	if (!PropDef.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
	Param->SetStringField(TEXT("name"), QualifiedName);
	if (bRequired)
	{
		Param->SetBoolField(TEXT("required"), true);
	}

	FString Type;
	if (PropDef->TryGetStringField(TEXT("type"), Type))
	{
		Param->SetStringField(TEXT("type"), Type);
	}

	FString Desc;
	if (PropDef->TryGetStringField(TEXT("description"), Desc))
	{
		Param->SetStringField(TEXT("description"), Desc);
	}

	for (const TCHAR* Bound : {TEXT("minimum"), TEXT("maximum")})
	{
		double Value = 0;
		if (PropDef->TryGetNumberField(Bound, Value))
		{
			Param->SetNumberField(Bound, Value);
		}
	}

	if (Type == TEXT("array"))
	{
		AppendArrayItemEnumField(PropDef, Param);
	}
	else
	{
		AppendEnumField(PropDef, Param);
	}
	OutParams.Add(MakeShared<FJsonValueObject>(Param));
}

static void ExtractPropertiesRecursive(const TSharedPtr<FJsonObject>& ObjectSchema, const FString& Prefix,
	                                     TArray<TSharedPtr<FJsonValue>>& OutParams)
{
	if (!ObjectSchema.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (!ObjectSchema->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj)
	{
		return;
	}

	TSet<FString> RequiredSet;
	CollectRequiredNames(ObjectSchema, RequiredSet);

	for (const auto& KV : (*PropsObj)->Values)
	{
		const FString Key = FString(*KV.Key);
		const FString QualifiedName = Prefix.IsEmpty() ? Key : Prefix + Key;

		const TSharedPtr<FJsonObject>* PropDef = nullptr;
		if (!KV.Value.IsValid() || !KV.Value->TryGetObject(PropDef) || !PropDef)
		{
			continue;
		}

		FString Type;
		(*PropDef)->TryGetStringField(TEXT("type"), Type);

		if (Type == TEXT("array"))
		{
			const TSharedPtr<FJsonObject>* ItemsObj = nullptr;
			if ((*PropDef)->TryGetObjectField(TEXT("items"), ItemsObj) && ItemsObj && ItemsObj->IsValid())
			{
				FString ItemsType;
				if ((*ItemsObj)->TryGetStringField(TEXT("type"), ItemsType) && ItemsType == TEXT("object"))
				{
					const FString ItemPrefix = QualifiedName + TEXT("[].");
					ExtractPropertiesRecursive(*ItemsObj, ItemPrefix, OutParams);
					continue;
				}
			}
			AppendParamFromProp(QualifiedName, *PropDef, RequiredSet.Contains(Key), OutParams);
		}
		else
		{
			AppendParamFromProp(QualifiedName, *PropDef, RequiredSet.Contains(Key), OutParams);
		}
	}
}

TArray<TSharedPtr<FJsonValue>> FNexusCapabilityIndexUtils::ExtractParameters(
	const TSharedPtr<FJsonObject>& InputSchema)
{
	TArray<TSharedPtr<FJsonValue>> Params;
	if (!InputSchema.IsValid())
	{
		return Params;
	}

	ExtractPropertiesRecursive(InputSchema, FString(), Params);
	return Params;
}

static TSharedPtr<FJsonObject> FindSchemaObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Name)
{
	const TSharedPtr<FJsonObject>* Child = nullptr;
	return Parent && Parent->TryGetObjectField(Name, Child) ? *Child : nullptr;
}

void FNexusCapabilityIndexUtils::DescribeAction(const FNexusCapabilityDefinition& Def, const FString& RequestedAction,
                                                TSharedPtr<FJsonObject>& Output)
{
	const auto RootProperties = FindSchemaObject(Def.InputSchema, TEXT("properties"));
	const auto Operations = FindSchemaObject(RootProperties, TEXT("Operations"));
	const auto Items = FindSchemaObject(Operations, TEXT("items"));
	const bool bNested = !FindSchemaObject(RootProperties, TEXT("Action")) && Items.IsValid();
	const auto Owner = bNested ? Items : Def.InputSchema;
	const auto Properties = FindSchemaObject(Owner, TEXT("properties"));
	const auto Selector = FindSchemaObject(Properties, TEXT("Action"));
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Selector || !Selector->TryGetArrayField(TEXT("enum"), Choices) || Choices->Num() == 0)
	{
		Output->SetStringField(TEXT("ErrorKind"), TEXT("ActionNotSupported"));
		Output->SetStringField(TEXT("Error"), TEXT("This capability has no enumerated Action. Omit Action to inspect its parameters."));
		return;
	}

	const auto* Match = Choices->FindByPredicate([&RequestedAction](const TSharedPtr<FJsonValue>& Choice)
	{
		return Choice->Type == EJson::String && Choice->AsString().Equals(RequestedAction, ESearchCase::CaseSensitive);
	});
	if (!Match)
	{
		Output->SetStringField(TEXT("ErrorKind"), TEXT("UnknownAction"));
		Output->SetStringField(TEXT("Error"), TEXT("Use one of the action names in AvailableActions."));
		Output->SetArrayField(TEXT("AvailableActions"), *Choices);
		return;
	}

	const FString Action = (*Match)->AsString();
	const FNexusActionFields* Fields = Def.ActionFields.Find(Action);
	const auto IsMissing = [&Properties](const FString& Name) { return !Properties->HasField(Name); };
	if (Fields && (Fields->Required.ContainsByPredicate(IsMissing) || Fields->Optional.ContainsByPredicate(IsMissing)))
		Fields = nullptr;
	TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetStringField(TEXT("name"), Def.Name);
	Detail->SetStringField(TEXT("description"), Def.Description);
	Detail->SetStringField(TEXT("Action"), Action);
	Detail->SetStringField(TEXT("ParameterScope"), Fields ? TEXT("ActionSpecific") : TEXT("Shared"));
	AttachMetaHints(Detail, Def, false);
	if (Fields && !Fields->Hint.IsEmpty()) Detail->SetStringField(TEXT("ActionHint"), Fields->Hint);

	// 仅复制要改写的层级。注册表仍供完整发现、校验及其他动作共用。
	auto SelectedAction = MakeShared<FJsonObject>(*Selector);
	SelectedAction->SetArrayField(TEXT("enum"), {*Match});
	FString DefaultAction;
	if (SelectedAction->TryGetStringField(TEXT("default"), DefaultAction) && !DefaultAction.Equals(Action, ESearchCase::CaseSensitive))
		SelectedAction->RemoveField(TEXT("default"));
	TSet<FString> Required;
	CollectRequiredNames(Owner, Required);
	Required.Add(TEXT("Action"));
	if (Fields)
	{
		for (const FString& Name : Fields->Required) Required.Add(Name);
	}
	auto SelectedProperties = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	for (const auto& Pair : Properties->Values)
	{
		const FString Name(*Pair.Key);
		if (Required.Contains(Name)) RequiredArray.Add(MakeShared<FJsonValueString>(Name));
		else if (Fields && !Fields->Optional.Contains(Name))
		{
			// 保留框架注入的顶层保存、编译与危险操作确认参数。
			const bool bInjected = !bNested && ((Def.bInjectSaveToDisk && Name == TEXT("bSaveToDisk"))
				|| (Def.bInjectCompile && Name == TEXT("bCompile"))
				|| (Def.HasTag(FNexusMcpTags::Dangerous) && Name == TEXT("Reason")));
			if (!bInjected) continue;
		}
		SelectedProperties->SetField(Name, Pair.Value);
	}
	SelectedProperties->SetObjectField(TEXT("Action"), SelectedAction);
	auto Schema = MakeShared<FJsonObject>(*Owner);
	Schema->SetObjectField(TEXT("properties"), SelectedProperties);
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	if (bNested)
	{
		auto SelectedOperations = MakeShared<FJsonObject>(*Operations);
		SelectedOperations->SetObjectField(TEXT("items"), Schema);
		auto SelectedRootProperties = MakeShared<FJsonObject>(*RootProperties);
		SelectedRootProperties->SetObjectField(TEXT("Operations"), SelectedOperations);
		Schema = MakeShared<FJsonObject>(*Def.InputSchema);
		Schema->SetObjectField(TEXT("properties"), SelectedRootProperties);
	}
	Detail->SetObjectField(TEXT("inputSchema"), Schema);
	if (Def.DescribeAction) Def.DescribeAction(Def.Name, Action, *Detail);
	if (Detail->GetStringField(TEXT("ParameterScope")) == TEXT("Shared"))
		Detail->SetStringField(TEXT("ActionHint"), TEXT("Includes shared fields. Some optional fields apply to other actions."));
	Output->SetObjectField(TEXT("Capability"), Detail);
}

int32 FNexusCapabilityIndexUtils::ScoreToken(const FString& Token, const TArray<FString>& Keywords)
{
	int32 Best = 0;
	for (const FString& KW : Keywords)
	{
		if (KW.Equals(Token))
		{
			return 10;
		}
		if (Token.Len() >= 2 && KW.StartsWith(Token))
		{
			Best = FMath::Max(Best, 5);
		}
		else if (Token.Len() >= 3 && KW.Contains(Token))
		{
			Best = FMath::Max(Best, 2);
		}
	}
	return Best;
}

int32 FNexusCapabilityIndexUtils::ScoreCapability(const TArray<FString>& Tokens, const TArray<FString>& Keywords)
{
	if (Tokens.Num() == 0) return 1;

	int32 Total = 0;
	int32 Matched = 0;
	for (const FString& Tok : Tokens)
	{
		const int32 S = ScoreToken(Tok, Keywords);
		if (S > 0)
		{
			++Matched;
			Total += S;
		}
	}
	if (Matched < Tokens.Num()) return 0;
	Total += 10;
	return Total;
}

int32 FNexusCapabilityIndexUtils::ScoreCapabilityPartial(const TArray<FString>& Tokens,
                                                          const TArray<FString>& Keywords,
                                                          int32& OutMatchedTokens)
{
	OutMatchedTokens = 0;
	if (Tokens.Num() == 0) return 0;

	int32 Total = 0;
	for (const FString& Tok : Tokens)
	{
		const int32 S = ScoreToken(Tok, Keywords);
		if (S > 0)
		{
			++OutMatchedTokens;
			Total += S;
		}
	}
	if (OutMatchedTokens == 0) return 0;
	Total += OutMatchedTokens * 5;
	return Total;
}

TArray<FString> FNexusCapabilityIndexUtils::FilterVisibleRelated(
	const TArray<FString>& Related,
	const UNexusLinkSettings* Settings,
	int32 MaxCount)
{
	TArray<FString> Out;
	for (const FString& Name : Related)
	{
		if (Name.IsEmpty())
		{
			continue;
		}
		const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(Name);
		if (!Rec)
		{
			continue;
		}
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(*Rec))
		{
			continue;
		}
		if (Settings && !Settings->IsCapabilityEnabled(Rec->Def.Name))
		{
			continue;
		}
		Out.Add(Rec->Def.Name);
		if (MaxCount > 0 && Out.Num() >= MaxCount)
		{
			break;
		}
	}
	return Out;
}

void FNexusCapabilityIndexUtils::AttachMetaHints(TSharedPtr<FJsonObject>& Entry,
                                                  const FNexusCapabilityDefinition& Def, bool bIncludeOutputSchema)
{
    if (bIncludeOutputSchema && Def.OutputSchema.IsValid()) Entry->SetObjectField(TEXT("outputSchema"), Def.OutputSchema);
    if (Def.ExecutionInfo) Entry->SetObjectField(TEXT("Execution"), Def.ExecutionInfo());

	const TArray<FString> Related = FilterVisibleRelated(Def.RelatedCapabilities, UNexusLinkSettings::Get());
	if (Related.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& R : Related)
			Arr.Add(MakeShared<FJsonValueString>(R));
		Entry->SetArrayField(TEXT("RelatedCapabilities"), Arr);
	}
	if (Def.Prerequisites.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& P : Def.Prerequisites)
			Arr.Add(MakeShared<FJsonValueString>(P));
		Entry->SetArrayField(TEXT("Prerequisites"), Arr);
	}
	if (!Def.WhenToUse.IsEmpty())
	{
		Entry->SetStringField(TEXT("WhenToUse"), Def.WhenToUse);
	}
	if (Def.HasTag(FNexusMcpTags::Dangerous)
		&& FNexusDangerousCapGate::NeedsConfirm(Def.Name))
	{
		Entry->SetBoolField(TEXT("bConfirmationRequired"), true);
		Entry->SetStringField(TEXT("Hint"),
			TEXT("Dangerous capability: pass reason (purpose, expected effect, why no safer cap). The editor will prompt the user to allow or deny THIS call."));
	}
}

TSharedPtr<FJsonObject> FNexusCapabilityIndexUtils::BuildDirectory(const UNexusLinkSettings* Settings)
{
	static const TArray<FString> GroupOrder = {
		TEXT("blueprint"), TEXT("material"), TEXT("widget"), TEXT("data"),
		TEXT("struct"), TEXT("runtime"), TEXT("editor"), TEXT("write"), TEXT("readonly"),
	};

	TMap<FString, TArray<TSharedPtr<FJsonValue>>> Groups;
	TArray<TSharedPtr<FJsonValue>> Ungrouped;

	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (!Settings->IsCapabilityEnabled(Record.Def.Name)) continue;
		if (!FNexusHostUtils::IsCapabilityVisibleOnHost(Record)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Record.Def.Name);
		// 目录模式只返 name（+可选 WhenToUse），详情靠 CapabilityName 取 Schema
		if (!Record.Def.WhenToUse.IsEmpty())
		{
			Entry->SetStringField(TEXT("WhenToUse"), Record.Def.WhenToUse);
		}

		FString GroupTag;
		for (const FString& Tag : Record.Def.Tags)
		{
			FString TLow = Tag.ToLower();
			if (TLow != TEXT("readonly") && TLow != TEXT("write") && TLow != TEXT("editor"))
			{
				GroupTag = TLow;
				break;
			}
		}
		if (GroupTag.IsEmpty())
		{
			for (const FString& Tag : Record.Def.Tags)
			{
				GroupTag = Tag.ToLower();
				break;
			}
		}

		if (!GroupTag.IsEmpty())
			Groups.FindOrAdd(GroupTag).Add(MakeShared<FJsonValueObject>(Entry));
		else
			Ungrouped.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Dir = MakeShared<FJsonObject>();
	for (const FString& G : GroupOrder)
	{
		if (Groups.Contains(G))
			Dir->SetArrayField(G, Groups[G]);
	}
	for (auto& KV : Groups)
	{
		if (!GroupOrder.Contains(KV.Key))
			Dir->SetArrayField(KV.Key, KV.Value);
	}
	if (Ungrouped.Num() > 0)
		Dir->SetArrayField(TEXT("Other"), Ungrouped);
	return Dir;
}
