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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkAssetUtilsRecommendCapsTest,
	"NexusLink.Utils.AssetUtils.ResolveRecommendedCapabilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkAssetUtilsRecommendCapsTest::RunTest(const FString& Parameters)
{
	// 依赖各 get/manage cap 静态注册时写入的 SearchAssetTypes 索引
	FString GetCap, ManageCap;

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("Blueprint"), GetCap, ManageCap);
	TestEqual(TEXT("Blueprint get"), GetCap, FString(TEXT("get_asset_blueprint")));
	TestEqual(TEXT("Blueprint manage"), ManageCap, FString(TEXT("manage_asset_blueprint")));

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("Widget"), GetCap, ManageCap);
	TestEqual(TEXT("Widget get"), GetCap, FString(TEXT("get_asset_user_widget")));
	TestEqual(TEXT("Widget manage"), ManageCap, FString(TEXT("manage_asset_user_widget")));

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("World"), GetCap, ManageCap);
	TestEqual(TEXT("World get"), GetCap, FString(TEXT("get_asset_level")));
	TestEqual(TEXT("World manage"), ManageCap, FString(TEXT("manage_asset_level")));

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("Struct"), GetCap, ManageCap);
	TestEqual(TEXT("Struct get"), GetCap, FString(TEXT("get_asset_struct")));
	TestEqual(TEXT("Struct manage"), ManageCap, FString(TEXT("manage_asset_struct_field")));

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("MaterialInstance"), GetCap, ManageCap);
	TestEqual(TEXT("MaterialInstance get"), GetCap, FString(TEXT("get_asset_material")));

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT(""), GetCap, ManageCap);
	TestTrue(TEXT("empty type clears get"), GetCap.IsEmpty());
	TestTrue(TEXT("empty type clears manage"), ManageCap.IsEmpty());

	FNexusAssetUtils::ResolveRecommendedCapabilities(TEXT("SomeUnknownUClass"), GetCap, ManageCap);
	TestTrue(TEXT("unknown type clears get"), GetCap.IsEmpty());
	TestTrue(TEXT("unknown type clears manage"), ManageCap.IsEmpty());

	return true;
}
