// Copyright byteyang. All Rights Reserved.

#include "NexusInstructionRegistry.h"

FNexusInstructionRegistry& FNexusInstructionRegistry::Get()
{
	static FNexusInstructionRegistry Instance;
	return Instance;
}

void FNexusInstructionRegistry::Register(
	const FString& SourceName, const FString& SearchMode, const FString& MultiTool)
{
	if (SourceName.IsEmpty())
	{
		return;
	}
	FFragment Frag;
	Frag.SourceName = SourceName;
	Frag.SearchMode = SearchMode;
	Frag.MultiTool = MultiTool;
	Frag.SearchMode.TrimStartAndEndInline();
	Frag.MultiTool.TrimStartAndEndInline();
	if (Frag.SearchMode.IsEmpty() && Frag.MultiTool.IsEmpty())
	{
		return;
	}

	Unregister(SourceName);
	Fragments.Add(MoveTemp(Frag));
	Fragments.Sort([](const FFragment& A, const FFragment& B)
	{
		return A.SourceName < B.SourceName;
	});
}

void FNexusInstructionRegistry::Unregister(const FString& SourceName)
{
	Fragments.RemoveAll([&SourceName](const FFragment& Frag)
	{
		return Frag.SourceName == SourceName;
	});
}

FString FNexusInstructionRegistry::Append(const FString& Base, bool bMultiTool) const
{
	FString Out = Base;
	for (const FFragment& Frag : Fragments)
	{
		const FString& Text = bMultiTool ? Frag.MultiTool : Frag.SearchMode;
		if (Text.IsEmpty())
		{
			continue;
		}
		if (!Out.IsEmpty())
		{
			Out += TEXT("\n\n");
		}
		Out += Text;
	}
	return Out;
}
