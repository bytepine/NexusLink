// Copyright byteyang. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Utils/NexusStringMatchUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusLinkStringMatchTest,
	"NexusLink.Utils.StringMatch.Matches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLinkStringMatchTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("empty pattern matches anything"),
		FNexusStringMatchUtils::Matches(TEXT("AnyTextHere"), FString()));
	TestTrue(TEXT("empty pattern matches empty"),
		FNexusStringMatchUtils::Matches(FString(), FString()));

	TestTrue(TEXT("substring lowercase"),
		FNexusStringMatchUtils::Matches(TEXT("BP_TestActor"), TEXT("test")));
	TestTrue(TEXT("substring mixed case"),
		FNexusStringMatchUtils::Matches(TEXT("BP_TestActor"), TEXT("TEST")));
	TestFalse(TEXT("substring miss"),
		FNexusStringMatchUtils::Matches(TEXT("BP_TestActor"), TEXT("xyz")));

	TestTrue(TEXT("^ prefix hit"),
		FNexusStringMatchUtils::Matches(TEXT("BP_Foo"), TEXT("^BP_")));
	TestFalse(TEXT("^ prefix miss"),
		FNexusStringMatchUtils::Matches(TEXT("WBP_Foo"), TEXT("^BP_")));

	TestTrue(TEXT("$ suffix hit"),
		FNexusStringMatchUtils::Matches(TEXT("MyActor"), TEXT("Actor$")));
	TestFalse(TEXT("$ suffix miss"),
		FNexusStringMatchUtils::Matches(TEXT("MyActorX"), TEXT("Actor$")));

	TestTrue(TEXT("regex hit"),
		FNexusStringMatchUtils::Matches(TEXT("BP_HeroActor"), TEXT("/^BP_.+Actor$/")));
	TestFalse(TEXT("regex miss"),
		FNexusStringMatchUtils::Matches(TEXT("Hero"), TEXT("/^BP_.+Actor$/")));

	return true;
}

