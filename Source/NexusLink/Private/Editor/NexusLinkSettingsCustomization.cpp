// Copyright byteyang. All Rights Reserved.

#if WITH_EDITOR

#include "Editor/NexusLinkSettingsCustomization.h"
#include "NexusFeedback.h"
#include "NexusLinkSettings.h"
#include "NexusCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "NexusLinkSettings"

/** 统一绝对路径与分隔符，供路径前缀比较（兼容 UE 4.26，无 FPaths::ConvertRelativeToNormalizedPath）。 */
static FString NexusNormalizeCapPath(const FString& InPath)
{
	FString P = FPaths::ConvertRelativePathToFull(InPath);
	FPaths::NormalizeFilename(P);
	return P;
}

/** MCP Capabilities 设置分组：扫描源码目录；标题由 SortKey（相对路径或 tag）推导。 */
struct FNexusCapSettingsUiLayout
{
	static FString ResolveCapabilitiesSourceRoot()
	{
		auto TryCapsUnderBaseDir = [](const FString& BaseDir) -> FString
		{
			if (BaseDir.IsEmpty())
			{
				return FString();
			}
			const FString CapsDir = NexusNormalizeCapPath(
				BaseDir / TEXT("Source/NexusLink/Private/Capabilities"));
			return FPaths::DirectoryExists(CapsDir) ? CapsDir : FString();
		};

		// 优先：当前已加载 NexusLink 插件的真实根目录（与安装位置无关）
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
		if (Plugin.IsValid())
		{
			const FString Hit = TryCapsUnderBaseDir(Plugin->GetBaseDir());
			if (!Hit.IsEmpty())
			{
				return Hit;
			}
		}

		// 回退：在 Plugins 树下有限深度搜索（兼容插件管理器尚未就绪或非标准路径）
		TFunction<bool(const FString&, int32, FString&)> SearchUnder;
		SearchUnder = [&TryCapsUnderBaseDir, &SearchUnder](const FString& Dir, int32 Depth, FString& OutHit) -> bool
		{
			if (Depth > 5 || Dir.IsEmpty() || !FPaths::DirectoryExists(Dir))
			{
				return false;
			}
			const FString CapsHit = TryCapsUnderBaseDir(Dir);
			if (!CapsHit.IsEmpty())
			{
				OutHit = CapsHit;
				return true;
			}
			TArray<FString> SubDirs;
			IFileManager::Get().FindFiles(SubDirs, *(Dir / TEXT("*")), false, true);
			for (const FString& Sub : SubDirs)
			{
				if (SearchUnder(NexusNormalizeCapPath(Dir / Sub), Depth + 1, OutHit))
				{
					return true;
				}
			}
			return false;
		};

		FString Hit;
		if (SearchUnder(NexusNormalizeCapPath(FPaths::ProjectPluginsDir()), 0, Hit))
		{
			return Hit;
		}
		SearchUnder(NexusNormalizeCapPath(FPaths::EnginePluginsDir()), 0, Hit);
		return Hit;
	}

	static void GatherCapabilityCppRecursive(const FString& Dir, TArray<FString>& OutFullPaths)
	{
		if (!FPaths::DirectoryExists(Dir))
		{
			return;
		}
		TArray<FString> FileNames;
		IFileManager::Get().FindFiles(FileNames, *(Dir / TEXT("*")), true, false);
		for (const FString& Name : FileNames)
		{
			const FString Full = NexusNormalizeCapPath(Dir / Name);
			if (Name.EndsWith(TEXT("Capability.cpp"), ESearchCase::IgnoreCase))
			{
				OutFullPaths.Add(Full);
			}
		}
		TArray<FString> SubDirs;
		IFileManager::Get().FindFiles(SubDirs, *(Dir / TEXT("*")), false, true);
		for (const FString& Sub : SubDirs)
		{
			GatherCapabilityCppRecursive(NexusNormalizeCapPath(Dir / Sub), OutFullPaths);
		}
	}

	static bool TryExtractCapNameFromCpp(const FString& Content, FString& OutCapName)
	{
		const int32 GetNamePos = Content.Find(TEXT("::GetName()"));
		if (GetNamePos == INDEX_NONE)
		{
			return false;
		}
		static const FString Prefix(TEXT("return TEXT(\""));
		const int32 RetPos = Content.Find(*Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, GetNamePos);
		if (RetPos == INDEX_NONE)
		{
			return false;
		}
		const int32 NameStart = RetPos + Prefix.Len();
		const int32 QuotePos = Content.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
		if (QuotePos == INDEX_NONE || QuotePos <= NameStart)
		{
			return false;
		}
		OutCapName = Content.Mid(NameStart, QuotePos - NameStart);
		return !OutCapName.IsEmpty();
	}

	static FString FolderRelFromFullCppPath(const FString& FullPathNorm, const FString& CapsRootNorm)
	{
		if (!FullPathNorm.StartsWith(CapsRootNorm))
		{
			return FString();
		}
		FString Mid = FullPathNorm.Mid(CapsRootNorm.Len());
		while (Mid.Len() > 0 && (Mid[0] == TEXT('/') || Mid[0] == TEXT('\\')))
		{
			Mid.RemoveAt(0, 1);
		}
		return FPaths::GetPath(Mid);
	}

	static FString SegmentToRowLabel(const FString& Seg)
	{
		const FString L = Seg.ToLower();
		if (L == TEXT("asset")) return TEXT("资产");
		if (L == TEXT("ai")) return TEXT("AI");
		if (L == TEXT("animation")) return TEXT("动画");
		if (L == TEXT("blueprint")) return TEXT("蓝图");
		if (L == TEXT("dataasset")) return TEXT("数据资产");
		if (L == TEXT("editor")) return TEXT("编辑器");
		if (L == TEXT("lua")) return TEXT("Lua");
		if (L == TEXT("material")) return TEXT("材质");
		if (L == TEXT("property")) return TEXT("属性");
		if (L == TEXT("runtime")) return TEXT("运行时");
		if (L == TEXT("struct")) return TEXT("结构体");
		if (L == TEXT("widget")) return TEXT("控件");
		return Seg;
	}

	/** 嵌套节点标题：只显示当前路径最后一段的中文（父级为上级文件夹）。 */
	static FString PathKeyToNestedTitle(const FString& PathKey)
	{
		if (PathKey == TEXT("_misc"))
		{
			return TEXT("其他");
		}
		for (const auto& Pair : FNexusLinkSettingsCustomization::GetCategoryMapping())
		{
			if (PathKey.Equals(Pair.Key, ESearchCase::IgnoreCase))
			{
				return Pair.Value;
			}
		}
		FString LastSeg = PathKey;
		int32 SlashIdx = INDEX_NONE;
		if (PathKey.FindLastChar(TEXT('/'), SlashIdx))
		{
			LastSeg = PathKey.Mid(SlashIdx + 1);
		}
		for (const auto& Pair : FNexusLinkSettingsCustomization::GetCategoryMapping())
		{
			if (LastSeg.Equals(Pair.Key, ESearchCase::IgnoreCase))
			{
				return Pair.Value;
			}
		}
		return SegmentToRowLabel(LastSeg);
	}

	static void BuildCapNameToFolderRelMap(TMap<FString, FString>& OutCapToRel)
	{
		static TMap<FString, FString> GCache;
		static bool GReady = false;
		if (GReady)
		{
			OutCapToRel = GCache;
			return;
		}
		GReady = true;
		const FString Root = ResolveCapabilitiesSourceRoot();
		if (Root.IsEmpty())
		{
			OutCapToRel = GCache;
			return;
		}
		const FString RootNorm = NexusNormalizeCapPath(Root);
		TArray<FString> Files;
		GatherCapabilityCppRecursive(RootNorm, Files);
		for (const FString& FPath : Files)
		{
			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *FPath))
			{
				continue;
			}
			FString CapName;
			if (!TryExtractCapNameFromCpp(Content, CapName))
			{
				continue;
			}
			const FString Rel = FolderRelFromFullCppPath(NexusNormalizeCapPath(FPath), RootNorm);
			if (!GCache.Contains(CapName))
			{
				GCache.Add(CapName, Rel);
			}
		}
		OutCapToRel = GCache;
	}
};

const TArray<TPair<FString, FString>>& FNexusLinkSettingsCustomization::GetCategoryMapping()
{
	static const TArray<TPair<FString, FString>> Mapping = {
		MakeTuple(FString(FNexusMcpTags::Editor),    FString(TEXT("编辑器"))),
		MakeTuple(FString(FNexusMcpTags::Blueprint), FString(TEXT("蓝图"))),
		MakeTuple(FString(FNexusMcpTags::Material),  FString(TEXT("材质"))),
		MakeTuple(FString(FNexusMcpTags::Struct),    FString(TEXT("结构体"))),
		MakeTuple(FString(FNexusMcpTags::Data),      FString(TEXT("数据资产"))),
		MakeTuple(FString(FNexusMcpTags::Widget),    FString(TEXT("控件蓝图"))),
		MakeTuple(FString(FNexusMcpTags::Runtime),   FString(TEXT("运行时"))),
		MakeTuple(FString(FNexusMcpTags::Gas),       FString(TEXT("GAS"))),
	};
	return Mapping;
}

TSharedRef<IDetailCustomization> FNexusLinkSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FNexusLinkSettingsCustomization());
}

void FNexusLinkSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, DisabledCapabilities));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, KnownCapabilityKeys));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bCapabilityDefaultsApplied));

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() == 0) return;
	SettingsPtr = Cast<UNexusLinkSettings>(Objects[0].Get());
	if (!SettingsPtr.IsValid()) return;

	CategoryCaps.Empty();
	CategoryCountTexts.Empty();
	CapTreeRoot.Reset();

	TMap<FString, FString> CapToRel;
	FNexusCapSettingsUiLayout::BuildCapNameToFolderRelMap(CapToRel);

	// 收集 cap：优先按源码路径（Capabilities 下相对目录）分组；扫描失败则按 tags 回退 GetCategoryMapping
	for (const FCapRecord& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
	{
		const FNexusCapabilityDefinition& Def = Record.Def;

		FString SortKey;
		if (const FString* Rel = CapToRel.Find(Def.Name))
		{
			SortKey = *Rel;
		}
		else
		{
			for (const auto& Pair : GetCategoryMapping())
			{
				if (Def.HasTag(Pair.Key))
				{
					SortKey = Pair.Key;
					break;
				}
			}
			if (SortKey.IsEmpty())
			{
				SortKey = TEXT("_misc");
			}
		}

		FCapEntry Entry;
		Entry.Name        = Def.Name;
		Entry.Description = Def.Description;
		Entry.Tags        = Def.Tags;
		CategoryCaps.FindOrAdd(SortKey).Add(MoveTemp(Entry));
	}

	for (auto& Pair : CategoryCaps)
	{
		Pair.Value.Sort([](const FCapEntry& A, const FCapEntry& B)
		{
			return A.Name < B.Name;
		});
	}

	CapTreeRoot = BuildCapGroupTree(CategoryCaps);

	// ── AI 反馈分类（置于 MCP Capabilities 之前）────────────────────────────
	IDetailCategoryBuilder& FbCategory = DetailBuilder.EditCategory(
		TEXT("AI 反馈"), LOCTEXT("FeedbackCategory", "AI 反馈"), ECategoryPriority::Uncommon);

	// 隐藏原始 UPROPERTY 自动生成行（由下方自定义行显示，避免重复）
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, bEnableFeedback));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, SearchOverflowThreshold));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, MaxSearchResults));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, SlowCallThresholdMs));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, RedundantCallWindowSec));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNexusLinkSettings, MaxArchiveCount));

	// 统计信息行（目录路径 + 记录数）
	FbCategory.AddCustomRow(LOCTEXT("FeedbackInfo", "反馈信息"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.AutoWrapText(true)
		.Text_Lambda([]() -> FText
		{
			const FString Dir   = FNexusFeedback::GetFeedbackDir();
			const int32   Count = FNexusFeedback::GetRecordCount();
			return FText::FromString(FString::Printf(
				TEXT("目录: %s  |  已记录 %d 条"), *Dir, Count));
		})
	];

	// 操作按钮行
	FbCategory.AddCustomRow(LOCTEXT("FeedbackActions", "操作"))
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		// 打开目录
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FbOpenDir", "打开目录"))
			.ToolTipText(LOCTEXT("FbOpenDirTip", "在文件管理器中打开 .nexus-feedback 目录"))
			.OnClicked_Lambda([]() -> FReply
			{
				const FString Dir = FNexusFeedback::GetFeedbackDir();
				IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
				FPlatformProcess::ExploreFolder(*Dir);
				return FReply::Handled();
			})
		]
		// 导出 Markdown 报告
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FbExport", "导出 Markdown 报告"))
			.ToolTipText(LOCTEXT("FbExportTip", "聚合反馈数据生成 Markdown 报告并自动打开，同时归档 JSONL"))
			.OnClicked_Lambda([]() -> FReply
			{
				if (FNexusFeedback::GetRecordCount() == 0)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("FbExportEmpty", "暂无反馈数据，请先使用 search_capabilities / call_capability 等工具触发记录。"));
					return FReply::Handled();
				}
				const FString ReportPath = FNexusFeedback::ExportReport();
				if (!ReportPath.IsEmpty())
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*ReportPath);
				}
				return FReply::Handled();
			})
		]
		// 浏览器预填 GitHub Issue
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FbCreateIssue", "创建 GitHub Issue"))
			.ToolTipText(LOCTEXT("FbCreateIssueTip", "根据当前反馈数据在浏览器打开 GitHub Issue 预填页（需手动提交）"))
			.OnClicked_Lambda([]() -> FReply
			{
				if (FNexusFeedback::GetRecordCount() == 0)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("FbIssueEmpty", "暂无反馈数据，请先使用 search_capabilities / call_capability 等工具触发记录。"));
					return FReply::Handled();
				}
				if (!FNexusFeedback::OpenIssuePrefillInBrowser())
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("FbIssueFailed", "无法打开浏览器预填页，请检查 Issue 目标仓库设置。"));
				}
				return FReply::Handled();
			})
		]
		// 清空
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FbClear", "清空"))
			.ToolTipText(LOCTEXT("FbClearTip", "删除 feedback.jsonl（不归档）"))
			.OnClicked_Lambda([]() -> FReply
			{
				const EAppReturnType::Type Ret = FMessageDialog::Open(EAppMsgType::YesNo,
					LOCTEXT("FbClearConfirm", "确定要清空所有反馈数据吗？此操作不可撤销。"));
				if (Ret == EAppReturnType::Yes)
				{
					FNexusFeedback::Clear();
				}
				return FReply::Handled();
			})
		]
	];

	// ── MCP Capabilities 分类 ────────────────────────────────────────────────
	IDetailCategoryBuilder& CapCategory = DetailBuilder.EditCategory(
		TEXT("MCP Capabilities"), LOCTEXT("MCPCapsCategory", "MCP Capabilities"), ECategoryPriority::Uncommon);

	CapCategory.AddCustomRow(LOCTEXT("CapQuickActions", "快捷操作"))
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CapSelectAll", "全选"))
			.ToolTipText(LOCTEXT("CapSelectAllTip", "启用所有 Capability"))
			.OnClicked_Lambda([this]() -> FReply
			{
				if (!SettingsPtr.IsValid()) return FReply::Handled();
				for (const auto& Pair : CategoryCaps)
				{
					for (const FCapEntry& E : Pair.Value)
					{
						SettingsPtr->SetCapabilityEnabled(E.Name, true, /*bNotify=*/false);
					}
				}
				SettingsPtr->NotifyCapabilitiesChanged();
				RefreshCategoryHeaders();
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 2.0f, 4.0f, 6.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CapReadOnly", "只读模式"))
			.ToolTipText(LOCTEXT("CapReadOnlyTip", "仅启用带 readonly 标签的 Capability"))
			.OnClicked_Lambda([this]() -> FReply
			{
				if (!SettingsPtr.IsValid()) return FReply::Handled();
				for (const auto& Pair : CategoryCaps)
				{
					for (const FCapEntry& E : Pair.Value)
					{
						const bool bReadonly = E.Tags.Contains(FNexusMcpTags::Readonly);
						SettingsPtr->SetCapabilityEnabled(E.Name, bReadonly, /*bNotify=*/false);
					}
				}
				SettingsPtr->NotifyCapabilitiesChanged();
				RefreshCategoryHeaders();
				return FReply::Handled();
			})
		]
	];

	TSharedRef<SVerticalBox> TreeVBox = SNew(SVerticalBox);
	if (CapTreeRoot != nullptr)
	{
		for (auto& Ch : CapTreeRoot->Children)
		{
			TreeVBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				CreateCapGroupWidget(Ch.Get())
			];
		}
	}

	// SExpandableArea 折叠/展开时不通知外层 Detail Panel 更新高度，
	// 用独立 SScrollBox + MaxDesiredHeight 隔离动态高度，避免外层滚动条计算错乱。
	CapCategory.AddCustomRow(LOCTEXT("CapNestedTree", "Capability 目录"))
	.WholeRowContent()
	[
		SNew(SBox)
		.MaxDesiredHeight(600.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				TreeVBox
			]
		]
	];

	RefreshCategoryHeaders();
}

TSharedRef<SWidget> FNexusLinkSettingsCustomization::CreateCapGroupWidget(FCapGroupNode* Node)
{
	if (!Node || (Node->Children.Num() == 0 && Node->Caps.Num() == 0))
	{
		return SNullWidget::NullWidget;
	}

	const FString RowTitle = FNexusCapSettingsUiLayout::PathKeyToNestedTitle(Node->PathKey);
	TSharedPtr<STextBlock> CountText;
	FCapGroupNode* NodePtr = Node;

	TSharedRef<SHorizontalBox> HeaderBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, NodePtr]() -> ECheckBoxState
			{
				if (!SettingsPtr.IsValid()) return ECheckBoxState::Checked;
				int32 EnabledCount = 0;
				int32 TotalCount = 0;
				ForEachCapInSubtree(*NodePtr, [&](const FCapEntry& E)
				{
					++TotalCount;
					if (SettingsPtr->IsCapabilityEnabled(E.Name)) ++EnabledCount;
				});
				if (TotalCount == 0) return ECheckBoxState::Unchecked;
				if (EnabledCount == TotalCount) return ECheckBoxState::Checked;
				if (EnabledCount == 0) return ECheckBoxState::Unchecked;
				return ECheckBoxState::Undetermined;
			})
			.OnCheckStateChanged_Lambda([this, NodePtr](ECheckBoxState NewState)
			{
				if (!SettingsPtr.IsValid()) return;
				const bool bEnable = (NewState != ECheckBoxState::Unchecked);
				ForEachCapInSubtree(*NodePtr, [&](const FCapEntry& E)
				{
					SettingsPtr->SetCapabilityEnabled(E.Name, bEnable, /*bNotify=*/false);
				});
				SettingsPtr->NotifyCapabilitiesChanged();
				RefreshCategoryHeaders();
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text(FText::FromString(RowTitle))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SAssignNew(CountText, STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

	CategoryCountTexts.Add(NodePtr->PathKey, CountText);

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	for (auto& Ch : Node->Children)
	{
		Body->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.Padding(FMargin(14.0f, 0.0f, 0.0f, 0.0f))
			[
				CreateCapGroupWidget(Ch.Get())
			]
		];
	}

	for (const FCapEntry& E : Node->Caps)
	{
		const FString CapName = E.Name;
		const FString Desc    = E.Description;

		Body->AddSlot()
		.AutoHeight()
		.Padding(24.0f, 1.0f, 0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, CapName]() -> ECheckBoxState
				{
					return (SettingsPtr.IsValid() && SettingsPtr->IsCapabilityEnabled(CapName))
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, CapName](ECheckBoxState NewState)
				{
					if (!SettingsPtr.IsValid()) return;
					SettingsPtr->SetCapabilityEnabled(CapName, NewState == ECheckBoxState::Checked);
					RefreshCategoryHeaders();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(FText::FromString(CapName))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(Desc))
			]
		];
	}

	return SNew(SExpandableArea)
		.InitiallyCollapsed(false)
		.HeaderContent()[ HeaderBox ]
		.BodyContent()[ Body ];
}

void FNexusLinkSettingsCustomization::RefreshCategoryHeaders()
{
	if (!SettingsPtr.IsValid() || !CapTreeRoot)
	{
		return;
	}
	RefreshCapCountsRecursive(SettingsPtr.Get(), CapTreeRoot.Get(), CategoryCountTexts);
}

void FNexusLinkSettingsCustomization::SortCapGroupChildren(FCapGroupNode& Node)
{
	Node.Children.Sort([](const TUniquePtr<FCapGroupNode>& A, const TUniquePtr<FCapGroupNode>& B)
	{
		const FString& Pa = A->PathKey;
		const FString& Pb = B->PathKey;
		const bool MA = (Pa == TEXT("_misc"));
		const bool MB = (Pb == TEXT("_misc"));
		if (MA && !MB) return false;
		if (!MA && MB) return true;
		return Pa < Pb;
	});
	for (auto& C : Node.Children)
	{
		SortCapGroupChildren(*C);
	}
}

TUniquePtr<FNexusLinkSettingsCustomization::FCapGroupNode> FNexusLinkSettingsCustomization::BuildCapGroupTree(
	const TMap<FString, TArray<FCapEntry>>& InCategoryCaps)
{
	auto Root = MakeUnique<FCapGroupNode>();
	Root->PathKey.Empty();

	for (const auto& KV : InCategoryCaps)
	{
		const FString& SortKey = KV.Key;
		if (SortKey.IsEmpty())
		{
			continue;
		}
		TArray<FString> Segs;
		SortKey.ParseIntoArray(Segs, TEXT("/"), true);
		if (Segs.Num() == 0)
		{
			continue;
		}

		FCapGroupNode* Cur = Root.Get();
		FString Accum;
		for (int32 i = 0; i < Segs.Num(); ++i)
		{
			Accum = Accum.IsEmpty() ? Segs[i] : (Accum + TEXT("/") + Segs[i]);

			FCapGroupNode* Next = nullptr;
			for (auto& Ch : Cur->Children)
			{
				if (Ch->PathKey == Accum)
				{
					Next = Ch.Get();
					break;
				}
			}
			if (!Next)
			{
				auto NewNode = MakeUnique<FCapGroupNode>();
				NewNode->PathKey = Accum;
				Next = NewNode.Get();
				Cur->Children.Add(MoveTemp(NewNode));
			}
			Cur = Next;
		}
		Cur->Caps.Append(KV.Value);
	}

	SortCapGroupChildren(*Root);
	return Root;
}

void FNexusLinkSettingsCustomization::ForEachCapInSubtree(FCapGroupNode& N, TFunctionRef<void(const FCapEntry&)> Fn)
{
	for (const auto& E : N.Caps)
	{
		Fn(E);
	}
	for (auto& C : N.Children)
	{
		ForEachCapInSubtree(*C, Fn);
	}
}

void FNexusLinkSettingsCustomization::RefreshCapCountsRecursive(UNexusLinkSettings* Settings, FCapGroupNode* N,
	TMap<FString, TSharedPtr<STextBlock>>& InCategoryCountTexts)
{
	if (!N || !Settings)
	{
		return;
	}
	if (!N->PathKey.IsEmpty())
	{
		if (TSharedPtr<STextBlock>* PT = InCategoryCountTexts.Find(N->PathKey))
		{
			int32 Enabled = 0;
			int32 Total = 0;
			ForEachCapInSubtree(*N, [&](const FCapEntry& E)
			{
				++Total;
				if (Settings->IsCapabilityEnabled(E.Name))
				{
					++Enabled;
				}
			});
			if (PT->IsValid())
			{
				(*PT)->SetText(FText::FromString(
					FString::Printf(TEXT("(%d/%d)"), Enabled, Total)));
			}
		}
	}
	for (auto& C : N->Children)
	{
		RefreshCapCountsRecursive(Settings, C.Get(), InCategoryCountTexts);
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
