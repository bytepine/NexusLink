// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PCG/NexusManageAssetPCGGraphCapability.h"

#if WITH_PCG

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "UObject/UObjectGlobals.h"

void FManageAssetPCGGraphCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_pcg_graph");
	Out.SearchAssetTypes = {TEXT("PCGGraph")};
	Out.Description = TEXT("管理 PCG Graph：add_node/remove_node/add_edge（UE 5.4+）。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PCG Graph 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("操作列表")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pcg"), TEXT("procedural"), TEXT("node"), TEXT("edge"), TEXT("connect") };
	Out.RelatedCapabilities = { TEXT("get_asset_pcg_graph"), TEXT("create_asset_pcg_graph") };
	Out.WhenToUse = TEXT("向 PCG Graph 添加/删除节点或连接 pin");
}

static void ApplyPCGOperation(const TSharedPtr<FJsonObject>& Op, UPCGGraph* Graph,
	TArray<TSharedPtr<FJsonValue>>& Results)
{
	FString Action;
	Op->TryGetStringField(TEXT("action"), Action);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("action"), Action);

	if (Action == TEXT("add_node"))
	{
		FString SettingsClassName;
		if (!Op->TryGetStringField(TEXT("settingsClass"), SettingsClassName) || SettingsClassName.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_node 需要 settingsClass (UPCGSettings 子类名)"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

#if NX_UE_HAS_FIND_FIRST_OBJECT
		UClass* SettingsClass = FindFirstObject<UClass>(*SettingsClassName, EFindFirstObjectOptions::NativeFirst);
#else
		UClass* SettingsClass = FindObject<UClass>(ANY_PACKAGE, *SettingsClassName);
#endif
		if (!SettingsClass || !SettingsClass->IsChildOf(UPCGSettings::StaticClass()))
		{
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("settingsClass '%s' 未找到或不是 UPCGSettings 子类"), *SettingsClassName));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGSettings* NewSettings = NewObject<UPCGSettings>(Graph, SettingsClass, NAME_None, RF_Transactional);
		if (!NewSettings)
		{
			Result->SetStringField(TEXT("error"), TEXT("创建 PCGSettings 实例失败"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* NewNode = Graph->AddNode(NewSettings);
		if (!NewNode)
		{
			Result->SetStringField(TEXT("error"), TEXT("AddNode 失败"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("nodeId"), NewNode->GetName());
		Result->SetStringField(TEXT("settingsClass"), SettingsClassName);
	}
	else if (Action == TEXT("remove_node"))
	{
		FString NodeId;
		Op->TryGetStringField(TEXT("nodeId"), NodeId);
		if (NodeId.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("remove_node 需要 nodeId"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* TargetNode = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->GetName() == NodeId) { TargetNode = Node; break; }
		}
		if (!TargetNode)
		{
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("节点 '%s' 未找到"), *NodeId));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		Graph->RemoveNode(TargetNode);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("nodeId"), NodeId);
	}
	else if (Action == TEXT("add_edge"))
	{
		FString FromId, FromPin, ToId, ToPin;
		Op->TryGetStringField(TEXT("fromNodeId"), FromId);
		Op->TryGetStringField(TEXT("fromPin"),    FromPin);
		Op->TryGetStringField(TEXT("toNodeId"),   ToId);
		Op->TryGetStringField(TEXT("toPin"),      ToPin);

		if (FromId.IsEmpty() || ToId.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), TEXT("add_edge 需要 fromNodeId 和 toNodeId"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		UPCGNode* FromNode = nullptr;
		UPCGNode* ToNode   = nullptr;
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (!Node) continue;
			if (Node->GetName() == FromId) FromNode = Node;
			if (Node->GetName() == ToId)   ToNode   = Node;
		}

		if (!FromNode || !ToNode)
		{
			Result->SetStringField(TEXT("error"), TEXT("源节点或目标节点未找到"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		const FName FromLabel = FromPin.IsEmpty() ? NAME_None : FName(*FromPin);
		const FName ToLabel   = ToPin.IsEmpty()   ? NAME_None : FName(*ToPin);
		Graph->AddEdge(FromNode, FromLabel, ToNode, ToLabel);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("未知 action '%s'，支持: add_node/remove_node/add_edge"), *Action));
	}

	Results.Add(MakeShared<FJsonValueObject>(Result));
}

FCapabilityResult FManageAssetPCGGraphCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UPCGGraph* Graph = FNexusAssetUtils::LoadAssetWithFallback<UPCGGraph>(AssetPath);
		if (!Graph)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				FString::Printf(TEXT("PCG Graph 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), OpsArr) || !OpsArr || OpsArr->IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("assetPath"), AssetPath}},
				TEXT("operations 数组为空"));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Results;
		for (const TSharedPtr<FJsonValue>& Val : *OpsArr)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Val->TryGetObject(OpObj) || !OpObj) continue;
			ApplyPCGOperation(*OpObj, Graph, Results);
		}

		Graph->MarkPackageDirty();

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("assetPath"), AssetPath);
		Entry->SetArrayField(TEXT("results"), Results);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPCGGraphCapability)

#endif // WITH_PCG
