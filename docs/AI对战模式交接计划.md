# AI 对战模式新会话交接说明

> 最新现场与四人规则修订请先读 `docs/2026-09-25-四人规则修订交接.md`。本文件以下内容是历史交接快照，其中的强度档位、等待时间、安装哈希、进程和测试状态有过期内容。

更新时间：2026-09-24（玩家称呼改数字、电脑座位统一云模型后）

最新规则：AI 对战除玩家一（真人）以外的电脑座位全部使用云模型，并共用同一份认证、模型、强度和超时；选好一个模型，全部电脑座位都用这一个，不再按座位选择本地机器人或不同模型。`AiBattleSettings::normalize()` 负责把旧 `settings.json`（例如玩家三还是本地机器人、玩家四用别的模型）统一成这份共用配置；AI 对战设置页只剩“人数”和单一分组“云模型（2及之后的电脑座位共用这一个）”。单机 2.1、AI 服务、规则、计分、持久化格式与快捷键均未改动。

玩家称呼：默认一律用数字 1/2/3/4，不朗读或显示“玩家一～四”；旧 `settings.json` 里等于旧默认的名称会在读取设置时迁移为数字，用户自定义名称保留。云回合既不朗读也不显示思考状态（AI 直接思考后出牌；故障暂停弹窗仍保留认证、模型与安全摘要）。

最新现场：用户首次真实 UI 对战在玩家二 `deepseek-v4-flash`、深入档、15 秒时于 14,996 ms 超时。安装文件、AI 服务和 TLS 插件无缺失；原因是该座位使用已知不适合当前 DeepSeek 接口的思考档。已通过安装版界面把三个云座位全部改为快速档；最小真实 UI 试玩连续 4 次云决策全部成功并由本地引擎接受，延迟 612～1,463 ms，之后为节省额度主动退出，未完成整局。游戏与 AI 服务现均已结束。

本轮新增每局 `ai_battle/logs/ai-game-*.jsonl` 脱敏诊断，记录牌局/请求生命周期、安全错误码、动作编号、公开计数、延迟与令牌数；绝不记录凭据、地址、提示词、原始响应、暗牌、动作目录或牌面签名。安装版还实机发现并修复 AI 对战 F5 错开单机设置的问题。为防止复发，已对实测不兼容的 `deepseek-v4-flash` 均衡/深入档增加保存/开战前明确拦截，要求改成快速，不静默降档或重试。针对性 `test_ai`、`test_ai_battle_persistence` 与 `test_main_window_shortcuts` 3/3 通过、96.70 秒；未重跑完整 CTest 或长测。当前安装/构建主程序 SHA-256 为 `E2ECE1A22E584897A3DEA39298D9ED185D73D550D085A1BAFC8C374E4454535A`、1,070,080 字节、版本 2.1.0.0；AI 服务未改。争渡双语音听感仍未获用户确认，固定战略场景仍需另行额度授权。

最新修复：用户使用争渡时发现启动后的模式页双语音。原因是按钮自然焦点朗读与模式页显式调用 `AccessibilityService::announce` 叠加；现仅保留焦点朗读，可访问名称、位置和说明不变，原 2.1 牌局朗读未改。完整 CTest 21/21、182.51 秒；安装版主程序已再次替换，SHA-256 `BD86DA42CAFE5CD4442963D9A3EA56D824899CA21DC1D6CB2EEFE42D61EE78DE`，旧试用内核备份在 `build\release-x64\install-trial-backup-20260924-voicefix\`。安装目录仍 197 个文件，待用户复测真实听感。

用户最新测试要求：小改动只跑受影响的针对性测试，已通过且未受影响的完整 CTest、稳定性长测和假模型压力证据直接沿用；有具体回归风险时再扩大范围。本次双语音修复的完整 CTest 已执行，后续不因此机械重跑。

本文件是“2.1 冻结基线 + AI 对战模式”开发的中途交接。新会话先完整阅读 `AGENTS.md`、根目录 `继续开发说明.txt`、本文和 `docs\AI对战模式规格.md`，再看 `docs\测试与验收.md`、`docs\开发日志.md`。下述快照取自 2026-09-24 本轮结束前；下一会话必须按实机重新核对会变化的进程和用户数据状态。

## 新会话先看：当前现场与用户授权

1. 仓库固定在 `C:\Feichuan-Doudizhu`，当前 HEAD `14122b7`（2.1 发布后的文档提交）；冻结的 2.1 正式产品代码提交为 `eafc5f07e9e373a78581ec8b061651f36f74b3e5`。2026-09-24 快照为 **15 个已修改的跟踪文件及大量新增文件，全部未提交**；包括用户亲自按方案 A 修订的 `docs\测试与验收.md`。本轮继续在这些未提交改动上增加逐局日志与 F5 修复。不要 reset、checkout、clean、提交、推送或打标签。先执行 `git -c core.quotepath=false status --short`、`git log -3 --oneline`、`git diff --stat` 核对。
2. AI 对战已开发、已安装为本机试用内核。本轮完成最小真实 UI 验证后已确认退出安装版，游戏和 AI 服务当前均不运行。快速档 4 次决策成功只证明首次超时路径已恢复，**尚未收到用户对争渡双语音修复的听感反馈，也没有在本轮完成整局**，不得扩大写成完整人工验收通过。
3. 用户授权自动执行构建和必要的针对性测试，并已明确授权替换 `C:\Program Files\飞船斗地主` 的试用内核；若下次替换时游戏开着，用户也已授权代理自行将其退出。操作前先确认游戏、AI 服务、更新器和长测进程状态；需要替换时先让游戏正常退出，必要时仅结束相关进程，再备份待覆盖文件、复制、核对哈希。**打包、正式安装包、签名、发布服务器、推送、标签、版本号变更仍需用户另行明确指令。**
4. 用户最新明确要求：小改动只跑受影响的针对性测试；已通过且未受影响的 CTest、90000 局本地基线和 30000 局假模型压力结果直接沿用。有具体回归风险或改变相关行为时才扩大测试；不要每次都机械重跑完整 CTest 或长测。真实模型场景评分会消耗额度，现有最小真实验证授权不能视为继续消耗额度的授权。
5. 当前安装目录共 197 个文件。主程序 1,070,080 字节，文件版本 2.1.0.0，SHA-256 `E2ECE1A22E584897A3DEA39298D9ED185D73D550D085A1BAFC8C374E4454535A`；AI 服务 121,344 字节，SHA-256 `49E5E4FD20A2D176777621E045830B985182B2313A8E5566460313BF9AABCDF3`；均与 `build\release-x64\` 当前构建输出相同。安装器并未运行，注册表安装版本仍是旧 2.0；安装目录里的更新器也仍是 2.0.0.0。不要把“试用内核复制”写成正式 2.1 或 AI 对战安装包已安装。
6. AI 数据目录为 `%APPDATA%\Feichuan\feichuan_offline_doudizhu\ai_battle\`。现有 DPAPI `credentials.dat`、`settings.json`、`statistics.json`、`logs/` 与 `replays/`；`replays/` 仍为 **0 个文件**，因为本轮为节省额度在整局结束前主动退出。已读取脱敏统计来定位 14,996 ms 超时，并检查新逐局日志的安全白名单；**从未读取 `credentials.dat`**。新日志记录 4 次成功决策，禁用字段扫描为阴性；但完整结算回放仍未产生，不能写成整局回放脱敏验收通过。不要把应用数据或凭据复制进仓库、构建日志或聊天。

## 最近问题：模式选择页双语音

- 用户在第一次安装版试用时报告“打开软件后有两种语音库，像两个人同时说话”。当时机器运行争渡（`ZDSRMain.exe`、`ZDSRMain_x64.exe` 等）。代码审查定位到新增的 `ModeSelectionWindow`：按钮获得焦点时，争渡会按 Qt/UIA 焦点自然朗读；模式页还调用 `AccessibilityService::announce`，通过争渡私有朗读 API 主动说同一内容。默认焦点的构造回调和 FocusIn 回调也都指向主动播报。
- 已在 `src\ui\mode_selection_window.h/.cpp` 移除该页的显式播报和无用的 `AccessibilityService` 依赖；`src\app\startup_controller.cpp` 改为直接构造模式页。按钮的可访问名称、位置、说明、Tab/Shift+Tab、回车选择保留，2.1 单机牌局中的原有朗读路径未改。`tests\ui\test_main_window_shortcuts.cpp` 增加模式页切换焦点恰好一个 Focus、零额外 Announcement 的断言。
- `tools\build.ps1 -Preset release-x64 -Build` 成功；针对性的 `test_main_window_shortcuts` 通过（12.31 秒）；本次已经额外跑过完整串行 CTest 21/21、0 失败、182.51 秒，原始 CTest 记录在 `build\release-x64\Testing\Temporary\LastTest.log`。修复版主程序已复制到安装目录，构建与安装 SHA-256 一致；隐藏启动 3 秒正常，未在进入 AI 模式前拉起 AI 服务。**自动测试不能证明真实争渡听感已修复；先等用户反馈，再决定是否进一步改动。**
- 下一会话若用户仍报告双语音，先确定是模式页、AI 设置页还是牌局页、在首次焦点还是 Tab 后发生、两路是否朗读相同文本；再针对该路径查自然 UIA 焦点与私有 API 叠加。避免直接改变冻结的 2.1 牌局朗读。若只是此修复的小调整，跑受影响的 UI 测试即可；不要重跑所有长测。

## 0. 不可违反的边界

- 项目固定在 `C:\Feichuan-Doudizhu`；Qt 6.8.3、MSVC 2022 x64、CMake、Ninja。
- 2.1 单机版是冻结基线，正式产品提交 `eafc5f07e9e373a78581ec8b061651f36f74b3e5`，标签 `v2.1`。二/三/四人、初/中/高级 AI、规则、计分、持久化、声音、快捷键、读屏行为不得改变；不得恢复已撤回的控制牌 AI。
- 已获用户授权替换本机安装目录的试用内核，并可在替换前结束运行中的游戏；版本号、正式安装包、签名、更新服务器、GitHub 推送和标签仍未经授权。
- 禁止 `git reset`、`git checkout --`、`git clean`。工作区当前有大量未提交改动，全部属于本功能，必须保留。
- 主程序、core、ai、ui 不得链接 Qt Network；只有独立更新器和独立 `飞船斗地主AI服务.exe` 允许。
- 不实现 ChatGPT 账号/网页登录/Cookie/令牌复用/未公开端点；OpenAI 仅正式 API 密钥。
- 密钥、认证头、完整模型响应、整桌暗牌不得进入仓库、文档、日志、诊断、回放、统计或剪贴板。

## 1. 当前状态（一句话）

AI 对战功能已在仓库构建目录完成开发，自动测试全绿（CTest 21/21）、假模型 30000 局压力零故障、并用用户授权的 DeepSeek `deepseek-v4-flash` 快速档真实打完二/三/四人各 1 局；**所有改动尚未提交**。安装目录已替换为 AI 对战试用内核；用户已重新打开软件，但实际试用结果尚未反馈。

## 2. 已完成内容（对照规格阶段）

| 阶段 | 状态 | 主要产物 |
| --- | --- | --- |
| 0 规格与基线 | 完成 | `docs\AI对战模式规格.md`、`AGENTS.md` 边界更新 |
| 1 首页 | 完成 | `src\ui\mode_selection_window.*`、`src\app\startup_controller.*`、`src\app\game_mode.h` |
| 2 决策接口 | 完成 | `src\ai\local_ai_decision_adapter.*`、`src\ai\ai_decision_request.*`、`src\app\ai_battle_types.*` |
| 3 AI 服务 | 完成 | `apps\ai_service\*`（IPC、DPAPI 凭据、三协议、错误分类） |
| 4 设置 | 完成 | `src\ui\dialogs\ai_battle_settings_dialog.*`、`credential_manager_dialog.*` |
| 5 对战 | 完成 | `src\ui\main_window.*` 云叫分/出牌、错位拒绝、引擎复验 |
| 6 故障与记录 | 完成 | 故障暂停/重试/打开设置/返回首页；`src\persistence\ai_battle_statistics_repository.*` 脱敏统计与回放摘要 |
| 7 质量与压力 | 大部分完成 | 假模型 30000 局完成；真实模型最小验证完成；本地稳定性门槛 9 组 × 10000 局 = 90000 局全绿（2026-09-24）；四人 5000 局精简回归已补跑；试用内核已安装、真实听感和对局反馈待用户提供；固定战略场景评分未做 |

关键行为要点（阅读代码时不要改坏）：

- 云请求只能通过 `AiActionCatalog::create` 生成动作目录，响应必须同时匹配请求号、`gameId`、`eventSequence`、阶段、座位（`AiDecisionResponse::matches`），随后仍交给 `GameEngine::execute` 复验；过期/错位/引擎拒绝都算故障且不自动重试。
- 目录上限 512、请求上限 128 KiB；超限时按“保留合法过牌／立即出完／全部炸弹／各牌型与主体点数代表／阻止地主立即获胜／高级 AI 首选”确定性缩减，不随机删除。
- 纯单机模式不创建设置/统计/AI 客户端对象，不启动 AI 服务（`testOfflineWindowNeverCreatesAiServiceClient`）。
- AI 对战故障后菜单“重试当前云模型回合”可用；重试必须由玩家显式触发。

- 2026-09-24 补跑门槛：`local-baseline-<人数>p-<档位>.txt` 为二/三/四人 × 初/中/高级共 9 组 × 10000 局 = 90000 局，全部正常结束、五项故障计数全零、全部组零云请求；`local-stability-4p.txt` 为四人高级 5000 局精简回归全零；`ctest-full3.txt` 为串行 CTest 21/21（184.43 秒）。注意 `local-baseline-120k-summary.txt` 只是驱动脚本汇总，该脚本用 `Start-Process -PassThru` 取不到子进程退出码，末尾 `SUMMARY groups=9 failing=9` 与各行 `exit=` 都不可信，判定必须以各结果文件内容为准。
## 3. 已有实测证据（均在 `build\release-x64\`）

- 最新双语音修复：串行 CTest **21/21 通过、182.51 秒**；`Testing\Temporary\LastTest.log` 是当时的 CTest 原始记录。主窗口快捷键目标单独跑过且通过（12.31 秒）。后续小改动优先运行受影响目标，勿无故重跑这些结果。
- 此前 AI 对战完整回归：`ctest-full3.txt` 为 **21/21 通过、184.43 秒**；其时的程序尚未包含后来的模式页双语音修复。

- `ctest-full2.txt`：CTest **21/21 通过**，总耗时 185.60 秒（空闲机器复跑）。
- `stress-cloud-2p.txt` / `-3p.txt` / `-4p.txt`：假模型二/三/四人各 **10000 局**，合计 **30000 局、1,366,658 次云决策**，非法动作 0、未结束 0、重复发牌 0、盖牌被打出 0、农民炸队友 0、错位 0、目录失败 0。四人模式 `trimmed_catalogs=262`（触发 512 项裁剪）并全部通过保留规则核验。
- `real-model-evidence.txt`：`deepseek-v4-flash` 快速档真实对局 二/三/四人各 1 局，共 137 次云决策，0 失败、0 非法、0 错位；平均 0.45 秒、P95 0.68～0.72 秒。
- `aisvc-recheck.txt`：本地假服务 1000 次决策 P95 **48 毫秒**（门槛 <50 毫秒）；超时触发误差 <250 毫秒；1.5、自然语言、代码块、401、超大响应、重定向等故障分类全部按预期。
- `local-stability-2p.txt` / `-3p.txt` / `-4p.txt`：本地高级 AI 各 **5000 局**，非法 0、未结束 0、重复发牌 0、盖牌被打出 0、农民炸队友 0；四人组已在 2026-09-24 补跑完成。
- 主程序依赖检查：`飞船斗地主.exe` 内不含 `Qt6Network.dll` 字符串；构建目录中的 `Qt6Network.dll` 只服务 AI 服务与更新器。

构建产物 SHA-256（本轮回合结束时的构建目录状态）：

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `飞船斗地主.exe` | 1,047,552 | `BD86DA42CAFE5CD4442963D9A3EA56D824899CA21DC1D6CB2EEFE42D61EE78DE` |
| `飞船斗地主AI服务.exe` | 121,344 | `49E5E4FD20A2D176777621E045830B985182B2313A8E5566460313BF9AABCDF3` |
| `飞船斗地主更新器.exe` | 72,192 | `F9C0C69C01F9E35EB09BA5F9B847B50A90288DDBB4308105774CB769CB6F0017` |
| `tls\qschannelbackend.dll` | 254,600 | `C58AB3C5873D3D5187FF1E6703662810F555EBE60C532E025250C47BBAC2E50C` |

这些哈希描述本次交接核对时的构建输出；任何重新编译都可能变化，下一会话替换安装版前必须重新计算并核对。

## 4. 未完成事项与下一步（按优先级）

P1（阻塞“可交付”）：

1. **已完成（2026-09-24）**：原 2.1 的本地稳定性基线。实际门槛为 9 组 × 10000 局 = 90000 局（原文档“120000 局”为计数错误，已订正），全部正常结束、五项故障计数全零、全部组零云请求；命令见第 8 节，证据 `build\release-x64\local-baseline-*.txt`。
2. **已完成（2026-09-24）**：四人精简回归补跑（`stability 4 2 5000 500003`，5000/5000、五项全零），结果写入 `build\release-x64\local-stability-4p.txt`，填补此前空文件。
3. **安装版试用内核已替换，结果待用户反馈**：2026-09-24 用户明确授权后，将构建版主程序与 `飞船斗地主AI服务.exe` 复制到 `C:\Program Files\飞船斗地主`，文件数 196→197。双语音修复后当前主程序 1,047,552 字节、SHA-256 `BD86DA42CAFE5CD4442963D9A3EA56D824899CA21DC1D6CB2EEFE42D61EE78DE`；AI 服务 SHA-256 `49E5E4FD20A2D176777621E045830B985182B2313A8E5566460313BF9AABCDF3`，均与当前构建目录一致。原有 Qt6Network 与 TLS 插件在替换时哈希未变。最初旧内核备份及前后清单在 `build\release-x64\install-trial-backup-20260924\`；双语音修复前主程序另备份在 `build\release-x64\install-trial-backup-20260924-voicefix\`。修复后隐藏启动 3 秒通过，AI 服务未提前启动，测试进程已结束。用户随后已打开安装版，交接时游戏和 AI 服务进程正在运行。应用数据目录目前已有设置和统计文件，但回放目录仍为空，实际统计内容及脱敏情况尚未检查。
4. **无障碍人工验收**：模式页 Tab/Shift+Tab/回车与朗读、故障弹窗朗读、API 密钥“已填写/未填写”、模型列表朗读。自动化已覆盖，但 NVDA／讲述人／争渡／保益的人工复读路径仍需确认。
5. **真实模型继续验证**：`deepseek-v4-flash` 的“均衡/深入”在完整牌局提示词下只返回思考过程（已知限制，会按 `reasoning_only_output` 暂停回合）；需确认 `deepseek-v4-pro` 或其它模型是否可用，并按规格完成固定战略场景（立即出完、地主剩一张必须拦截、队友即将获胜、炸弹取舍、全知叫分、全知残局、二人盖牌、四人双副牌复杂组合）。

P2（体验与走查）：

6. 认证管理人工走查：Alt+N/E/D/F/S、清除密钥、HTTP 明文警告、不可删除使用中的认证、编辑不回显旧密钥。
7. 牌局进行中设置页人数锁定，以及故障后更换共用的认证/模型/强度/超时并显式重试。
8. 真实故障走查：DNS 失败、断网、证书错误、401/403/404/429/5xx、超时、AI 服务被杀后冻结回合。目前只有本地假服务的分类测试。
9. 真实 AI 对战结束后的 `statistics.json`、`replays\` 摘要人工检查（确认无手牌/底牌/密钥/原始响应）。统计文件现在已出现，但尚未核验是否有完整对局；`replays\` 在交接快照时没有文件，不得把“文件存在”写成脱敏检查通过。

P3（仅用户明确要求后）：

10. 版本号（可能 2.2）、打包、签名、更新服务器、GitHub 推送与标签、安装包发布。

## 5. 真实模型配置现状（本机，不含密钥）

- 认证显示名：`DeepSeek V4 Pro`；协议：兼容 Chat Completions；请求地址 `https://api.deepseek.com`（代码自动补 `/v1/chat/completions`）；认证方式 Bearer。
- 模型列表：`deepseek-v4-flash`、`deepseek-v4-pro`；手工模型 `deepseek-v4-flash`（用户指定默认，快且便宜）。
- 选项：`standard_reasoning_parameter=true`、`structured_output=true`。
- 凭据保存在应用数据目录 `ai_battle\credentials.dat`，由 AI 服务用 Windows DPAPI 绑定当前用户整体加密；密钥不在仓库、文档、日志中。新会话不要重问密钥，除非用户要求更换或吊销。

强度映射（兼容 Chat Completions，启用标准推理参数时）：快速 → `reasoning_effort=none`（关闭隐藏思考，约 0.45 秒/次）；均衡 → `high`；深入 → `max`。OpenAI 官方 Responses 仍用 low/medium/high。思考模式与 JSON 对象模式互斥：只有非思考请求附带 `response_format={"type":"json_object"}`，不静默删参重试。

## 6. 关键文件地图

- 首页与导航：`src\ui\mode_selection_window.*`、`src\app\startup_controller.*`、`src\app\game_mode.h`、`src\main.cpp`
- 决策与目录：`src\ai\ai_decision_request.*`（目录生成、语义签名、512 裁剪、响应匹配）、`src\ai\local_ai_decision_adapter.*`
- 对战类型与设置：`src\app\ai_battle_types.*`（`SeatControllerConfig`/`AiBattleSettings`/强度名称）
- IPC 客户端：`src\app\ai_service_client.*`（QProcess 逐行 JSON、128 KiB 限制、同步请求、服务退出处理）
- AI 服务：`apps\ai_service\ai_service.*`（三协议、强度映射、错误分类、重定向阻止、响应上限）、`apps\ai_service\credential_store.*`（DPAPI、原子写入、头部白名单）、`apps\ai_service\main.cpp`（标准输入超长行保护）
- 主窗口云逻辑：`src\ui\main_window.cpp`（`requestCloudDecision`/`handleCloudDecisionResponse`/`showCloudFault`/`retryCloudTurn`/`toggleBattleState`/`returnToMainScreen`）
- 设置与认证界面：`src\ui\dialogs\ai_battle_settings_dialog.*`、`src\ui\dialogs\credential_manager_dialog.*`
- 统计与摘要：`src\persistence\ai_battle_statistics_repository.*`、`src\persistence\data_paths.*`
- 测试与长测：`tests\ai\test_ai.cpp`、`tests\ai_service\test_ai_service.cpp`、`tests\persistence\test_ai_battle_persistence.cpp`、`tests\ui\test_main_window_shortcuts.cpp`、`tests\ai\ai_validation_runner.cpp`、`tests\ai\ai_real_model_runner.cpp`
- 构建/部署：`CMakeLists.txt`、`cmake\InstallRules.cmake`、`tests\CMakeLists.txt`（AI 服务与更新器部署 `tls\qschannelbackend.dll`）

## 7. 已知坑（务必先看）

- **构建被长测锁住**：`ai_validation_runner.exe` / `ai_real_model_runner.exe` 正在运行时 ninja 会报 `LNK1104: 无法打开文件`。先停进程再构建。
- **不要并行跑 ctest 与长测**：`test_main_window_shortcuts` 的焦点类用例在 CPU 高负载下会偶发 `hasFocus()` 失败，空闲机器上是 112/112 通过。
- **tls 插件必部署**：AI 服务旁缺少 `tls\qschannelbackend.dll` 时 HTTPS 直接报“无法连接模型服务”，不是网络问题。
- **思考模式陷阱**：DeepSeek 兼容接口同时要“思考 + json_object”只会返回思考过程；`reasoning_only_output` 是预期分类，不要改成自动重试。
- **解析容错是刻意的**：动作编号接受整数或纯数字字符串，容忍 `actionId`/`action`/`id`；越界、非整数、不存在编号、引擎拒绝都仍是硬失败。
- **真实额度**：`ai_real_model_runner` 不进 CTest，只在用户授权后手工运行；不要写进自动测试。

## 8. 常用命令

```powershell
# 构建（先确认没有长测进程在运行）
.\tools\build.ps1 -Preset release-x64 -Build
.\tools\build.ps1 -Preset release-x64 -Configure -Build

# 全量自动测试（21 项）
.\tools\build.ps1 -Preset release-x64 -Test

# 假模型压力（不联网、不花钱）：二/三/四人各 10000 局
.\build\release-x64\ai_validation_runner.exe cloud-stability 2 10000 1000001
.\build\release-x64\ai_validation_runner.exe cloud-stability 3 10000 2000001
.\build\release-x64\ai_validation_runner.exe cloud-stability 4 10000 3000001

# 本地 2.1 AI 稳定性基线（正式候选门槛：二/三/四人 × 初/中/高级共 9 组、每组 10000 局 = 90000 局；
# 下面三行只是「高级」档示例，完整门槛还需把档位参数 2 换成 0（初级）和 1（中级）各跑一遍；
# 种子约定：初级 300001/300002/300003、中级 400001/400002/400003、高级 500001/500002/500003）
.\build\release-x64\ai_validation_runner.exe stability 2 2 10000 500001
.\build\release-x64\ai_validation_runner.exe stability 3 2 10000 500002
.\build\release-x64\ai_validation_runner.exe stability 4 2 10000 500003

# 真实模型对局（会消耗用户额度，仅用户授权时；不在 CTest 内）
.\build\release-x64\ai_real_model_runner.exe 2 1 "DeepSeek V4 Pro" deepseek-v4-flash 120 fast
.\build\release-x64\ai_real_model_runner.exe 3 1 "DeepSeek V4 Pro" deepseek-v4-flash 120 fast
.\build\release-x64\ai_real_model_runner.exe 4 1 "DeepSeek V4 Pro" deepseek-v4-flash 120 fast
```

## 9. 新会话开头建议动作

```powershell
git -c core.quotepath=false status --short
git log -3 --oneline
git diff --stat
```

先确认工作区仍是本节描述的未提交状态（不要提交、不要清理），再看用户对修复版争渡听感和实际对局的反馈。试用内核替换与必要时结束运行中的游戏已获授权；打包、签名、正式发布、推送、标签及版本变更仍须用户明确下达新指令。按用户要求只跑受影响的针对性测试。
