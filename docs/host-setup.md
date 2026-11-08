# Windows 电脑端

## 依赖与配置

推荐 Python 3.11、Node.js LTS。先在仓库根目录建立 `.venv` 并安装 `host/requirements.txt`。

准备两项本机信息：

1. 电脑在开发板可访问网络上的 IPv4 地址。
2. 一条本机 Codex 聊天的 UUID，作为桌面桥的初始上下文。它只写入本地配置，不提交到仓库。桌面应用若不能提供相应本机工具接口，本适配器无法工作。

```powershell
# 替换示例地址和聊天 UUID，使用自己的值。
.\.venv\Scripts\python.exe scripts/setup.py --pc-host 192.0.2.10 --thread 11111111-1111-4111-8111-111111111111
```

`192.0.2.10` 是文档示例，不能直接作为真实电脑地址。配置程序会生成新随机 token、自签配对证书和固件头文件。证书私钥仅留在电脑；开发板只嵌入该证书的公钥证书与自己的配对 token，并校验连接。首次生成后不自动覆盖旧配对。

默认 ASR 为 faster-whisper `small`、CPU int8、中文/VAD。第一次转写可能下载模型。可用 `--model <本地模型目录> --local-model-only` 完全使用已有模型。录音和转写保存在本机忽略目录中，不会自动上传 GitHub。

## 构建控制助手

在 PowerShell 中运行：

```powershell
cd host/codex_sync
.\build_task_control.ps1
```

生成的 `TaskControl.exe` 不纳入源码仓库。它调用 Windows UI Automation 定位当前 Codex 窗口，用于任务停止和原生问题选项的适配。请在正常交互桌面登录状态下运行。

## 启动

在仓库根目录运行 `scripts/start-host.ps1`，或分别启动两个进程：

```powershell
# 窗口一
.\.venv\Scripts\python.exe host/codex_sync/supervisor.py

# 窗口二
node host/monitor/server.cjs
```

电脑桥本地预览端口为8790；开发板通过8791的配对HTTPS连接。监控进程只监听本机127.0.0.1:8788，开发板经已鉴权的HTTPS桥读取监控和提交控制请求。不要把这些端口映射到公网。

保持 Codex 桌面应用运行，并确保它的 `codex-app-tools` 本机插件与命名管道可用。适配器不会安装或修改 Codex 的内部工具，不提供登录凭据或模型 Key。

Windows 防火墙如阻止8791入站，请由电脑所有者按自己的网络策略配置。启动脚本不会静默创建防火墙规则或登录自启项。

电脑地址或证书变化需要重新配对并重新构建板端固件。Mac版、自动发现与一键安装是后续工作，不在首版兼容承诺内。
