// Copyright byteyang. All Rights Reserved.

#pragma once

#include "StateTreeEditorData.h"
#include "StateTreeState.h"

// 编辑器数据可含空子状态或重复引用；按对象去重，GUID 损坏也不影响其子树。
inline void VisitNexusStateTreeStates(const UStateTreeEditorData& Data, TFunctionRef<EStateTreeVisitor(UStateTreeState&)> Visitor)
{
	TSet<const UStateTreeState*> Visited;
	auto Pending = Data.SubTrees;
	while (!Pending.IsEmpty())
	{
		UStateTreeState* State = Pending.Pop();
		if (!State || Visited.Contains(State)) continue;
		Visited.Add(State);
		if (Visitor(*State) == EStateTreeVisitor::Break) return;
		Pending.Append(State->Children);
	}
}
