// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Utils/NexusAssetUtils.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkAssetUtilsTest,
	"NexusLink.Utils.AssetUtils.FindClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkAssetUtilsTest::RunTest(const FString& Parameters)
{
	// 裸名 + U 前缀补全
	{
		UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(
			TEXT("StaticMeshComponent"));
		TestNotNull(TEXT("bare name StaticMeshComponent"), Found);
		TestEqual(TEXT("resolves to UStaticMeshComponent"),
			Found, UStaticMeshComponent::StaticClass());
	}

	// 完整名
	{
		UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(
			TEXT("Actor"));
		TestNotNull(TEXT("bare name Actor"), Found);
		TestEqual(TEXT("resolves to AActor"),
			Found, AActor::StaticClass());
	}

	// 明显不存在
	{
		UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(
			TEXT("__DefinitelyNotARealClass_XYZ__"));
		TestNull(TEXT("garbage class returns nullptr"), Found);
	}

	// 空串 — 不应崩溃
	{
		UClass* Found = FNexusAssetUtils::FindClassWithUPrefix(FString());
		TestNull(TEXT("empty name returns nullptr"), Found);
	}

	// LoadAssetWithFallback 对空路径不崩溃
	{
		UObject* Loaded = FNexusAssetUtils::LoadAssetWithFallback<UObject>(FString());
		TestNull(TEXT("empty path returns nullptr"), Loaded);
	}

	// LoadAssetWithFallback 对不存在资产 graceful
	{
		UObject* Loaded = FNexusAssetUtils::LoadAssetWithFallback<UObject>(
			TEXT("/Game/__nonexistent_asset_path__"));
		TestNull(TEXT("nonexistent path returns nullptr"), Loaded);
	}

	return true;
}
