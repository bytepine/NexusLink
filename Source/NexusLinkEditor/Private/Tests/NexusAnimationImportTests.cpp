// Copyright byteyang. All Rights Reserved.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Capabilities/Asset/NexusImportAssetCapability.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "FbxImporter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 自建两个有效 take，不依赖项目动画或渲染器导出路径。
static bool WriteAnimationImportFixture(const FString& Filename, bool bMultipleMeshes = false)
{
	FbxManager* Manager = FbxManager::Create();
	if (!Manager) return false;
	ON_SCOPE_EXIT { Manager->Destroy(); };
	Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
	FbxScene* Scene = FbxScene::Create(Manager, "NexusMultiTake");
	FbxSkeleton* Bone = FbxSkeleton::Create(Scene, "RootBone");
	Bone->SetSkeletonType(FbxSkeleton::eRoot);
	FbxNode* Root = FbxNode::Create(Scene, "RootBone");
	Root->SetNodeAttribute(Bone); Scene->GetRootNode()->AddChild(Root);
	if (bMultipleMeshes)
	{
		for (int32 MeshIndex = 0; MeshIndex < 2; ++MeshIndex)
		{
			const FString Name = FString::Printf(TEXT("Part%d"), MeshIndex);
			FbxNode* MeshNode = FbxNode::Create(Scene, TCHAR_TO_UTF8(*Name));
			MeshNode->SetNodeAttribute(FbxMesh::Create(Scene, TCHAR_TO_UTF8(*Name)));
			Root->AddChild(MeshNode);
		}
	}
	FbxTime Start, End; Start.SetSecondDouble(0); End.SetSecondDouble(1);
	FbxTimeSpan Span(Start, End);
	for (const char* Take : {"Walk", "Run"})
	{
		FbxAnimStack* Stack = FbxAnimStack::Create(Scene, Take); Stack->SetLocalTimeSpan(Span);
		FbxAnimLayer* Layer = FbxAnimLayer::Create(Scene, "Base"); Stack->AddMember(Layer);
		Root->LclRotation.GetCurveNode(Layer, true);
		Root->LclScaling.GetCurveNode(Layer, true);
		FbxAnimCurve* Curve = Root->LclTranslation.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X, true);
		Curve->KeyModifyBegin();
		Curve->KeySetValue(Curve->KeyAdd(Start), 0);
		Curve->KeySetValue(Curve->KeyAdd(End), 100);
		Curve->KeyModifyEnd();
	}
	FbxExporter* Exporter = FbxExporter::Create(Manager, "NexusMultiTake");
	return Exporter->Initialize(TCHAR_TO_UTF8(*Filename), -1, Manager->GetIOSettings()) && Exporter->Export(Scene);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMultiTakeImportTest, "NexusLink.Import.MultiTakeConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusMultiTakeImportTest::RunTest(const FString&)
{
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Folder = TEXT("/Game/_NexusTest/Import_") + Id;
	const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / (TEXT("NexusMultiTake_") + Id + TEXT(".fbx")));
	if (!TestTrue(TEXT("Two-take FBX exported"), WriteAnimationImportFixture(Filename))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Filename); };
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>(CreatePackage(*(Folder / TEXT("Skeleton"))), TEXT("Skeleton"), RF_Public | RF_Standalone));
	{
		FReferenceSkeletonModifier Modifier(const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton()), Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("RootBone"), TEXT("RootBone"), INDEX_NONE), FTransform::Identity);
	}
	UPackage* ProtectedPackage = CreatePackage(*(Folder / TEXT("Clips_Walk")));
	TStrongObjectPtr<UAnimSequence> Protected(NewObject<UAnimSequence>(ProtectedPackage, TEXT("Clips_Walk"), RF_Public | RF_Standalone));
	Protected->SetSkeleton(Skeleton.Get()); ProtectedPackage->SetDirtyFlag(false);
	const float BeforeLength = Protected->GetPlayLength();
	auto Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("SourceFile"), Filename);
	Arguments->SetStringField(TEXT("DestinationPath"), Folder);
	Arguments->SetStringField(TEXT("AssetName"), TEXT("Clips"));
	Arguments->SetStringField(TEXT("AssetType"), TEXT("AnimSequence"));
	Arguments->SetStringField(TEXT("SkeletonPath"), Skeleton->GetPathName());
	FNexusImportAssetCapability Capability;
	const FCapabilityResult Failed = Capability.Run(Arguments);
	TestTrue(TEXT("Fail rejects an existing suffixed take before import"), Failed.HasErrors());
	TestEqual(TEXT("Fail leaves existing animation unchanged"), Protected->GetPlayLength(), BeforeLength);
	TestFalse(TEXT("Fail never dirties existing animation"), ProtectedPackage->IsDirty());
	Arguments->SetStringField(TEXT("ConflictPolicy"), TEXT("Unique"));
	const FCapabilityResult Unique = Capability.Run(Arguments);
	TestFalse(TEXT("Unique imports the entire take group"), Unique.HasErrors());
	TestEqual(TEXT("Both unique takes are reported"), Unique.Entries.Num(), 2);
	for (const auto& Entry : Unique.Entries)
		TestTrue(TEXT("Every take uses the conflict-free base"), Entry->AsObject()->GetStringField(TEXT("Path")).StartsWith(Folder / TEXT("Clips_1_")));
	TestEqual(TEXT("Unique leaves existing animation unchanged"), Protected->GetPlayLength(), BeforeLength);
	TestFalse(TEXT("Unique never dirties existing animation"), ProtectedPackage->IsDirty());
	Arguments->SetStringField(TEXT("ConflictPolicy"), TEXT("Replace"));
	const FCapabilityResult Replacement = Capability.Run(Arguments);
	TestFalse(TEXT("Explicit replacement succeeds"), Replacement.HasErrors());
	TestEqual(TEXT("Existing and new takes are both reported"), Replacement.Entries.Num(), 2);
	TestTrue(TEXT("Explicit replacement updates existing animation"), Protected->GetPlayLength() > BeforeLength);
	const FCapabilityResult Repeated = Capability.Run(Arguments);
	TestFalse(TEXT("Replacement of already dirty takes succeeds"), Repeated.HasErrors());
	TestEqual(TEXT("All replaced takes are reported even when already dirty"), Repeated.Entries.Num(), 2);
	if (!TestTrue(TEXT("Multi-mesh fixture exported"), WriteAnimationImportFixture(Filename, true))) return false;
	Arguments->SetStringField(TEXT("AssetType"), TEXT("SkeletalMesh"));
	for (const TCHAR* Policy : {TEXT("Fail"), TEXT("Unique"), TEXT("Replace")})
	{
		Arguments->SetStringField(TEXT("ConflictPolicy"), Policy);
		const FCapabilityResult Unsupported = Capability.Run(Arguments);
		TestTrue(TEXT("Multi-mesh names are rejected before factory writes"), Unsupported.FatalError.Contains(TEXT("exactly one mesh node")));
	}
	return true;
}
#endif
