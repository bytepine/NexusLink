// Copyright byteyang. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** 读取 manage_* Schema 中 operations.items.action.enum。 */
inline bool NexusSchemaCollectActions(const FString& CapName, TArray<FString>& OutActions, FAutomationTestBase& Test)
{
	OutActions.Reset();
	const FCapRecord* Rec = FNexusCapabilityRegistry::Get().FindRecordByName(CapName);
	if (!Rec)
	{
		Test.AddError(FString::Printf(TEXT("未注册: %s"), *CapName));
		return false;
	}
	if (!Rec->Def.InputSchema.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Props = nullptr;
	if (!Rec->Def.InputSchema->TryGetObjectField(TEXT("properties"), Props) || !Props)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Ops = nullptr;
	if (!(*Props)->TryGetObjectField(TEXT("operations"), Ops) || !Ops)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Items = nullptr;
	if (!(*Ops)->TryGetObjectField(TEXT("items"), Items) || !Items)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ItemProps = nullptr;
	if (!(*Items)->TryGetObjectField(TEXT("properties"), ItemProps) || !ItemProps)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ActionObj = nullptr;
	if (!(*ItemProps)->TryGetObjectField(TEXT("action"), ActionObj) || !ActionObj)
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Enums = nullptr;
	if (!(*ActionObj)->TryGetArrayField(TEXT("enum"), Enums) || !Enums)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : *Enums)
	{
		OutActions.Add(V->AsString());
	}
	return true;
}

inline bool NexusSchemaActionContains(const FString& CapName, const FString& Action, FAutomationTestBase& Test)
{
	TArray<FString> Actions;
	if (!NexusSchemaCollectActions(CapName, Actions, Test))
	{
		return false;
	}
	return Actions.Contains(Action);
}
