# GAS retest / 联机复测

Python 3.10+，仅使用标准库。在插件的 `tools` 目录运行：

```sh
python -m gas_retest case.json --output results
python -m gas_retest case.json --preflight --output preflight-results
python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare
```

先启动服务器和两个客户端，在用例 `Endpoints` 中填写三个进程各自的 **UE 直连** `/stream` 地址。凭证来自 `TokenEnv` 环境变量或本机共享 NexusLink token 文件，不能写入用例。

`examples/PredictionRejected.json` 与 `examples/Success.json` 使用公开 NexusUnreal 的 `NexusDiagnosticGameMode`；端口是示例值，运行前须替换。用例会修改测试角色的运行时状态。

已有运行中的三个诊断进程时，可用 `--template PredictionRejected` 或 `--template Success`，配合 `--server URL --owner URL --observer URL` 生成新用例文件；地址须事先确认，不会自动扫描选中实例，也不覆盖文件。`--preflight` 只读验证角色绑定、进程/World 和显式 `RequiredAbilities`，不执行场景或清理步骤。完整命令见下方文档。

公开 NexusUnreal 示例工程中可直接运行 `python Script/gas_network_retest.py --ue-root <UE-directory> --quickstart`：启动一服两客户端，检查接入、复现预测拒绝、恢复后验证成功，生成 `Saved/GasRetest/index.html` 汇总。

输出离线 `report.html`、`results.json` 与 `junit.xml`。报告区分用例状态与技能实际结果，按端展示事件、失败步骤及原始证据。退出码：0 通过，1 断言失败，2 配置/运行错误。写操作不会自动重试；清理失败独立记录；无法取得权威端证据时不宣称服务器成功。

保存基线与前后对比均为离线操作，输入是已生成的 `results.json`，不会连接 UE 或重新执行用例：

```sh
python -m gas_retest previous/results.json --save-baseline gas-retest-results/baselines/Success.json
python -m gas_retest current/results.json --compare gas-retest-results/baselines/Success.json --output gas-retest-results/comparison-01
```

基线要求复测通过、没有丢失或身份未知的事件，拒绝预检结果和覆盖已有文件。预期拒绝用例通过时也可保存，技能结果单独保留。对比输出目录必须不存在，生成 `comparison.html`、`comparison.json` 和 `before/`、`after/` 完整报告与原始证据。

对比区分新增失败、恢复、新增及缺失步骤，并展示各端事件、失败标签与角色变化。用例名称、模式、端点角色及新版用例签名须一致，修改断言不能被当成恢复；签名忽略部署地址和 token 环境变量名。旧报告可对比，但会明确提示无法核对完整用例定义。事件比较忽略进程号、端口、时间、采集 ID、SpecHandle 与 PredictionKey，保留采集边界、顺序、次数、技能和失败标签。

也可用 `--compare previous/results.json` 比较两份普通结果，包括失败后恢复。对比退出码：0 本次用例通过且没有缺失步骤；1 本次失败/错误或有缺失步骤；2 输入不合法、不兼容或文件写入失败。事件变化本身不判定回归，未确认结果不会被当成服务器成功。

自动回归入口位于公开 NexusUnreal 示例工程：`python Script/gas_regression.py --ue-root <UE-directory>`。它先核对受版本控制的基线及用例签名，再运行固定的 `PredictionRejected` / `Success`，生成汇总 HTML / JSON / JUnit、逐项对比及包含日志的 `reports.zip`；失败和未执行用例同样进入归档与门禁，不自动更新基线。GitHub CI 的工具检查无需 UE；真实联机任务要求配置 Windows UE 自托管 Runner，详见示例工程测试指南。

Python 3.10+, standard library only. Run from the plugin's `tools` directory. Configure explicit direct UE endpoints for server, owning client and observer. Tokens come from an environment variable or the local shared token file. Examples require the opt-in diagnostic GameMode from the public NexusUnreal sample. They modify only the selected test character's runtime state. Writes are never retried; missing authority evidence remains unconfirmed.

完整格式与限制 / Full format and limitations: [GAS diagnostics and replay](https://github.com/bytepine/NexusLink/blob/master/docs/gas-retest.md).
