// Copyright byteyang. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusJsonUtils.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNexusCapabilitySchemaDump,
	"NexusLink.Smoke.PluginAndRegistry.SchemaDump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCapabilitySchemaDump::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("NexusLink plugin not found"));
		return false;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	for (const FCapRecord& Rec : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		if (Rec.Def.InputSchema.IsValid())
		{
			Root->SetObjectField(Rec.Def.Name, Rec.Def.InputSchema);
		}
	}

	const FString OutPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("scripts/generated/capability_schemas.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	const FString Json = FNexusJsonUtils::SerializeCondensed(Root);
	const bool bOk = FFileHelper::SaveStringToFile(Json, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	TestTrue(TEXT("wrote capability_schemas.json"), bOk && FPaths::FileExists(OutPath));
	TestTrue(TEXT("schema dump not empty"), Root->Values.Num() > 0);
	return true;
}

#endif
