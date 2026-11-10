// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR && __has_include("Materials/MaterialExpressionNamedReroute.h")
#include "NexusCapabilityRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Utils/NexusMaterialUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMaterialNamedRerouteReachabilityTest, "NexusLink.Independent.Material.DeleteUnusedNamedReroute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusMaterialNamedRerouteReachabilityTest::RunTest(const FString&)
{
    const FCapRecord* Capability = FNexusCapabilityRegistry::Get().FindRecordByName(TEXT("ManageAssetMaterial"));
    if (!TestNotNull(TEXT("Material capability is registered"), Capability)) return false;
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
    auto* Source = Cast<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass()));
    auto* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionNamedRerouteDeclaration::StaticClass()));
    auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionNamedRerouteUsage::StaticClass()));
    auto* Unused = Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass()));
    if (!TestNotNull(TEXT("Source created"), Source) || !TestNotNull(TEXT("Declaration created"), Declaration) || !TestNotNull(TEXT("Usage created"), Usage) || !TestNotNull(TEXT("Unused node created"), Unused)) return false;
    Declaration->Input.Expression = Source;
    Usage->Declaration = Declaration;
    Usage->DeclarationGuid = Declaration->VariableGuid;
    if (!TestTrue(TEXT("Named usage feeds the material output"), UMaterialEditingLibrary::ConnectMaterialProperty(Usage, TEXT(""), MP_BaseColor))) return false;
    auto Args = MakeShared<FJsonObject>(), Operation = MakeShared<FJsonObject>();
    Args->SetStringField(TEXT("AssetPath"), Material->GetPathName()); Operation->SetStringField(TEXT("Action"), TEXT("DeleteUnused"));
    Args->SetArrayField(TEXT("Operations"), {MakeShared<FJsonValueObject>(Operation)}); Args->SetBoolField(TEXT("bSaveToDisk"), false);
    const FCapabilityResult Result = Capability->Instance->Run(Args);
    if (!TestFalse(TEXT("Actual material edit entry succeeds"), Result.HasErrors())) return false;
    const auto& Remaining = FNexusMaterialUtils::GetExpressions(Material);
    TestTrue(TEXT("Connected named usage survives"), Remaining.Contains(Usage));
    TestTrue(TEXT("Its non-pin declaration link survives"), Remaining.Contains(Declaration));
    TestTrue(TEXT("Declaration input survives"), Remaining.Contains(Source));
    TestFalse(TEXT("An unrelated unused node is deleted"), Remaining.Contains(Unused));
    TestEqual(TEXT("Exactly the live chain remains"), Remaining.Num(), 3);
    return true;
}
#endif
