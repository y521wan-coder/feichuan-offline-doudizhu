# 在线客户端边界（2.3）

`FeichuanOnlineDoudizhu` 是独立 Windows 进程。主程序只启动它并在其退出后恢复模式选择页。这个进程使用 Qt WebSockets 连接固定的 `wss://play.327802521.xyz/ws`，不创建 `GameEngine` 或 `GameState`，只保存服务端发给当前账号的公开房间视图、本家手牌和房内最近 20 条纯文本消息。选牌牌型由现有 `PatternAnalyzer` 本地预览，最终动作由服务端判定。

在线数据写入 Windows `AppLocalDataLocation/online`，与旧单机、云 AI 数据分开。只有勾选“记住登录”才用 Windows DPAPI 保存会话令牌；主动退出登录立即撤销服务器会话并删除本地密文。F5 的表情音效管理可分别给 1～8 号表情导入自己制作的 PCM WAV 文件，文件复制到在线数据目录；没有导入时仍可发送表情名称。此处不附带第三方表情音频。原牌局音效继续通过 `SoundService` 播放。

“复制诊断信息”只输出应用版本、连接状态、当前页面、房间与轮次修订号、错误码、读屏路径，并可附加服务端允许的健康摘要。不得把密码、令牌、手牌、房内文字正文或隐藏牌加入诊断。界面朗读沿用 `AccessibilityService`；房内新消息顺序排队朗读。

构建目标：`tools/build.ps1 -Preset release-x64 -Build`。Windows 安装包需携带 `Qt6WebSockets.dll`、`Qt6Network.dll`、`tls/qschannelbackend.dll` 和其它既有 Qt Widgets 运行库。发布前应在真实服务上完整走注册、登录、二／三／四人大厅、入房、F1～F5、断线重连和退出流程。
