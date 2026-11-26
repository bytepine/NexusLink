# NexusLink MCP · MultiTool

English | [中文](InitializeInstructions.MultiTool.zh.md)

Use `tools/list` for the capabilities enabled on this host. Read the selected tool’s description, `inputSchema` and prerequisites, then call its name directly with `tools/call`. Use one call per target.

Use MCP for UE state. Search assets with `SearchAsset` (typed `AssetType`, narrow `PathFilter`, small `Limit`). Use the returned `Assets[].Path` and `RecommendedGet` / `RecommendedManage`. Request the needed `Sections` and supported `PropertyPaths` together. Filter logs first (up to 50 entries), then fetch missing data only. Check runtime prerequisites. When reproducing a problem, capturing an event or verifying an outcome, read the tool description and start any required observation before triggering the action.

When confirmation is required, supply `Reason`. Stop on `UserDenied`. Check `isError` and operation, section, compile and save errors. A write may have partially executed. After a timeout or disconnect, verify state before considering another write. Report `FeedbackHint` via `SubmitFeedback`.
