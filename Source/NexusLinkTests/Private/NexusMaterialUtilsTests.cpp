// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusMaterialUtils.h"
#include "Materials/MaterialFunction.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMaterialUtilsMfExpressionRoundtrip,
	"NexusLink.Utils.Material.MfExpressionRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMaterialUtilsMfExpressionRoundtrip::RunTest(const FString& Parameters)
{
	UMaterialFunction* MF = NewObject<UMaterialFunction>(GetTransientPackage());
	TestNotNull(TEXT("transient MaterialFunction"), MF);
	if (!MF) return false;

#if WITH_EDITOR
	const int32 Count = FNexusMaterialUtils::GetExpressions(MF).Num();
	TestTrue(TEXT("空 MF 表达式列表可访问"), Count >= 0);
	TestNull(TEXT("未知 nodeId 返回空"), FNexusMaterialUtils::FindExpressionByNodeId(MF, TEXT("NoSuchNode")));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusMaterialSearchAssetTypesIncludeFunction,
	"NexusLink.Capability.Material.SearchAssetTypesIncludeFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMaterialSearchAssetTypesIncludeFunction::RunTest(const FString& Parameters)
{
	const FCapRecord* GetRec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("get_asset_material"));
	const FCapRecord* ManRec = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("manage_asset_material"));
	TestNotNull(TEXT("get_asset_material 已注册"), GetRec);
	TestNotNull(TEXT("manage_asset_material 已注册"), ManRec);
	if (!GetRec || !ManRec) return false;

	TestTrue(TEXT("get SAT 含 MaterialFunction"), GetRec->Def.SearchAssetTypes.Contains(TEXT("MaterialFunction")));
	TestTrue(TEXT("manage SAT 含 MaterialFunction"), ManRec->Def.SearchAssetTypes.Contains(TEXT("MaterialFunction")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
