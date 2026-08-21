// Copyright byteyang. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"

/** 读取 manage_* Schema 中 operations.items.action.enum。 */
inline bool NexusSchemaCollectActions(const FString& CapName, TArray<FString>& OutActions, FAutomationTestBase& Test)
{
	if (!FNexusCapabilityRegistry::CollectOperationActions(CapName, OutActions))
	{
		if (!FNexusCapabilityRegistry::Get().FindRecordByName(CapName))
		{
			Test.AddError(FString::Printf(TEXT("未注册: %s"), *CapName));
		}
		return false;
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
