// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Asset/Mesh/NexusCreateAssetPhysicsAssetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusVersionCompat.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
void FNexusCreateAssetPhysicsAssetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("CreateAssetPhysicsAsset");
    Out.Description = TEXT("Create and save a PhysicsAsset from an existing SkeletalMesh using engine collision generation. Existing assets are never replaced.");
    Out.InputSchema = FNexusSchema::Object().Required(TEXT("AssetPath"), FNexusSchema::Str(TEXT("New PhysicsAsset package path")))
        .Required(TEXT("SkeletalMeshPath"), FNexusSchema::Str(TEXT("SkeletalMesh path from asset discovery")))
        .Prop(TEXT("bAssignToMesh"), FNexusSchema::Bool(TEXT("Assign and save the new physics asset on the source mesh"), true, false)).Build();
    Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Editor};
    Out.SearchAssetTypes = {TEXT("PhysicsAsset")};
    Out.RelatedCapabilities = {TEXT("GetAssetPhysicsAsset"), TEXT("ManageAssetPhysicsAsset"), TEXT("GetAssetSkeletalMesh")};
}
FCapabilityResult FNexusCreateAssetPhysicsAssetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    return FNexusCapabilityResultBuilder::Build([&](auto& Entries, auto& Top, auto& Error) {
        const FNexusArgs Args(Arguments);
        USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(Args.Str(TEXT("SkeletalMeshPath")));
        if (!Mesh || FNexusAssetUtils::GetSkeletalMeshRefSkeleton(*Mesh).GetNum() == 0) { Error = TEXT("SkeletalMeshPath must resolve to a mesh with bones"); return; }
        const FString Destination = Args.Str(TEXT("AssetPath"));
        const FString DestinationObject = Destination + TEXT(".") + FPaths::GetBaseFilename(Destination);
        if (StaticFindObject(UObject::StaticClass(), nullptr, *DestinationObject) || FPackageName::DoesPackageExist(Destination)) { Error = TEXT("Destination asset/package already exists"); return; }
        const auto Created = FNexusAssetUtils::CreatePlainAsset<UPhysicsAsset>(Args.Str(TEXT("AssetPath")), RF_Public | RF_Standalone | RF_Transactional, false);
        if (!Created.Ok()) { Error = Created.Error; return; }
        UPhysicsAsset* Asset = Cast<UPhysicsAsset>(Created.Asset);
        if (!Asset) { Error = TEXT("Could not create PhysicsAsset"); return; }
        if (Args.Bool(TEXT("bAssignToMesh"))) Mesh->Modify();
        FText Failure; FPhysAssetCreateParams Params;
        if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset, Mesh, Params, Failure, Args.Bool(TEXT("bAssignToMesh"))
#if NX_UE_HAS_PHYSICS_ASSET_PROGRESS_ARG
            , false
#endif
        )) {
            Error = Failure.ToString();
            // 生成失败的临时对象不占据用户请求的目标名，也不注册或写入资产。
            Asset->ClearFlags(RF_Public | RF_Standalone);
            const FName TemporaryName = MakeUniqueObjectName(GetTransientPackage(), UPhysicsAsset::StaticClass());
            Asset->Rename(*TemporaryName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            return;
        }
        if (!FNexusAssetUtils::NotifyAndSaveCreated(Asset->GetOutermost(), Asset, Args.Str(TEXT("AssetPath")))) { Error = TEXT("PhysicsAsset created but save failed"); return; }
        FNexusCapabilityResultBuilder::AddCreatedEntry(Entries, Asset);
        if (Args.Bool(TEXT("bAssignToMesh"))) { Mesh->MarkPackageDirty(); if (!FNexusAssetUtils::SaveNewAsset(Mesh->GetOutermost(), Mesh, FNexusAssetUtils::PackagePathOf(Mesh))) Error = TEXT("Mesh assignment succeeded but mesh save failed"); }
    });
}
REGISTER_MCP_CAPABILITY(FNexusCreateAssetPhysicsAssetCapability)
