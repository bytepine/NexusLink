// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/IKRig/NexusManageAssetIKRigCapability.h"

#if WITH_IK_RIG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Rig/IKRigDefinition.h"
#include "Rig/Solvers/IKRigSolver.h"
#include "Engine/SkeletalMesh.h"
#include "NexusMcpTool.h"

void FManageAssetIKRigCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_ik_rig");
	Out.SearchAssetTypes = {TEXT("IKRig"), TEXT("IKRigDefinition")};
	Out.Description = TEXT("编辑 IKRig：set_preview_mesh / set_solver_enabled。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_preview_mesh"), TEXT("set_solver_enabled") }))
		.Prop(TEXT("meshPath"),       FNexusSchema::Str(TEXT("SkeletalMesh 路径（set_preview_mesh）")))
		.Prop(TEXT("solverIndex"),    FNexusSchema::Int(TEXT("Solver 索引（set_solver_enabled）")))
		.Prop(TEXT("enabled"),        FNexusSchema::Bool(TEXT("是否启用（set_solver_enabled）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"),  FNexusSchema::Str(TEXT("IKRig 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("ikrig"), TEXT("ik"), TEXT("solver"), TEXT("preview mesh") };
	Out.RelatedCapabilities = { TEXT("get_asset_ik_rig"), TEXT("create_asset_ik_rig") };
	Out.WhenToUse = TEXT("修改 IKRig 属性；修改后需 save_asset 落盘");
}

FCapabilityResult FManageAssetIKRigCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UIKRigDefinition* IKRig = FNexusAssetUtils::LoadAssetWithFallback<UIKRigDefinition>(AssetPath);
		if (!IKRig)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("IKRig 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}}, TEXT("缺少 operations 数组"));
			return;
		}

		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpObjPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			TSharedPtr<FJsonObject> ResEntry = MakeShared<FJsonObject>();
			ResEntry->SetStringField(TEXT("assetPath"), AssetPath);
			ResEntry->SetStringField(TEXT("action"), Action);

			if (Action.Equals(TEXT("set_preview_mesh"), ESearchCase::IgnoreCase))
			{
				FString MeshPath;
				Op->TryGetStringField(TEXT("meshPath"), MeshPath);
				if (MeshPath.IsEmpty())
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("set_preview_mesh 需要 meshPath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				USkeletalMesh* Mesh = FNexusAssetUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
				if (!Mesh)
				{
					ResEntry->SetStringField(TEXT("error"),
						FString::Printf(TEXT("SkeletalMesh 未找到: %s"), *MeshPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				IKRig->SetPreviewMesh(Mesh, true);
				bDirty = true;
				ResEntry->SetBoolField(TEXT("success"), true);
				ResEntry->SetStringField(TEXT("meshPath"), MeshPath);
			}
			else if (Action.Equals(TEXT("set_solver_enabled"), ESearchCase::IgnoreCase))
			{
				int32 SolverIdx = -1;
				if (Op->HasField(TEXT("solverIndex")))
					SolverIdx = static_cast<int32>(Op->GetNumberField(TEXT("solverIndex")));
				bool bEnabled = true;
				if (Op->HasField(TEXT("enabled")))
					Op->TryGetBoolField(TEXT("enabled"), bEnabled);
				// mutable access via GetSolverArray (const) -- need cast
				const TArray<UIKRigSolver*>& Solvers = IKRig->GetSolverArray();
				if (!Solvers.IsValidIndex(SolverIdx))
				{
					ResEntry->SetStringField(TEXT("error"), TEXT("solverIndex 越界"));
					OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry)); continue;
				}
				// IKRig asset's solvers are read-only; modification requires IKRigController API
				// but IKRigController is editor-only (IKRigDeveloper module)
#if WITH_EDITOR
				Solvers[SolverIdx]->SetEnabled(bEnabled);
				bDirty = true;
				ResEntry->SetBoolField(TEXT("success"), true);
				ResEntry->SetNumberField(TEXT("solverIndex"), SolverIdx);
				ResEntry->SetBoolField(TEXT("enabled"), bEnabled);
#else
				ResEntry->SetStringField(TEXT("error"), TEXT("set_solver_enabled 仅编辑器可用"));
#endif
			}
			else
			{
				ResEntry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(ResEntry));
		}

		if (bDirty)
		{
			IKRig->MarkPackageDirty();
			OutTop->SetStringField(TEXT("note"), TEXT("已修改，用 save_asset 落盘"));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetIKRigCapability)

#endif // WITH_IK_RIG
