// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusGetAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "MetasoundSource.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#if NX_UE_HAS_METASOUND_PATCH
#include "Metasound.h"
#endif

void FGetAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("get_asset_meta_sound");
	Out.SearchAssetTypes = {TEXT("MetaSoundSource"), TEXT("MetaSoundPatch")};
	Out.Description = TEXT("读取 MetaSound Source / MetaSound Patch：inputs/outputs/节点摘要（≥5.1 支持 Patch）。写用 manage_asset_meta_sound。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("MetaSound Source 或 Patch 资产路径")))
		.Prop(TEXT("assetPaths"), FNexusSchema::StrArr(TEXT("多个路径（批量）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("sound"), TEXT("frontend"), TEXT("document"), TEXT("patch") };
	Out.RelatedCapabilities = { TEXT("manage_asset_meta_sound"), TEXT("create_asset_meta_sound"), TEXT("create_asset_meta_sound_patch"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("读取 MetaSound Source 或 Patch 的 inputs/outputs/节点；写用 manage_asset_meta_sound");
}

static void CollectPaths(const TSharedPtr<FJsonObject>& Args, TArray<FString>& Out)
{
	Out.Reset();
	FString Single;
	if (Args->TryGetStringField(TEXT("assetPath"), Single) && !Single.IsEmpty())
		Out.Add(Single);
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Args->TryGetArrayField(TEXT("assetPaths"), Arr))
		for (auto& V : *Arr) { FString S; if (V->TryGetString(S) && !S.IsEmpty()) Out.AddUnique(S); }
}

FCapabilityResult FGetAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		TArray<FString> Paths;
		CollectPaths(Arguments, Paths);
		if (Paths.IsEmpty())
		{
			FNexusCapability::EmitError(OutEntries, {}, TEXT("assetPath 为空"));
			return;
		}

		for (const FString& AssetPath : Paths)
		{
			// 优先尝试 MetaSoundSource，若失败再尝试 MetaSoundPatch（≥5.1）
			UObject* SoundAsset = nullptr;
			FString ActualType;
			UMetaSoundSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundSource>(AssetPath);
			if (Source) { SoundAsset = Source; ActualType = TEXT("MetaSoundSource"); }
#if NX_UE_HAS_METASOUND_PATCH
			else
			{
				UMetaSoundPatch* Patch = FNexusAssetUtils::LoadAssetWithFallback<UMetaSoundPatch>(AssetPath);
				if (Patch) { SoundAsset = Patch; ActualType = TEXT("MetaSoundPatch"); }
			}
#endif
			if (!SoundAsset)
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
				ErrObj->SetStringField(TEXT("assetPath"), AssetPath);
				ErrObj->SetStringField(TEXT("error"), TEXT("MetaSound Source / Patch 未找到"));
				OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
				continue;
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("assetPath"), SoundAsset->GetPathName());
			Entry->SetStringField(TEXT("assetType"), ActualType);
			Entry->SetStringField(TEXT("name"),      SoundAsset->GetName());

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
			// 通过 IMetaSoundDocumentInterface 接口读取 Frontend Document（Source 和 Patch ≥5.3 均实现该接口）
			IMetaSoundDocumentInterface* DocIface = Cast<IMetaSoundDocumentInterface>(SoundAsset);
			if (DocIface)
			{
				const FMetasoundFrontendDocument& Doc = DocIface->GetConstDocument();
				const FMetasoundFrontendClassInterface& Iface = Doc.RootGraph.Interface;

				// inputs
				TArray<TSharedPtr<FJsonValue>> InputsArr;
				for (const FMetasoundFrontendClassInput& Input : Iface.Inputs)
				{
					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("name"),     Input.Name.ToString());
					Obj->SetStringField(TEXT("typeName"), Input.TypeName.ToString());
					InputsArr.Add(MakeShared<FJsonValueObject>(Obj));
				}
				Entry->SetArrayField(TEXT("inputs"), InputsArr);

				// outputs
				TArray<TSharedPtr<FJsonValue>> OutputsArr;
				for (const FMetasoundFrontendClassOutput& Output : Iface.Outputs)
				{
					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("name"),     Output.Name.ToString());
					Obj->SetStringField(TEXT("typeName"), Output.TypeName.ToString());
					OutputsArr.Add(MakeShared<FJsonValueObject>(Obj));
				}
				Entry->SetArrayField(TEXT("outputs"), OutputsArr);

			// 图节点与连线：遍历所有分页图
			TArray<TSharedPtr<FJsonValue>> NodesArr;
			TArray<TSharedPtr<FJsonValue>> EdgesArr;
			Doc.RootGraph.IterateGraphPages([&](const FMetasoundFrontendGraph& Graph)
			{
				for (const FMetasoundFrontendNode& Node : Graph.Nodes)
				{
					TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
					NObj->SetStringField(TEXT("id"),   Node.GetID().ToString());
					NObj->SetStringField(TEXT("name"), Node.Name.ToString());
					// 输入/输出引脚名（用于 add_edge 的 fromPin/toPin）
					TArray<TSharedPtr<FJsonValue>> InPins, OutPins;
					for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
						InPins.Add(MakeShared<FJsonValueString>(V.Name.ToString()));
					for (const FMetasoundFrontendVertex& V : Node.Interface.Outputs)
						OutPins.Add(MakeShared<FJsonValueString>(V.Name.ToString()));
					NObj->SetArrayField(TEXT("inputPins"),  InPins);
					NObj->SetArrayField(TEXT("outputPins"), OutPins);
					NodesArr.Add(MakeShared<FJsonValueObject>(NObj));
				}
				// 边（连线）
				for (const FMetasoundFrontendEdge& Edge : Graph.Edges)
				{
					TSharedPtr<FJsonObject> EObj = MakeShared<FJsonObject>();
					EObj->SetStringField(TEXT("fromNodeID"), Edge.From.NodeID.ToString());
					EObj->SetStringField(TEXT("fromPin"),    Edge.From.VertexName.ToString());
					EObj->SetStringField(TEXT("toNodeID"),   Edge.To.NodeID.ToString());
					EObj->SetStringField(TEXT("toPin"),      Edge.To.VertexName.ToString());
					EdgesArr.Add(MakeShared<FJsonValueObject>(EObj));
				}
			});
			Entry->SetArrayField(TEXT("nodes"), NodesArr);
			Entry->SetArrayField(TEXT("edges"), EdgesArr);

				// 接口版本
				TArray<TSharedPtr<FJsonValue>> IfaceVersions;
				for (const FMetasoundFrontendVersion& Ver : Doc.Interfaces)
				{
					IfaceVersions.Add(MakeShared<FJsonValueString>(Ver.Name.ToString()));
				}
				Entry->SetArrayField(TEXT("interfaces"), IfaceVersions);
			}
#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetMetaSoundCapability)

#endif // WITH_METASOUND
