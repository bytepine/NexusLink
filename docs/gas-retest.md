# GAS 技能激活诊断

中文 · [English](gas-retest.en.md)

[文档导航](README.md) · [项目首页](../README.md)

执行器、HTML 布局与基线代码已提取到 `tools/retest`，`gas_retest` 的 CLI、Python 导入路径和旧报告继续可用。新报告标注 `Domain=GAS`；缺少 Domain 的历史报告按 GAS 解释，不能与 BT 报告比较。


只读快照、限量事件捕获，以及仅依赖 Python 标准库的复测工具。

命令行工具随源码提供，插件安装包不附带；先获取对应版本源码并进入 `tools/` 目录，见[安装说明](usage-guide.md#21-安装)。

## 运行时工具

GetRuntimeActorAbilitySystem 保留默认的 Abilities、Effects、Attributes 分段。显式诊断分段要求 ActorName：

已有 All 查询无需技能选择器或采集仍可使用；可选激活分段在提供相应选择器或 CaptureId 后才返回。

- Context：进程与 World 身份、Owner/Avatar、PlayerId、权限和本地控制状态。
- Tags：已拥有标签和阻止技能的标签。
- ActivationDiagnostics：SpecHandle（Abilities 返回的字符串）与 AbilityPath（完整类路径）恰选其一。报告技能授予可见性、缺失/阻止标签及配置的冷却 GE 剩余时间；不调用自定义激活检查。消耗与自定义规则在实际复现前仍未知。
- ActivationEvents：传 CaptureId，返回 Events、EventTypes、FailureTags、Dropped、LatestSequence。

InteractRuntimeActorAbilitySystem 新增：

- TryActivateGrantedAbility：明确 ActorName，并恰选上述一个技能标识；不会授予技能。bRequestAccepted 仅表示本地请求，权威端结果应检查服务器采集。原 ActivateAbility 仍先授予再激活。
- StartActivationCapture：明确 ActorName，返回 CaptureId。
- StopActivationCapture：传匹配的 ActorName 与 CaptureId，释放订阅和事件。

采集 120 秒后过期，保留 256 个事件并报告丢弃数量；每进程最多同时 16 个采集。World 销毁或 MCP 关闭会移除采集，失效 ASC 会被清理。每个端点都须在尝试前开始采集。Activated、Committed、Failed、Ended 是不同事件；预测不是服务器确认。服务器事件缺失表示未确认，不是已证明拒绝。非实例化或身份不明确时显式标记 bIdentityKnown=false，不能归因于某次尝试。

诊断不授予或激活技能，也不调用项目自定义检查。冷却只按配置的 GE 类匹配，不能证明自定义冷却逻辑允许激活；消耗、自定义规则应结合真实失败事件。其他玩家的技能可能只对所属客户端可见，未看到技能不等于服务器未授予。采集溢出、超时、身份不明确时，结果应标记为未确认。

## GenericActivation

通用接入不依赖示例技能、属性、标签或 GameMode。先通过发现工具确认端点、Actor 名和完整技能类路径；下面的占位值须替换为真实值，保存为 `project.json`：

```json
{
  "Name": "Project activation",
  "Endpoints": {"Authority": {"Url": "http://127.0.0.1:45000/stream", "bAuthority": true}},
  "Actors": {"Target": {"Bindings": {"Authority": "ACTOR_FROM_DISCOVERY"}}},
  "AbilityPath": "/Script/YOUR_MODULE.YOUR_ABILITY",
  "ActivationEndpoint": "Authority",
  "AuthorityEndpoint": "Authority",
  "ExpectedEvent": "Committed",
  "FailureTagHints": {}
}
```

```sh
python -m gas_retest case.json --template GenericActivation --config project.json
python -m gas_retest case.json --preflight --output preflight
python -m gas_retest case.json --output results
```

`ExpectedEvent` 可为 `Committed` 或 `Failed`；自定义 `FailureTagHints` 为实际失败标签到项目说明的映射，只在捕获到相应 `FailureTags` 时显示。生成过程完全离线、拒绝覆盖、不会授予技能或改变运行条件。修改条件由项目显式配置生成用例的 `Setup` / `Teardown`，也可在两次运行之间人工恢复；保留相同用例定义进行失败/恢复比较。配置只接受声明字段和显式端点，不扫描后自动选择。

`Actors` 首版包含一个逻辑目标。`Bindings` 在每个端点分别指定 Actor，支持没有 PlayerState 的普通 Actor，以及 ASC 在 Pawn / PlayerState 上的工程。映射是用户声明；报告只确认各端本地身份，不自动证明跨端 Actor 相同。也可用原有 `ClassFilter` + `OwnerEndpoint`（或显式 `PlayerId`）代替 `Bindings` 进行 PlayerId 关联。每个端点需独立进程，显式配置 `bAuthority`。发起端必须具有 Authority 或本地控制权；发起端、权威端各需恰好一个匹配技能，策略限 InstancedPerActor。观察端不强制技能可见。

Context 新增 `ActorId`、`AscId`、`OwnerId`、`AvatarId`、`WorldIdentity`、Actor/Owner/Avatar 类路径。对象身份由本地槽位与序列号组成，仅在本进程内解释。通用模板要求 Owner/Avatar 初始化完成及身份字段可用；每一步前检查所有绑定端，进程/World/对象被替换立即停止，包括清理写入。旧用例仍可读取旧版 Context，存在新身份字段时同样检查变化。

采集从每次尝试前开始，步骤串行；只读诊断不调用项目 `CanActivateAbility`。只记录快照与真实激活事件，缺少服务器事件、身份未知、溢出或观察到其他技能时显示未确认。模板及报告完全独立于 `NexusDiagnosticGameMode`。

GenericActivation 模板接受显式端点/Actor 映射、技能类、发起/权威端点、预期服务器事件及可选失败标签说明，支持普通 Actor、Pawn、PlayerState 的组件 ASC。绑定声明不自动证明跨进程身份。每次尝试独立采集，仅支持 InstancedPerActor；已有模板、CLI 和报告继续可用。

## 复测

在对应版本源码仓库的 tools 目录执行；插件安装包不包含这些工具：

```sh
python -m gas_retest case.json --output results
python -m gas_retest case.json --preflight --output preflight-results
python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare
```

### 首次接入

编译公开 NexusUnreal 示例后，在该工程运行 python Script/gas_network_retest.py --ue-root <UE-directory> --quickstart。它启动一服两客户端，只读检查绑定/技能，复现预测拒绝，恢复条件并验证成功激活。打开 Saved/GasRetest/index.html 比较报告。不带 --quickstart 的既有方式仍保留全部八个场景及配置的重复次数。

已有三个运行中的诊断进程时，可从源码仓库 `tools/` 目录生成端口已填写的用例；URL 必须是发现后确认的 UE 直连端点。模板生成不连接 UE，也不覆盖现有文件：

```sh
python -m gas_retest rejected.json --template PredictionRejected --server http://127.0.0.1:45010/stream --owner http://127.0.0.1:45011/stream --observer http://127.0.0.1:45012/stream
python -m gas_retest rejected.json --preflight --output preflight-results
python -m gas_retest rejected.json --output rejected-results
```

上述端点必须替换成真实发现的地址。--template Success 创建修复后的用例模板。模板依赖 NexusDiagnosticGameMode，不是通用生产技能定义。

--preflight 初始化 MCP，只读 Actor 身份、所属关系、进程/World 与显式 RequiredAbilities，不执行 Setup、Steps、Teardown。没有 Actors 时只能检查 MCP 初始化，角色/World 身份仍未验证。正式复测在任何场景写入前做相同检查；绑定或必需技能失败时也跳过清理，因为场景尚未开始。

首版仅支持明确的 **UE 直连** /stream 端点，每进程只承载一个游玩 World。凭证取端点 TokenEnv 环境变量（默认 NEXUS_MCP_TOKEN）或本机共享 NexusLink token 文件；禁止把 token 写入用例 JSON。

联机用例服务器设 bAuthority=true、客户端 false。Actor 绑定检查此预期并拒绝重复进程，避免把客户端事件误当成服务器证据。

最小只读示例，端点和 Actor 名须替换成已发现值：

```json
{
  "SchemaVersion": 1,
  "Name": "GAS context",
  "Endpoints": {"Server": {"Url": "http://127.0.0.1:45000/stream"}},
  "Steps": [{
    "Id": "context", "Type": "Call", "Endpoint": "Server",
    "Capability": "GetRuntimeActorAbilitySystem",
    "Arguments": {"ActorName": "ACTOR_FROM_DISCOVERY", "Sections": ["Context"]}
  }]
}
```

用例可选 Actors、Setup、Teardown。Actor 绑定用 ClassFilter 和 OwnerEndpoint 找到所属客户端唯一的本地受控 Pawn，再以复制的 PlayerId 匹配其他端点；也可显式 PlayerId。缺失或多义匹配在写入前失败。用 {"Ref":"/Actors/Hero/Server"} 引用端点 Actor，实际名称在各进程分别解析。

RequiredAbilities 可选数组形如 {"Actor":"Hero","Endpoint":"Server","AbilityPath":"/Script/Nexus.NexusDiagnosticAbility"}。每项须在绑定 Actor/端点恰好匹配一个已授予的 InstancedPerActor spec；只读 Abilities，不授予或激活。仅列应当存在的前提，负向用例故意未授予的技能不能列入；不要求观察端看得到其他玩家技能。

步骤：

- Call：Id、Endpoint、Capability、Arguments；ExpectError=true 允许预期 MCP 工具错误，并保留结构化数据。
- Assert：Actual、Op（Equal、Contains、Exists、Approx）、Value；数值 Approx 可指定 Tolerance，默认 0.0001。
- Await：相同调用字段，并附 Check 的 Path（响应内 JSON Pointer）、Op、Value。仅允许 GetRuntimeActorAbilitySystem、GetRuntimeActorProperty、ListRuntimeActors。默认超时 10 秒、间隔 0.1 秒；可配置的最大超时为 120 秒。

完整 {"Ref":"/Steps/context/Payload/Context/ProcessId"} 对象解析前序响应且保留类型，不执行表达式、循环或脚本。断言前恢复列表 *_defaults。报告保留各端点、原始结构化证据和断言结果。

写入不重试。进程/World 变化中止复测。失败后尽力执行 Teardown，清理失败单独报告。运行时修改须显式提供清理；执行器不回滚任意玩法副作用。即使无法连接进程完成清理，采集仍会到期。

输出离线 report.html、results.json、junit.xml 和终端摘要。退出码 0 通过，1 断言失败，2 配置/传输错误或证据不完整。Await 超时保留末次观测，权威结果标为未确认。

HTML 报告展示端点角色、首个失败步骤、激活快照、按端去重事件、后续检查和可展开原始证据。用例通过/失败与实际玩法结果分开，预期拒绝也可通过。缺失权威事件、事件丢失、身份未知或冲突激活均保持未确认。事件按每次采集的观测顺序保留，跨进程时钟不能证明因果。报告离线可用，无外部脚本或资源。

### 保存基线与前后对比

两项操作均离线读取已记录结果，不初始化 MCP、不激活技能、不重新运行步骤：

```sh
python -m gas_retest previous/results.json --save-baseline gas-retest-results/baselines/Success.json
python -m gas_retest current/results.json --compare gas-retest-results/baselines/Success.json --output gas-retest-results/comparison-01
```

每个用例单独保存基线，例如 Success、PredictionRejected。基线保留完整报告和 UTC 保存时间，要求复测通过且无丢失/身份未知事件，拒绝覆盖已有文件。预期拒绝用例通过可作为基线，但不宣称玩法成功。预检不能保存为复测基线；没有权威激活证据的只读用例保持未确认。

仅比较同一用例；名称、模式、端点别名及配置权威角色须一致。新报告用 CaseSignature 签名覆盖用例步骤、断言和预期角色，排除部署 URL、token 变量名。签名不同会拒绝，削弱断言不能冒充恢复。无签名旧报告仍可读取，但醒目提示无法核对原始断言定义。

comparison.html 和 comparison.json 区分新增失败、恢复、新增检查和缺失/未执行检查，含设置与清理；缺失不算成功。报告另比权威结论、观测角色、事件顺序/数量、采集边界、技能路径、失败标签。进程/World ID、URL、耗时、采集 ID、SpecHandle、PredictionKey 不参与语义事件比较，原值仍保留。重复采集快照去重；单纯事件变化仅为观测，不证明回归或修复，不完整/多义证据保持未确认。

比较目录须不存在。before/ 与 after/ 各含独立 HTML、JSON、JUnit，移动目录后仍可离线浏览，不覆盖原输入或已有基线。查看失败后恢复可用 --compare failed/results.json，不要把失败结果保存为基线。

对比退出码：0 当前用例通过且无缺失检查；1 当前失败/错误或缺失检查；2 输入无效/不兼容或输出错误。它们描述比较操作，原始复测退出码仍在两侧报告保留。

这两个模式的首个参数为已保存的结果文件，而非用例定义；与 `--preflight` / `--template` 互斥。固定用例的修改前、修改后结果分别对比；不要用预期不同的 `PredictionRejected` 和 `Success` 相互替代。基线和对比记录保留完整原始证据，步骤状态与实际技能结果分别展示。

用例中的 `Setup` / `Steps` / `Teardown` 顺序执行；不自动重试写操作。JSON Pointer 引用保留原始类型；遇到响应默认值压缩会先还原再断言。执行前后检查进程与 World，换图或重启后不继续写入。失败和清理失败分别进入报告。

## 联机示例

公开 [NexusUnreal 示例工程](https://github.com/bytepine/NexusUnreal) 提供可选 NexusDiagnosticGameMode 与 Script/gas_network_retest.py，启动独立服务器/所属客户端/观察端进程，并保存技能未授予、必需/阻止标签、消耗、冷却、预测拒绝、LocalPredicted/ServerOnly 成功激活的可运行用例和报告；不修改默认 GameMode。编译运行见示例测试指南。

在 NexusUnreal 手动运行 python Script/gas_regression.py --ue-root <UE-directory> 开始固定用例回归。启动 UE 前核对两份经审阅签名的基线，再执行 PredictionRejected、Success，分别比较并把完整 HTML/JSON/JUnit/日志归档到 reports.zip。基线缺失/变更会失败而非重建，未执行用例与子进程错误不能通过门禁。GitHub 工作流只在 GitHub 托管 Ubuntu 执行工具检查；UE 编译和联机回归需用户明确命令，不调度本地 runner。手动执行和基线复核见[示例测试指南](https://github.com/bytepine/NexusUnreal/blob/master/Tests/README.md)。

首版验证 InstancedPerActor 技能。同一 ASC/技能的尝试须串行，每次新建采集。无关并发玩法引发类/handle 或 PredictionKey 冲突时按多义处理；跨机器墙钟不证明因果。
