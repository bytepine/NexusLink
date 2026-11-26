# NexusLink MCP · SearchMode

English | [中文](InitializeInstructions.SearchMode.zh.md)

Call the meta-tools directly. For an unknown capability, use `SearchCapabilities` with 1–2 specific keywords. For its parameters and prerequisites, look up its exact `CapabilityName`, adding `Action` when known. Invoke `CallCapability` with `Capability` and a nested `Arguments` object. Batch independent targets with `Calls[]`.

Use MCP for UE state. Search assets with `SearchAsset` (typed `AssetType`, narrow `PathFilter`, small `Limit`). Use the returned `Assets[].Path` and `RecommendedGet` / `RecommendedManage`. Request the needed `Sections` and supported `PropertyPaths` together. Filter logs first (up to 50 entries), then fetch missing data only. Check runtime prerequisites. When reproducing a problem, capturing an event or verifying an outcome, read the tool description and start any required observation before triggering the action.

When confirmation is required, supply `Reason`. Stop on `UserDenied`. Check `isError` and operation, section, compile and save errors. A write may have partially executed. After a timeout or disconnect, verify state before considering another write. Report `FeedbackHint` via `SubmitFeedback`.
