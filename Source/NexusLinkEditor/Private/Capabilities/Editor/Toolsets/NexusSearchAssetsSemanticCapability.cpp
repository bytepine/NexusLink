// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusSearchAssetsSemanticCapability.h"
#if WITH_NEXUS_SEMANTIC_SEARCH
#include "Utils/NexusEditorToolSchema.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "ISemanticSearchModule.h"
#include "HybridSearchIndex.h"
#include "AssetProcessorManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Internationalization/Regex.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"

struct FNexusSemanticRequest final
{
    TSharedPtr<FNexusEditorOperation> Operation;
    TMap<int64, FAssetData> Assets;
    TArray<int64> Candidates;
    TArray<TSharedPtr<FJsonValue>> Items;
    int32 PendingCaptions = 0;
    FString Query;
    int32 Limit = 10;
};

static void NexusCompleteSemanticRequest(const TSharedRef<FNexusSemanticRequest>& Request)
{
    if (!Request->Operation->IsRunning() || Request->PendingCaptions != 0) return;
    auto Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("Items"), Request->Items);
    Result->SetNumberField(TEXT("Total"), Request->Items.Num());
    auto Envelope = MakeShared<FJsonObject>();
    Envelope->SetObjectField(TEXT("CompatibilityResult"), Result);
    FNexusEditorOperations::Finish(Request->Operation, TEXT("Completed"), Envelope);
}

static void NexusReceiveSemanticResults(const TSharedRef<FNexusSemanticRequest>& Request,
    TArray<UE::SemanticSearch::FHybridSearchResult>&& Hits)
{
    if (!Request->Operation->IsRunning()) return;
    // 先建立所有结果再请求说明；缓存命中时回调可能同步执行。
    TArray<FAssetData> ResultAssets;
    for (const auto& Hit : Hits)
    {
        const auto* Asset = Request->Assets.Find(Hit.ID);
        if (!Asset) continue;
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("Path"), Asset->GetSoftObjectPath().ToString());
        Item->SetStringField(TEXT("Class"), Asset->AssetClassPath.ToString());
        Item->SetStringField(TEXT("Caption"), TEXT(""));
        Item->SetNumberField(TEXT("Score"), Hit.RRFScore);
        Item->SetNumberField(TEXT("VectorDistance"), Hit.VectorDistance);
        Item->SetNumberField(TEXT("BM25Score"), Hit.BM25Score);
        Request->Items.Add(MakeShared<FJsonValueObject>(Item));
        ResultAssets.Add(*Asset);
    }
    Request->PendingCaptions = ResultAssets.Num() + 1;
    for (int32 AssetIndex = 0; AssetIndex < ResultAssets.Num(); ++AssetIndex)
    {
        const bool bStarted = UE::SemanticSearch::FAssetProcessorManager::Get().GetCaptionData(ResultAssets[AssetIndex],
            [Request, AssetIndex](UE::SemanticSearch::FAssetCaptionResult&& Caption, FString&& Error, UE::SemanticSearch::EAssetIndexFailureReason)
            {
                auto CompleteCaption = [Request, AssetIndex, Caption = MoveTemp(Caption), Error = MoveTemp(Error)]()
                {
                    if (!Request->Operation->IsRunning()) return;
                    if (Error.IsEmpty()) Request->Items[AssetIndex]->AsObject()->SetStringField(TEXT("Caption"), Caption.Caption);
                    else Request->Items[AssetIndex]->AsObject()->SetStringField(TEXT("CaptionError"), Error);
                    --Request->PendingCaptions;
                    NexusCompleteSemanticRequest(Request);
                };
                if (IsInGameThread()) CompleteCaption();
                else AsyncTask(ENamedThreads::GameThread, MoveTemp(CompleteCaption));
            });
        if (!bStarted) --Request->PendingCaptions;
    }
    --Request->PendingCaptions;
    NexusCompleteSemanticRequest(Request);
}

static void NexusRunSemanticRequest(const TSharedRef<FNexusSemanticRequest>& Request, TArray<float>&& Embedding)
{
    if (!Request->Operation->IsRunning()) return;
    const auto Stats = UE::SemanticSearch::ISemanticSearchModule::Get().GetIndexStats();
    if (Embedding.Num() == 0 || (Stats.Dimension > 0 && Stats.Dimension != Embedding.Num()))
    {
        FNexusEditorOperations::Finish(Request->Operation, TEXT("Failed"), nullptr, TEXT("Embedding dimensions do not match the semantic index"));
        return;
    }
    UE::SemanticSearch::ISemanticSearchModule::Get().SearchAsync(Request->Query, Embedding, Request->Limit,
        Request->Candidates, TNumericLimits<float>::Max(),
        [Request](TArray<UE::SemanticSearch::FHybridSearchResult>&& Hits) { NexusReceiveSemanticResults(Request, MoveTemp(Hits)); });
}

void FNexusSearchAssetsSemanticCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("SearchAssetsSemantic");
    Out.Description = TEXT("Search indexed assets by meaning or similarity. Calls the embedding/index service directly; requires SemanticSearch and a model.");
    Out.InputSchema = FNexusSchema::Object()
        .Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Semantic search kind"), {TEXT("Search"), TEXT("FindSimilar")}, TEXT("Search")))
        .Prop(TEXT("Query"), FNexusSchema::Str(TEXT("Natural-language query for Search")))
        .Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Indexed source asset object path for FindSimilar")))
        .Prop(TEXT("ClassPaths"), FNexusSchema::StrArr(TEXT("1-32 indexed asset class paths; e.g. /Script/Engine.Material")))
        .Prop(TEXT("PathRegexes"), FNexusSchema::StrArr(TEXT("Optional asset path regular expressions, maximum 32")))
        .Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum semantic matches"), 10, 1, 100))
        .Required({TEXT("ClassPaths")}).Build();
    FNexusEditorToolSchema::Configure(Out, false);
    Out.Prerequisites.Add(TEXT("PluginEnabled:SemanticSearch"));
    Out.ExtraSearchKeywords = {TEXT("semantic"), TEXT("similarity"), TEXT("vector"), TEXT("meaning")};
    Out.RelatedCapabilities = {TEXT("SearchAsset"), TEXT("GetEditorOperation")};
}

FCapabilityResult FNexusSearchAssetsSemanticCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
    const FNexusArgs Args(Arguments);
    const FString Action = Args.Str(TEXT("Action"), TEXT("Search"));
    if (Action != TEXT("Search") && Action != TEXT("FindSimilar")) return FCapabilityResult::MakeArgInvalid(TEXT("Unknown semantic search action"));
    const TArray<FString> ClassPaths = Args.StrArr(TEXT("ClassPaths"));
    const TArray<FString> Regexes = Args.StrArr(TEXT("PathRegexes"));
    if (ClassPaths.Num() == 0 || ClassPaths.Num() > 32 || Regexes.Num() > 32)
        return FCapabilityResult::MakeArgInvalid(TEXT("ClassPaths requires 1-32 classes; PathRegexes permits at most 32 entries"));
    const FString Query = Args.Str(TEXT("Query")).TrimStartAndEnd();
    const FString AssetPath = Args.Str(TEXT("AssetPath"));
    if ((Action == TEXT("Search") ? Query : AssetPath).IsEmpty())
        return FCapabilityResult::MakeArgInvalid(TEXT("Search requires Query; FindSimilar requires AssetPath"));
    TArray<FRegexPattern> Patterns;
    for (const FString& Regex : Regexes)
    {
        if (Regex.IsEmpty() || Regex.Len() > 2048) return FCapabilityResult::MakeArgInvalid(TEXT("Path regex requires 1-2048 characters"));
        Patterns.Emplace(Regex);
    }
    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    for (const FString& ClassPath : ClassPaths)
    {
        const FSoftObjectPath ClassReference(ClassPath);
        if (!ClassReference.IsValid()) return FCapabilityResult::MakeArgInvalid(TEXT("ClassPaths must contain full class object paths"));
        Filter.ClassPaths.Add(FTopLevelAssetPath(ClassPath));
    }
    auto& Service = UE::SemanticSearch::ISemanticSearchModule::Get();
    if (!Service.IsInitialized() || Service.GetIndexStats().VectorCount == 0)
        return FCapabilityResult::MakeFatal(TEXT("SemanticSearch vector index is not initialized or is empty"));
    const auto Provider = Service.GetEmbeddingProvider();
    if (Action == TEXT("Search") && !Provider.IsValid()) return FCapabilityResult::MakeFatal(TEXT("SemanticSearch has no embedding provider"));
    auto Operation = FNexusEditorOperations::Create(TEXT("Independent"));
    if (!Operation.IsValid()) return FNexusEditorToolSchema::Result(Operation);
    auto Request = MakeShared<FNexusSemanticRequest>();
    Request->Operation = Operation;
    Request->Query = Action == TEXT("Search") ? Query : FString();
    Request->Limit = FMath::Clamp(static_cast<int32>(Args.Num(TEXT("Limit"), 10)), 1, 100);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector() || !UE::SemanticSearch::ISemanticSearchModule::IsInIndexedFolder(Asset)) continue;
        const FString Path = Asset.GetSoftObjectPath().ToString();
        bool bMatches = Patterns.Num() == 0;
        for (const auto& Pattern : Patterns)
        {
            FRegexMatcher Matcher(Pattern, Path);
            if (Matcher.FindNext()) { bMatches = true; break; }
        }
        if (!bMatches || (Action == TEXT("FindSimilar") && Path == AssetPath)) continue;
        const int64 Identifier = UE::SemanticSearch::GetAssetIndexID(Asset);
        Request->Assets.Add(Identifier, Asset);
        Request->Candidates.Add(Identifier);
    }
    FAssetData Source;
    if (Action == TEXT("FindSimilar"))
    {
        Source = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
        if (!Source.IsValid())
        {
            FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("Source asset does not exist"));
            return FNexusEditorToolSchema::Result(Operation);
        }
    }
    if (Request->Candidates.Num() == 0)
    {
        NexusCompleteSemanticRequest(Request);
        return FNexusEditorToolSchema::Result(Operation);
    }
    TWeakPtr<FNexusEditorOperation> WeakOperation = Operation;
    FNexusEditorControlServices::AddTicker(FTickerDelegate::CreateLambda([WeakOperation](float)
    {
        const auto Active = WeakOperation.Pin();
        if (!Active.IsValid() || !Active->IsRunning()) return false;
        if (FPlatformTime::Seconds() - Active->StartedAt < 120) return true;
        FNexusEditorOperations::Finish(Active, TEXT("Failed"), nullptr, TEXT("Semantic request timed out"));
        return false;
    }), 1.0f);
    Operation->Stop = [WeakOperation]()
    {
        const auto Active = WeakOperation.Pin();
        if (!Active.IsValid() || !Active->IsRunning()) return false;
        FNexusEditorOperations::Finish(Active, TEXT("Cancelled"), nullptr, TEXT("Stopped waiting for this semantic request"));
        return true;
    };
    if (Action == TEXT("FindSimilar"))
    {
        const int64 Identifier = UE::SemanticSearch::GetAssetIndexID(Source);
        UE::SemanticSearch::FHybridSearchIndex::Get().ExtractEmbeddingsAsync({Identifier},
            [Request](TArray<int64>&& Identifiers, TArray<float>&& Vectors, int32 Dimension)
            {
                if (!Request->Operation->IsRunning()) return;
                if (Identifiers.Num() != 1 || Dimension <= 0 || Vectors.Num() != Dimension)
                    FNexusEditorOperations::Finish(Request->Operation, TEXT("Failed"), nullptr, TEXT("Source asset is not present in the semantic vector index"));
                else NexusRunSemanticRequest(Request, MoveTemp(Vectors));
            });
    }
    else
    {
        Provider->GenerateEmbeddingAsync(Query,
            [Request, Provider](UE::SemanticSearch::FEmbeddingResponse&& Response)
            {
                auto Continue = [Request, Provider, Response = MoveTemp(Response)]() mutable
                {
                    if (!Request->Operation->IsRunning()) return;
                    if (!Response.ErrorMessage.IsEmpty()) FNexusEditorOperations::Finish(Request->Operation, TEXT("Failed"), nullptr, Response.ErrorMessage);
                    else NexusRunSemanticRequest(Request, MoveTemp(Response.Embedding));
                };
                if (IsInGameThread()) Continue();
                else AsyncTask(ENamedThreads::GameThread, MoveTemp(Continue));
            }, nullptr);
    }
    return FNexusEditorToolSchema::Result(Operation);
}
REGISTER_MCP_CAPABILITY(FNexusSearchAssetsSemanticCapability)
#endif
