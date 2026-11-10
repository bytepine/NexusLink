# 功能缺口首批补充

本批分别交付九个方向的首批功能。能力取决于 UE 版本、宿主与已启用插件；以下明确各项范围。

## 动作级发现

`GetProjectToolsets(Sections=["Actions"], Capability="ControlEditorAnimation", Action="AddFloatKey")` 返回 bSupported、Backend、MinimumVersion、Prerequisites、Reason 与 InputSchema。支持 Animation、Niagara、PCG 六个编辑器读写入口；省略筛选可分页列举。所有版本均使用独立实现；发现结果基于当前引擎 API 和领域插件，不读取官方 Registry，也不自动启用插件。

透传入口的 Schema 描述 Parameters 内部参数；扁平入口返回完整客户端输入契约。路径来自实际发现。接口可用仍需满足目标对象、属性类型等约束，不能据此推断所有原生参数类型在兼容层可用。

## 结构预览与回读

GetAssetEditPreview 在隔离临时副本上顺序规划：

- Blueprint：RemoveNode、Disconnect、DisconnectAll。
- UserWidget：具名 Add、Remove、Move；Add 必须指定 WidgetName，根已存在时指定父面板。
- PCGGraph：AddNode、RemoveNode、AddEdge、RemoveEdge。

返回 StructureHash、结构记录的 Added / Removed 与逐项 bValid。Hash 保留中文等 Unicode 名称；Blueprint 副本包含接口实现图，UE 5.8 控件删除计划也包含引擎删除的变量引用节点。首个失败后停止；副本中新节点 ID 仅供预览，实际写入后重新读取。计划验证节点、边与层级，不编译、保存或验证图的业务结果。不要混合标量与结构预览。

对应 Manage 接受 ExpectedStructureHash，拒绝预览后的结构变化；bVerifyStructure=true 逐操作构造结构计划并比较实际结构，返回 bStructureVerified。不一致阻止本次自动保存，已执行的内存修改按既有部分成功规则保留；不承诺磁盘整体回滚。标量 ExpectedValue / bVerify 保持原规则。

## 首次导入

ImportAsset 接受单个本地绝对 SourceFile、DestinationPath（/Game/ 下目录）、AssetType 与可选 AssetName：

- Texture：PNG/JPEG/BMP/TGA/EXR/HDR，bSRGB 控制颜色空间。
- SoundWave：WAV。
- StaticMesh：FBX，合并文件内网格，bGenerateCollision 控制自动碰撞。
- SkeletalMesh / AnimSequence：FBX，必须指定已有 SkeletonPath。SkeletalMesh 首批只接受一个网格节点并固定目标完整名称；含多个网格节点（包括多 LOD、同骨架分片）的文件须先拆分，避免工厂忽略指定名称造成覆盖。AnimSequence 支持多 take。

ConflictPolicy 默认 Fail，也可显式选 Unique 或 Replace。动画 FBX 在写入前检查单 take 及多 take 的潜在目标名；任一冲突时 Fail 拒绝整个导入，Unique 为整组 take 选择空闲基础名，只有 Replace 允许覆盖。take 名经合法化后重名会拒绝导入。返回全部实际创建或修改的动画路径，包括被工厂主返回值遗漏的 take。FBX 不导入材质/纹理、不创建 PhysicsAsset，网格导入不附带动画。返回产物路径、类型及读回结果，不自动保存。随后调用 SaveAsset；失败检查 GetOutputLog。已有资产更新可用 ReimportAsset。

## 多人 PIE 配置

ControlPie Start 新增 NetMode=Standalone|ListenServer|Client、NumberOfClients=1..8 与 bRunUnderOneProcess。默认为单人，Standalone/Simulate 限一个玩家。等待 Status 的实际 PlayWorlds 就绪，每项报告 PieInstance、WorldPath 与真实 NetMode。独立进程只报告本实例世界，须发现并连接相应端点。此批不含输入录制/重放及 ListView 条目交互。

## 低版本 PCG 执行与输出

兼容后端的 ControlEditorPCG 支持 Spawn 与异步 Execute。通过所属 GetEditorOperation 等待，拒绝对正在生成的组件重复启动，支持取消及 120 秒超时。生成结束不等于业务成功，检查日志与输出数据。

GetEditorPCG NodeData 支持单执行栈的点数据（Transform、Density、Seed），Offset/Limit 分页最多 1000 项。Execute 自动启用检查，无数据时重新执行。非点数据、多循环栈、任意 Metadata 属性返回明确错误。深层 Niagara 动态输入和 Rig 烘焙按当前引擎公开 API 及动作发现结果裁剪。

## 骨骼姿态采样

GetAssetAnimSequence 在 UE 5.2+ 可传 BoneNames（1–32 个实际骨架名称）与 SampleTimes（1–128 个片段内秒数），返回本地空间 BoneSamples：Translation、XYZW 顺序的 RotationQuaternion 与 Scale。未传采样参数时保持元数据查询。此批不含骨骼轨道写入、裁剪或重采样。

## 所属任务历史与取消

GetEditorOperation 省略 OperationId 时列举当前 MCP 会话最近摘要，支持 bRunningOnly 与 Limit（1–128）。摘要含 bHasResult，完整结果继续按 ID 查询。ControlEditorOperation 只向暴露停止回调的自有运行任务请求取消；终态、不支持取消或其他会话任务拒绝。取消等待仍只取消等待。

历史在内存中有界保留，编辑器重启清空，不支持跨身份恢复。VSCode、Rider、Desktop 使用相同会话路由，查询/取消保持 Owner，不缓存任务响应、不重放写入。此批不新增任务面板。

## 性能预算

离线 `trace_compare compare` 新增 `--budget budget.json`：

```json
{
  "MinSamples": 3,
  "Frames": {"P95Seconds": {"MaxAfterSeconds": 0.02, "MaxIncreasePercent": 5}},
  "Cpu": {"GameThread": {"MaxIncreaseSeconds": 0.01}}
}
```

帧指标支持 MedianSeconds/P95Seconds/MaxSeconds，CPU 使用准确计时器名称。规则为 MaxAfterSeconds、MaxIncreaseSeconds、MaxIncreasePercent。退出码：0 通过（或无预算的报告模式）、1 超预算、2 输入/执行错误、3 证据不足。条件不同、样本不足、计时器缺失/重名、零百分比基线均不能通过预算。JSON/HTML 保存明细。此批不增加 Trace 录制、GPU、内存或网络分析。

## 资产源控预检

GetAssetSourceControl 查询 1–128 个已保存包，报告文件路径、只读属性、Provider 可用性及缓存状态。可用状态包括受控、检出、他人检出、可检出、冲突、当前版本及新增。bStatusKnown=false 表示不能推断安全写入。显式 bRefresh=true 请求 Provider 刷新，失败按错误返回。此批只读，不执行 checkout/add/submit，不生成二进制语义差异。
