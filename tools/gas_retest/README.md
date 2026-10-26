# GAS retest / 联机复测

Python 3.10+，仅使用标准库。在插件的 `tools` 目录运行：

```sh
python -m gas_retest case.json --output results
python -m unittest gas_retest.test_runner
```

先启动服务器和两个客户端，在用例 `Endpoints` 中填写三个进程各自的 **UE 直连** `/stream` 地址。凭证来自 `TokenEnv` 环境变量或本机共享 NexusLink token 文件，不能写入用例。

`examples/PredictionRejected.json` 与 `examples/Success.json` 使用公开 NexusUnreal 的 `NexusDiagnosticGameMode`；端口是示例值，运行前须替换。用例会修改测试角色的运行时状态。

输出 `results.json` 与 `junit.xml`。退出码：0 通过，1 断言失败，2 配置/运行错误。写操作不会自动重试；清理失败独立记录；无法取得权威端证据时不宣称服务器成功。

Python 3.10+, standard library only. Run from the plugin's `tools` directory. Configure explicit direct UE endpoints for server, owning client and observer. Tokens come from an environment variable or the local shared token file. Examples require the opt-in diagnostic GameMode from the public NexusUnreal sample. They modify only the selected test character's runtime state. Writes are never retried; missing authority evidence remains unconfirmed.

完整格式与限制 / Full format and limitations: [GAS diagnostics and replay](https://github.com/bytepine/NexusLink/blob/master/docs/gas-retest.md).
