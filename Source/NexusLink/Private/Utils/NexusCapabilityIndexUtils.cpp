// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusCapabilityIndexUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusCapability.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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

		AppendEnumField(PropDef, Param);
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

void FNexusCapabilityIndexUtils::AttachMetaHints(TSharedPtr<FJsonObject>& Entry,
                                                  const FNexusCapabilityDefinition& Def)
{
	if (Def.RelatedCapabilities.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& R : Def.RelatedCapabilities)
			Arr.Add(MakeShared<FJsonValueString>(R));
		Entry->SetArrayField(TEXT("relatedCapabilities"), Arr);
	}
	if (Def.Prerequisites.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& P : Def.Prerequisites)
			Arr.Add(MakeShared<FJsonValueString>(P));
		Entry->SetArrayField(TEXT("prerequisites"), Arr);
	}
	if (!Def.WhenToUse.IsEmpty())
	{
		Entry->SetStringField(TEXT("whenToUse"), Def.WhenToUse);
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

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Record.Def.Name);
		// 目录模式只返 name（+可选 whenToUse），详情靠 capabilityName 取 Schema
		if (!Record.Def.WhenToUse.IsEmpty())
		{
			Entry->SetStringField(TEXT("whenToUse"), Record.Def.WhenToUse);
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
		Dir->SetArrayField(TEXT("other"), Ungrouped);
	return Dir;
}
