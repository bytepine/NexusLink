// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/MetaSound/NexusGetAssetMetaSoundCapability.h"

#if WITH_METASOUND

#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpTool.h"
#include "MetasoundSource.h"
#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
#include "MetasoundDocumentInterface.h"
#endif
#include "MetasoundFrontendDocument.h"
#if NX_UE_HAS_METASOUND_PATCH
#include "Metasound.h"
#endif

void FNexusGetAssetMetaSoundCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("GetAssetMetaSound");
	Out.SearchAssetTypes = {TEXT("MetaSoundSource"), TEXT("MetaSoundPatch")};
	Out.Description = TEXT("Read MetaSound Source/Patch: inputs/outputs/node summary (Patch >=5.1). Use ManageAssetMetaSound for writes.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("MetaSound Source or Patch asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("metasound"), TEXT("audio"), TEXT("Sound"), TEXT("frontend"), TEXT("document"), TEXT("patch") };
	Out.RelatedCapabilities = { TEXT("ManageAssetMetaSound"), TEXT("CreateAssetMetaSound"), TEXT("CreateAssetMetaSoundPatch"), TEXT("SearchAsset") };
	Out.WhenToUse = TEXT("Read MetaSound Source/Patch inputs/outputs/nodes; use ManageAssetMetaSound for writes");
}

FCapabilityResult FNexusGetAssetMetaSoundCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("AssetPath"));

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
			ErrObj->SetStringField(TEXT("Path"), AssetPath);
			ErrObj->SetStringField(TEXT("Error"), TEXT("MetaSound Source / Patch not found"));
			OutEntries.Add(MakeShared<FJsonValueObject>(ErrObj));
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), SoundAsset->GetPathName());
		Entry->SetStringField(TEXT("AssetType"), ActualType);
		Entry->SetStringField(TEXT("name"),      SoundAsset->GetName());

#if NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT
		// 通过 IMetaSoundDocumentInterface 接口读取 Frontend Document（Source 和 Patch ≥5.3 均实现该接口）
		IMetaSoundDocumentInterface* DocIface = Cast<IMetaSoundDocumentInterface>(SoundAsset);
		if (DocIface)
		{
#if NX_UE_HAS_METASOUND_GET_CONST_DOCUMENT
			const FMetasoundFrontendDocument& Doc = DocIface->GetConstDocument();
#else
			const FMetasoundFrontendDocument& Doc =
				static_cast<const IMetaSoundDocumentInterface*>(DocIface)->GetDocument();
#endif
#if NX_UE_HAS_METASOUND_CLASS_GET_DEFAULT_INTERFACE
			const FMetasoundFrontendClassInterface& Iface = Doc.RootGraph.GetDefaultInterface();
#else
			const FMetasoundFrontendClassInterface& Iface = Doc.RootGraph.Interface;
#endif

			// inputs
			TArray<TSharedPtr<FJsonValue>> InputsArr;
			for (const FMetasoundFrontendClassInput& Input : Iface.Inputs)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"),     Input.Name.ToString());
				Obj->SetStringField(TEXT("TypeName"), Input.TypeName.ToString());
				InputsArr.Add(MakeShared<FJsonValueObject>(Obj));
			}
			Entry->SetArrayField(TEXT("Inputs"), InputsArr);

			// outputs
			TArray<TSharedPtr<FJsonValue>> OutputsArr;
			for (const FMetasoundFrontendClassOutput& Output : Iface.Outputs)
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"),     Output.Name.ToString());
				Obj->SetStringField(TEXT("TypeName"), Output.TypeName.ToString());
				OutputsArr.Add(MakeShared<FJsonValueObject>(Obj));
			}
			Entry->SetArrayField(TEXT("Outputs"), OutputsArr);

		// 图节点与连线：遍历所有分页图
		TArray<TSharedPtr<FJsonValue>> NodesArr;
		TArray<TSharedPtr<FJsonValue>> EdgesArr;
		auto CollectGraph = [&](const FMetasoundFrontendGraph& Graph)
		{
			for (const FMetasoundFrontendNode& Node : Graph.Nodes)
			{
				TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
				NObj->SetStringField(TEXT("Id"),   Node.GetID().ToString());
				NObj->SetStringField(TEXT("name"), Node.Name.ToString());
				// 输入/输出引脚名（用于 AddEdge 的 FromPin/ToPin）
				TArray<TSharedPtr<FJsonValue>> InPins, OutPins;
				for (const FMetasoundFrontendVertex& V : Node.Interface.Inputs)
					InPins.Add(MakeShared<FJsonValueString>(V.Name.ToString()));
				for (const FMetasoundFrontendVertex& V : Node.Interface.Outputs)
					OutPins.Add(MakeShared<FJsonValueString>(V.Name.ToString()));
				NObj->SetArrayField(TEXT("InputPins"),  InPins);
				NObj->SetArrayField(TEXT("OutputPins"), OutPins);
				NodesArr.Add(MakeShared<FJsonValueObject>(NObj));
			}
			// 边（连线）
			for (const FMetasoundFrontendEdge& Edge : Graph.Edges)
			{
				TSharedPtr<FJsonObject> EObj = MakeShared<FJsonObject>();
				EObj->SetStringField(TEXT("FromNodeId"), Edge.FromNodeID.ToString());
				EObj->SetStringField(TEXT("ToNodeId"), Edge.ToNodeID.ToString());
				FName FromPin, ToPin;
				for (const FMetasoundFrontendNode& N : Graph.Nodes)
				{
					if (N.GetID() == Edge.FromNodeID)
					{
						for (const FMetasoundFrontendVertex& V : N.Interface.Outputs)
						{
							if (V.VertexID == Edge.FromVertexID) { FromPin = V.Name; break; }
						}
					}
					if (N.GetID() == Edge.ToNodeID)
					{
						for (const FMetasoundFrontendVertex& V : N.Interface.Inputs)
						{
							if (V.VertexID == Edge.ToVertexID) { ToPin = V.Name; break; }
						}
					}
				}
				EObj->SetStringField(TEXT("FromPin"), FromPin.ToString());
				EObj->SetStringField(TEXT("ToPin"), ToPin.ToString());
				EdgesArr.Add(MakeShared<FJsonValueObject>(EObj));
			}
		};
#if NX_UE_HAS_METASOUND_GRAPH_PAGES
		Doc.RootGraph.IterateGraphPages(CollectGraph);
#else
		// 5.3/5.4 无分页图，RootGraph 直接持有唯一 Graph
		CollectGraph(Doc.RootGraph.Graph);
#endif
		Entry->SetArrayField(TEXT("Nodes"), NodesArr);
		Entry->SetArrayField(TEXT("Edges"), EdgesArr);

			// 接口版本
			TArray<TSharedPtr<FJsonValue>> IfaceVersions;
			for (const FMetasoundFrontendVersion& Ver : Doc.Interfaces)
			{
				IfaceVersions.Add(MakeShared<FJsonValueString>(Ver.Name.ToString()));
			}
			Entry->SetArrayField(TEXT("Interfaces"), IfaceVersions);
		}
#endif // NX_UE_HAS_METASOUND_FRONTEND_DOCUMENT

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetMetaSoundCapability)

#endif // WITH_METASOUND
