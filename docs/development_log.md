# 开发日志

## 2026-08-03 版本3.0长期内测：生成朋友测试安装包

- 用户人工确认音效与背景音乐音量卡死问题解决后，明确要求生成D盘根目录安装包供朋友继续测试；版本保持3.0，未上传或修改更新服务器。
- 新建`build\installer-stage-3.0-audio-confirmed-final`，部署最新Release、Qt 6.8.3运行组件、VC++运行库、README、162个WAV及两处共12份UTF-8 BOM文档；安装包内更新日志同步写明声音设置只在按OK后生效的最终规则。
- 阶段审计结果为162个WAV、12份BOM TXT、0个Qt6Sql/Qt6Concurrent/Qt6Test/sqldrivers及第三方读屏组件；阶段EXE哈希与Release一致，均为`D778484B00548D8240F92183A9FDD98D21FE472AC7EA3E0E3A77F227EF901605`。
- 最终安装包为57,030,989字节，SHA-256为`0C7152AFF6C5A71A79CB50F5DB6FAB53C3AD74A7F0332148C67171A0B26604D4`；临时输出、项目dist和`D:\飞船AI斗地主单机版-Setup-3.0.exe`三份完全一致。旧dist 3.0包已先归档保存。
- 本轮没有重复安装或启动游戏，游戏进程为0。误建的普通临时文件经复核后因执行环境安全策略无法删除，但未被本次全新阶段引用，也未进入安装包。

## 2026-08-03 版本3.0长期内测：音效与背景音乐音量卡死彻底修复并通过用户验收

- 根据音效音量和背景音乐音量均可导致F5设置卡死的连续实测，最终取消设置窗口内全部音频实时监听：两个开关、两个音量及音乐模式只修改待确认值，按OK后统一应用并保存，按Cancel不改变运行状态。
- 短音效和背景音乐完全移除`waveOutSetVolume`，统一从原始PCM副本按0%至100%进行软件缩放；背景音乐按OK应用新音量时安全重建循环播放，不在数字框编辑期间操作音频设备。
- 短音效改用`CALLBACK_NULL`，不再接收WinMM完成回调；自然完成由Qt主线程轮询`WHDR_DONE`后释放，主动停止也只在主线程清理，消除设备调用与回调锁序造成死锁的结构性风险。
- 自动回归覆盖待确认值、OK应用、PCM 0%/26%/48%缩放、短音效自然完成与快速停止、背景音乐播放中多档音量重建；Release全目标构建及14/14 CTest通过，总测试约50秒。
- 最新Release与Program Files内核均为721,920字节，SHA-256为`D778484B00548D8240F92183A9FDD98D21FE472AC7EA3E0E3A77F227EF901605`。用户已在正式安装版亲自测试并确认问题解决；未制作安装包、未上传服务器，版本保持3.0。

## 2026-08-03 版本3.0长期内测：音效音量快速编辑卡死修复（已由上方最终方案取代）

- 用户使用正式安装版进入F5设置，在音效音量框快速编辑数值时界面卡死；脱敏日志确认焦点位于`soundVolumeSpinBox`，正式版进程持续无响应。
- 根因是上一版`setVolume`持有声音句柄互斥锁时，对尚在播放的试听句柄调用`waveOutSetVolume`；试听完成回调并发等待同一把锁时，部分实机驱动会让主线程与回调互相等待。
- 短音效与音效试听不再修改正在播放的waveOut句柄音量，统一在每次播放前复制原始PCM并按当前0%至100%数值软件缩放；快速输入新数字时停止上一声，再以新百分比创建试听，音量仍即时生效且不接触上述锁序。
- 背景音乐使用独立的无回调循环句柄，原有实时音量调整和软件回退保持不变；牌局音效队列、玩家声线、162个WAV和查找顺序不变。
- 新增连续快速切换26、4、45、0、48、100等音量并反复停止/重播试听的回归；针对性测试及Release全目标14/14 CTest均通过，总测试约51秒。
- 已确认并只关闭卡死的`C:\Program Files\飞船AI斗地主单机版\FourPlayerDoudizhu.exe`进程。最新Release与Program Files内核均为729,600字节，SHA-256为`D8C7C69C26C159BD9393E1D8F4EC0516A10E67C40DB0076975FFD4CBDF7BB6D5`；替换后未启动游戏、未制作安装包、未修改服务器。

## 2026-08-03 版本3.0长期内测：游戏音效与背景音乐音量实时调节

- 根据本机设置值26%和48%的实测反馈，确认数字框能够用上下光标修改并保存，但短音效实际仍通过`PlaySoundW`直接播放文件，完全绕过了已有PCM音量缩放；背景音乐则只在设置对话框按“确定”后才应用数值。
- 短音效统一改为解码PCM后由waveOut播放，优先使用当前播放句柄的独立音量控制；驱动不支持时自动回退软件缩放，0%至100%的设置现在会真实作用于游戏音效。
- 设置对话框的音效开关、音效音量、背景音乐开关、音乐音量和音乐模式支持即时预览；音效音量每次变化通过独立试听通道播放短提示音，不打断原有出牌语音安全队列。
- 背景音乐音量优先原地更新当前waveOut句柄，不再为每个数值变化重启音乐；不支持句柄音量的环境继续使用软件缩放回退。按“取消”恢复打开设置前的声音状态，只有按“确定”才保存到`settings.json`。
- waveOut主动停止与自然播放完成采用两条独立清理路径，避免重置句柄触发回调时重复关闭造成死锁；音效队列、玩家声线、资源查找优先级和162个WAV保持不变。
- 新增PCM百分比缩放、waveOut短音效路径、音量数字框上下光标即时预览和取消还原回归；Release全目标构建及14/14 CTest通过，总测试约50秒。
- 最新Release与Program Files内核均为730,112字节，SHA-256为`32DB548ED3D2945E3F24B14048152199CE6348510EFDA89BA9A4A0F6AC3196FB`；只替换本机安装版EXE，未制作安装包、未启动游戏、未修改更新服务器。

## 2026-08-03 版本3.0长期内测：传统读屏事件兼容修复

- 根据外部 Windows 11 24H2、争渡读屏和3.0正式版诊断，确认普通Qt/UIA菜单可以朗读，但Qt 6.8公告通知事件完全无声；程序界面、键盘事件和Qt无障碍激活状态均正常。
- 动态状态不再发送`QAccessibleAnnouncementEvent`，改为把消息写入现有状态控件的可访问名称并发送传统`QAccessible::Focus`事件；不调用`setFocus()`，真实键盘焦点不会离开菜单、叫分按钮或手牌。
- 每条消息只投递一个Windows可处理的传统焦点事件，不同时发送新旧路线；继续不加载争渡、保益或NVDA专用DLL，也不加入SAPI或其他内置语音。
- 诊断报告格式保持6，投递通道改为`qt_focus_event`；公告调度、150毫秒重复过滤、拿放牌静默、真人出牌后手牌静默及全部游戏音效保持不变。
- 新增无障碍事件回归，验证传统Focus事件、移除Announcement事件、消息可访问文字、真实键盘焦点不变和重复过滤。
- 全新Release全目标构建和14/14 CTest通过；Release、安装阶段和Program Files正式EXE均为704,000字节，SHA-256为`1B13875E5AF8A1AE5E8808F0E37C44354F111D95C98D843B87738B609C6973B5`。
- 独立兼容测试安装包为57,024,524字节，SHA-256为`068F7E8F29664CB90F85C408A37072F2763D527C10321DD1C609A2C45BE61389`；已静默覆盖本机，版本保持3.0，安装后未启动游戏或修改更新服务器。
- 安装后首次核验游戏进程为0；最终审计时Program Files正式版已随后运行，未关闭进程或发送游戏按键。

## 2026-08-03 版本3.0长期内测：安全发牌、公开历史与三级训练模型

- 版本继续保持3.0，不增加玩法功能，不联网发布；改动范围限定为随机性、三档机器人、训练接入、稳定性和无障碍错误反馈。
- 正式牌局使用Windows系统加密随机源、拒绝采样和无偏Fisher-Yates；测试/训练使用稳定确定性洗牌，无人叫分重发牌仍保持同种子可复现。
- 公开快照新增完整叫分、出牌和过牌历史，保存格式升为3；AI仍只能得到自己的手牌和公开信息，其他三家隐藏手牌未进入观察对象。
- 三级统一使用StandardAI和独立模型，预算为初级150毫秒、中级450毫秒、大师1000毫秒；公开底牌参与叫分估值，公开出牌历史和受预算约束的后续动作估值参与选牌。
- 模型格式兼容旧`heuristic_v1`并新增`heuristic_search_v2`、叫分参数、两轮级联评测元数据和三级层级名。
- 游戏改为加载原子三级清单及同集合模型；严格校验路径边界、文件哈希、内容哈希、规则指纹、层级和资格。损坏时整套回退内置三级逻辑，并通过标准Qt对话框只提示一次。
- 新增本机训练安装退出请求：活动牌局会先放弃，再停止计时器和声音、保存设置并正常关闭，不强制杀进程。
- Release全目标构建和14/14 CTest通过，含中级500局、大师500局稳定性长测；未启动正式游戏或修改服务器发布记录。
- 本地3.0长期内测EXE为704,000字节，SHA-256为`1AFB5E2A9233A9FF0087F821F9A0098D8EB7818C7D66E1B3B65D43C9954EDC33`；安装包为57,024,700字节，SHA-256为`B2DE113358FD25B24441873C0352717C488FB03AD2F857D2544BEE12C6902F6A`。静默覆盖成功，未启动游戏或上传服务器。

## 2026-08-02 版本2.9 本地大师训练模型接入（历史）

- 按用户确认的独立训练方案，为大师级机器人增加经过正式评测的本地 `.fpdzmodel` 权重加载；初级和中级保持原逻辑，模型缺失、损坏、规则不符、内容哈希错误或未获晋升资格时自动使用内置大师逻辑。
- 活动模型固定为 `%APPDATA%\FourPlayerDoudizhu\FourPlayerDoudizhu\models\master\active_model.fpdzmodel`，只在新牌局创建大师机器人时读取，不访问真人训练数据、不联网。
- 新增 `heuristic_v1` 模型格式、20项有界启发式权重、FourPlayerStandardV1规则指纹和SHA-256内容完整性校验；游戏规则引擎继续验证全部叫分和出牌命令。
- 四王的牌型文本统一为“天尊”；一小王加一大王仍为“王炸”，原有双王枪毙音效映射不变。
- 只构建 `FourPlayerDoudizhu` 应用目标，未运行CTest、自动测试或游戏。版本保持2.9，未启动软件，未删除AppData数据。
- Release、安装阶段和Program Files正式版EXE均为676,352字节，SHA-256为`37902E862703B8CEC8A8353163B472C605DB46EE6EEA88211C2A259593042A6B`。
- 项目安装包 `dist\飞船AI斗地主单机版-Setup-2.9.exe` 为57,015,640字节，SHA-256为`5C7FCE78280A789D4E6FCE55016C6A699833F68238E6BF57D69B832814AFE221`；使用静默参数覆盖正式版成功，安装后游戏进程为0。
- 配套训练系统位于 `C:\y.apple`，训练数据位于 `D:\y.apple-data`，完整交接以 `C:\y.apple\交接说明.txt` 为准。

## 2026-08-02 版本2.9 本地修订：移除远程大模型功能

- 按用户决定彻底移除远程大模型接管功能，删除远程请求客户端、模型策略、API 认证存储和管理对话框、联网决策诊断、远程教师数据学习链及对应测试目标。
- 设置中的机器人模式固定为初级、中级、大师级三档，三个电脑玩家全部使用本地逻辑；旧设置中的大师级数值 3 会迁移为当前大师级数值 2，其他旧远程字段被安全忽略。
- 保留本地叫分、出牌、提示、规则验证和全部牌局时序；大师级继续使用原有最高档本地策略，不再叠加历史远程教师训练结果。
- 删除不再需要的 Qt SQL、Concurrent 和 qsqlite 运行依赖，保留软件更新所需的 Qt Network；覆盖安装会清理安装目录里的旧 DLL，但不会删除 AppData 中既有凭据或训练库等用户文件。
- 诊断报告格式升为 6，删除远程模型、请求、降级、认证失败和 token 统计字段；其他诊断、无障碍、键盘、音效、明底牌、F2、更新和统计路径不变。
- 按用户要求版本保持 2.9；仅构建应用、制作项目目录安装包并后台覆盖本地正式版，不运行自动测试或 CTest，安装后不启动、不测试。
- Release、安装阶段和 Program Files 正式安装版 EXE 均为 666,624 字节，SHA-256 为 `CD3ECE4E29F600C360378A6868697F9855E010871B3156425C6A56CAB4E83CCE`；安装阶段为 `build\installer-stage-2.9-local-only`。
- 项目安装包 `dist\飞船AI斗地主单机版-Setup-2.9.exe` 为 57,010,822 字节，SHA-256 为 `6EEDAEF1661D9E56FF9B7DDA22414288CDA01F6C3ED7575590843C07B852C916`；未复制到 D 盘根目录。
- 使用 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NORESTARTAPPLICATIONS` 后台覆盖安装成功，退出码 0；安装后进程未运行，公共桌面快捷方式继续指向 Program Files 正式版，旧凭据和训练库文件均保留但不再读取。

## 2026-08-02 版本2.9 轻量远程 AI、标准无障碍与稳定性优化（历史）

- 依据既有脱敏日志确认远程大模型曾实际接管：429 次请求中 110 次采用、319 次降级；轻量非思考模型成功 105 次、失败 3 次，而高性能思考模型成功 5 次、失败 297 次。2.9 因此删除均衡、轻量、高性能三档和自动切换，只保留用户选择的一个当前模型。
- 旧认证优先迁移原 `economyModel`，其次使用原 `model`；新 DeepSeek 配置默认 `deepseek-v4-flash`，模型列表完整展示并允许手填。密钥仍仅通过 Windows DPAPI 当前用户范围保存，本轮未读取、输出或调用真实凭据。
- DeepSeek 固定使用 `/chat/completions` 且关闭思考；其他提供商保留显式协议，自动兼容路线只探测一次并在会话内记住成功协议。请求采用 `compact_v3`、最多 24 个候选、低随机度和 64 个输出 token，不发送其他玩家隐藏手牌，也不记录请求正文。
- 远程决策增加 8 秒绝对超时且不自动重试；空响应、非法动作、认证错误、超时和过期结果均立即使用本地高级 AI。连续三次普通失败暂停远程请求 60 秒，401/403 暂停到用户重新保存认证。
- API 认证管理新增用户手动触发的“测试当前模型”；仅点击后发送最小动作选择请求，并显示模型、合法动作结果和耗时。程序不会自动测试、自动开局或输出凭据。
- 诊断报告升级为格式 5，新增会话请求、成功采用、本地降级、超时、空响应、认证失败及 token 计数；朗读结果改为“Qt/UIA 公告请求已提交，实际朗读由读屏软件决定”，不再把接口调用误报为已经朗读。
- 继续只使用 Qt/UIA 标准无障碍，不接入 SAPI、NVDA Controller、争渡、保益或其他专用朗读接口。手牌移动不再额外连续发送 Focus、Selection、NameChanged 三种事件，只保留 Qt 控件自身语义和单一标准公告，减少重复朗读风险。
- 低级键盘钩子正确区分按住重复；上、下、回车、空格、功能键等单次动作只处理首次按下，左右连续浏览保留。窗口失焦、卸载时清空按键状态，物理重复事件不再逐条写入诊断。
- 诊断写入改为 250 毫秒有界批处理，内存队列最多 4096 条或 2 MB，过载时汇总丢弃数量；日志仍保持单文件 10 MB、保留 5 份，避免事件风暴拖慢界面。
- 新通用能力调研优先复用 Qt、Microsoft 标准能力和项目现有组件；没有引入新的第三方运行依赖。NVDA、争渡和保益文档仅作为标准无障碍兼容参考，专用 Speak API 不符合本项目底层原则，未采用。
- 按用户要求没有运行任何自动测试或 CTest，没有启动游戏、发送游戏按键或调用真实远程 AI；仅配置并编译 `FourPlayerDoudizhu` Release 应用目标。
- 使用固定 MSVC 14.50.35717、Windows SDK 10.0.26100.0 和 Qt 6.8.3 完成 `--clean-first` Release 应用目标构建；Release、2.9 安装阶段和 portable EXE 均为 831,488 字节，SHA-256 为 `39EFFED0A7C913233E8002C6BAD48256F2AE28298BE782A5DEA1625A4C19199B`。
- 2.9 安装阶段为 `build\installer-stage-2.9-light-ai-uia-stability`，默认 WAV 为 162 个，不包含 Qt6Test、NVDA Controller、争渡或保益专用运行文件。
- 2.9 安装包为 `dist\飞船AI斗地主单机版-Setup-2.9.exe`，大小 57,997,935 字节，SHA-256 为 `DE8D9FF2756A8586B7D45999E62D938C2010BFA60D34B6DB6AB4D77A1A588C6E`；D 盘测试副本哈希一致，已使用 `/NORESTARTAPPLICATIONS` 静默覆盖正式安装版，安装器未启动游戏。

## 2026-08-01 版本2.8 拿放牌读屏静默

- 按用户要求，将上光标拿一张、Ctrl+上光标拿当前组、下光标放一张和 Ctrl+下光标全部放下统一改为读屏静默；成功和“当前没有拿起的牌”等无效放牌操作均不再发送主动公告。
- 拿放牌修改模型状态和移动当前索引期间临时屏蔽可访问文本，并停止为选择状态变化发布 `Qt::AccessibleTextRole`，避免 Qt/UIA 根据模型更新产生隐式朗读；普通左右、Shift+左右、Home 和 End 浏览仍按原格式朗读。
- 上下光标不再解除真人成功出牌后的手牌静默，只有下一次主动浏览手牌才恢复可访问文本；原有拿起、放下游戏提示音、累计多张和多组选牌、拿牌顺序及出牌逻辑保持不变。
- `hand_action` 脱敏诊断继续记录拿放牌结果，`take_single` 删除不再真实发生的 `announcement` 字段；本轮不修改规则、AI、远程 AI、F2、明底牌、牌型音效或其他无障碍公告。
- 按用户要求没有运行任何自动测试或 CTest；使用 CMake `--fresh` 和 `--clean-first`，仅构建 `FourPlayerDoudizhu` Release 应用目标，构建成功。
- Release、2.8 安装阶段、portable 和 Program Files EXE 大小均为 820,736 字节，SHA-256 为 `2726F5A905FECBA44EA58EFC9A7A283ED45983D48DD155CBD10005EF76BA0365`。
- 2.8 安装包位于 `D:\FourPlayerDoudizhu\dist\飞船AI斗地主单机版-Setup-2.8.exe`，D 盘测试副本位于 `D:\飞船AI斗地主单机版-Setup-2.8.exe`；大小均为 57,985,846 字节，SHA-256 为 `0BCEB9FDC1A57BEEB10B4D6AFFB8265449AE5C713DD498B531E77711864269D5`。
- 安装阶段为 `build\installer-stage-2.8-hand-actions-screen-reader-silent`；源码、安装阶段、portable 和 Program Files 默认 WAV 均为 162 个，且不存在 NVDA Controller、争渡或保益官方接口文件。
- 使用 `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NORESTARTAPPLICATIONS` 静默覆盖本机正式安装版，卸载注册表 `DisplayVersion=2.8`；安装器未自动启动游戏，公共桌面快捷方式继续指向 Program Files 正式安装版。
- 构建时发现当前进程 PATH 中 `C:\Program Files\dotnet"` 存在多余引号，导致 VS 环境批处理无法正确传递工具链；仅在本次构建进程内清理该字符，并显式使用本机 MSVC 14.50.35717 和 Windows SDK 10.0.26100.0，没有修改系统环境变量。

## 2026-08-01 版本2.7 飞机带对子口播去除“带”

- 修复飞机带对子重复播放两个“带”的问题；`CardPatternType::AirplaneWithPairs` 不再追加 `to.wav`，固定按“主体起点、至、主体终点、飞机、各翅膀对子”播放。
- 使用用户确认实际内容仅为“飞机”的 `D:\男生飞机代翅膀.wav` 和 `D:\女声飞机带翅膀.wav` 完整替换男女声 `plane.wav`，未裁剪、未转码，D 盘原文件保留不动。
- 精确牌例 QQQ、KKK、33、44 的计划队列为 `Q.wav`、`zhi.wav`、`K.wav`、`plane.wav`、`pair3.wav`、`pair4.wav`；三带一对等其他牌型继续保留原有 `to.wav` 逻辑。
- 本轮只修改飞机带对子音效规划、两份默认音效、相邻测试期望、版本和文档；不修改规则、AI、远程 AI、无障碍、键盘、姓名调度、明底牌、手牌静默或牌局推进。
- 按用户要求没有运行任何自动测试或 CTest；使用 CMake `--fresh` 和 `--clean-first` 只构建 `FourPlayerDoudizhu` Release 应用目标，构建成功。
- Release、2.7 安装阶段、portable 和 Program Files EXE 大小均为 822,784 字节，SHA-256 为 `A6B7A7CE88EAEF7012F6D309FA7EFD5BB23C715056113A5471C25E818F5D84D4`。
- 2.7 安装包位于 `D:\FourPlayerDoudizhu\dist\飞船AI斗地主单机版-Setup-2.7.exe`，D 盘测试副本位于 `D:\飞船AI斗地主单机版-Setup-2.7.exe`；大小均为 57,986,653 字节，SHA-256 为 `09B4EF3E63727FB773173355F750533DBF0B845EA95B7A2CADAF3B691EF80ED8`。
- 安装阶段为 `build\installer-stage-2.7-airplane-pairs-no-dai`；默认 WAV 总数保持 162 个，男女声 `plane.wav` 在源码、安装阶段、portable 和 Program Files 中哈希一致。
- 使用 `/NORESTARTAPPLICATIONS` 静默覆盖本机正式安装版，卸载注册表 `DisplayVersion=2.7`；安装器未自动启动游戏，公共桌面快捷方式继续指向 Program Files 正式安装版。

## 2026-08-01 版本2.6 双王枪毙语音修复

- 修复一张小王加一张大王组成王炸时，语音被拆成“小王、大王、枪毙”多个文件依次播放的问题。
- 王炸现在按玩家声线直接播放单个 `shuangwangqiangbi.wav`：男声玩家使用男声文件，女声玩家使用女声文件；随后继续播放原有公共纯枪毙效果音，既不改变效果音队列，也不影响其他炸弹牌型。
- 将用户放在 D 盘根目录的 `男生双王枪毙.wav` 和 `女声双王枪毙.wav` 导入默认音效资源，并在音效管理中新增男声、女声“双王枪毙”条目；默认 WAV 总数由 160 个增加到 162 个。
- 仅修改王炸音效规划、对应资源、音效目录、版本和文档；不修改规则、AI、远程 AI、无障碍、键盘、明底牌、手牌静默或牌局推进。
- 按用户要求不运行自动测试或 CTest；只进行必要的 Release 应用目标构建、安装包制作和本机覆盖安装。
- 使用 CMake `--fresh` 和 `--clean-first` 只构建 `FourPlayerDoudizhu` Release 应用目标，构建成功；Release、干净安装阶段、portable 和 Program Files EXE 大小均为 822,784 字节，SHA-256 为 `BC3FF2833CE6997E5E26D4D24B703719BF9E0FE30C36BE796B4B0162DFD7C0E9`。
- 2.6 安装包位于 `D:\FourPlayerDoudizhu\dist\飞船AI斗地主单机版-Setup-2.6.exe`，D 盘测试副本位于 `D:\飞船AI斗地主单机版-Setup-2.6.exe`；大小均为 58,112,745 字节，SHA-256 为 `FDDBDA832F5622D406B2F8A0E0C9FA99687620A2365CBB0FBAC20A53FFF6782A`。
- 使用 `/NORESTARTAPPLICATIONS` 静默覆盖本机正式安装版，卸载注册表 `DisplayVersion=2.6`；最终核验时正式安装版已再次运行，视为用户正在试玩，未再关闭。公共桌面快捷方式继续指向 Program Files 正式安装版。

## 2026-08-01 版本2.5 移除官方读屏兼容模式

- 按用户决定彻底移除争渡、保益和 NVDA 官方接口兼容路线；程序固定使用 Qt/UIA 标准无障碍，不再加载、调用或卸载第三方读屏 DLL。
- 删除设置中的兼容模式开关和旧设置字段；旧 `settings.json` 中的兼容字段会被安全忽略，其他用户设置和数据保持不变。
- 标准模式状态栏继续显示动态文字，但可访问名称固定为“游戏状态”、可访问描述固定为说明文字，不再随状态变化；每条动态消息只发送一个 `QAccessibleAnnouncementEvent`。
- 诊断报告格式升级为 4，移除官方接口初始化、后端探测和系统读屏 DLL 检测，只保留 Qt/UIA 路线及脱敏投递信息。
- CMake、部署和安装包不再包含 `nvdaControllerClient64.dll`；历史 `third_party` 归档保留但不参与运行时分发。
- 本轮不修改规则、AI、远程 AI、音效、键盘、手牌操作、牌局推进、明底牌或 F2；按用户要求不运行自动测试或 CTest。
- 使用 CMake `--fresh` 和 `--clean-first` 只构建 `FourPlayerDoudizhu` Release 应用目标，构建成功；未构建或执行测试目标。
- Release、安装阶段和 Program Files 安装版 EXE 大小均为 822,272 字节，SHA-256 为 `A9D2E012F75B6394AFA9F1B7F2712F503D8477934E3E7BB77C5860B87984E1DF`。
- 2.5 安装包位于 `D:\FourPlayerDoudizhu\dist\飞船AI斗地主单机版-Setup-2.5.exe`，朋友副本位于 `D:\飞船AI斗地主单机版-Setup-2.5.exe`；大小均为 58,112,930 字节，SHA-256 为 `5E4E78EE9D29BB365D44A4C1261E28A9524B262A0C1BAE555A2CBE4C4E767A61`。
- 使用 `/NORESTARTAPPLICATIONS` 静默覆盖本机正式安装版，卸载注册表 `DisplayVersion=2.5`；安装后未启动游戏进程，安装目录不存在旧 NVDA DLL 和许可文件。

## 2026-08-01 版本2.4 Qt/UIA重复朗读修复

- 根据外部 Windows 10 21H2、保益读屏和 2.0 绿色版脱敏诊断，确认兼容模式关闭、官方接口未初始化，重复朗读来自 Qt/UIA 标准模式的多重无障碍通知，而不是两个读屏后端同时工作。
- 状态栏改用固定可访问名称“游戏状态”，动态内容只更新可见文本和可访问描述；移除动态 `NameChanged` 事件，标准模式每次播报只发送一个 `QAccessibleAnnouncementEvent`。
- 备用官方接口兼容模式继续默认关闭，设置文字明确提示没有特殊情况不要开启；仅当标准模式动态内容完全无声时才使用。
- 诊断报告格式升级为 3，新增脱敏的最近投递通道和 `speech_delivery.channel`，只记录 `qt_announcement`、`official_api` 或 `none`，不记录朗读正文。
- 兼容模式后端单选策略、音效、F2、空格、回车、手牌操作、规则、AI、远程 AI 和明底牌流程保持不变；按用户要求不运行自动测试或 CTest。

## 2026-08-01 版本2.3 F2原生消息前置分发

- 2.2 正式安装版诊断确认 F2 已进入 physical_hook，但没有产生 bottom_cards_queried，根因是自定义钩子消息和原生备用路径仍位于 shouldYieldKeyboardHandlingToFocusedWidget 之后。
- 新增统一 F2 查询入口，物理钩子消息在菜单让路判断之前直接分发，原生键盘备用路径也移动到内部让路判断之前；Qt 快捷键和 Qt 事件复用同一入口。
- 新增不含底牌正文的 bottom_cards_shortcut_received 诊断事件，记录来源、阶段、焦点类型和 dispatched/blocked_modal 结果。
- 空格键过牌、回车出牌、叫分空格及其他所有功能保持 2.2 不变；按用户要求不运行任何自动测试或 CTest。

## 2026-08-01 版本2.2 F2物理键盘修复

- 根据正式安装版真实键盘日志确认，F2 的物理键码和扫描码已进入低级键盘钩子，但通用菜单让路判断可能阻止后续查询消息。
- 仅为无修饰键 F2 增加与 F11 相同的前台独立优先处理，并在 Qt、原生消息路径查询前关闭残留菜单状态。
- 空格键过牌、回车出牌、叫分空格、Alt+D 和 Ctrl+回车释放状态以及所有其他功能均保持 2.1 不变。
- 按用户要求不运行任何自动测试或 CTest，只构建 Release 应用、生成安装包并静默覆盖本机正式安装版。

## 2026-08-01 版本2.1快捷键调整

- 查询公开底牌由 `Alt+D` 改为软件前台单按 `F2`，并从 Qt 快捷键、Windows 原生消息、低级键盘钩子和系统热键注册中完整释放 `Alt+D`。
- 真人手动过牌由 `Ctrl+回车` 改为正式出牌阶段单按空格键；叫分阶段的空格仍确认当前叫分按钮，普通回车仍提交出牌。
- `Ctrl+回车` 不再执行或拦截游戏操作，保留给后续快捷键扩展使用。
- 按用户要求不运行任何自动测试或 CTest，只构建 Release 应用、生成安装包并静默覆盖本机正式安装版。

## 2026-08-01 版本2.0首次更新说明与正式发布

- 版本号由本地试玩版 `1.13` 提升为正式版 `2.0`，后续按 `2.1` 至 `2.9`、再到 `3.0` 的顺序递增。
- 首次说明修订号提升为 2；新安装或旧版升级后第一次启动时，自动使用 Windows 默认文本程序打开真实 TXT 更新说明。
- 更新说明汇总服务器正式版 `1.3` 至 `2.0` 的 10 项主要变化，并明确提示按 `Alt+F4` 关闭 TXT 后正常进入软件。
- 按用户要求不运行自动测试或 CTest，只进行 Release 应用构建、安装包生成、覆盖安装后的首次弹窗简要验证和服务器发布核验。

## 2026-07-31 版本1.13飞机带对子口播修正

- 使用用户最终确认的 `D:\男生至.wav` 和 `D:\女声至.wav` 更新男女声 `zhi.wav`。
- 使用 FFmpeg 将用户生成的 `D:\男生代.mp3` 和 `D:\女声代.mp3` 转换为单声道、44.1 kHz、16 位 PCM WAV，并作为男女声 `to.wav` 的“带”连接词。
- 飞机带对子由“主体范围、三顺、带、翅膀对子”改为“主体范围、飞机、带、翅膀对子”；纯三顺与三带一对保持各自原有牌型结构。
- 本轮只修改四个语音资源、牌型音频规划、相邻测试期望、版本和文档，不修改规则、AI、远程 AI、姓名调度、SoundService 队列、无障碍或叫分底牌。
- 按用户要求不运行任何自动测试或 CTest，只编译应用目标并由用户通过正式安装版实际试听。

## 2026-07-31 版本1.12连续牌型口播精简

- 顺子、连对、三顺的连续主体统一改为“起点、至、终点、牌型”，避免逐个点数或重复“对”“三个”的冗长播报。
- 将 1.9 至 1.11 的 `to.wav` 另存为独立 `zhi.wav` 用于“至”，并从 1.7 安装阶段恢复原 `to.wav` 用于“带”，修复三带一对和飞机带对子连接词错误。
- 飞机带对子按“主体范围、三顺、带、各翅膀对子”播放，不再额外追加“飞机带翅膀”；其他牌型和纯效果声保持不变。
- 仅调整牌型音频规划、两个连接词资源、音效管理目录和相邻测试期望，不修改规则识别、比较逻辑、姓名调度、SoundService 队列、AI、远程 AI、无障碍或叫分底牌。
- 按用户要求不运行任何自动测试或 CTest，只编译应用目标并由用户实际试听验收。

## 2026-07-31 版本1.11完整默认音效更新

- 将用户重新整理的 136 个 OGG 素材转换为未压缩 PCM WAV，并按标准路径覆盖对应默认资源；其余 22 个默认 WAV 保持不变，完整资源总数仍为 158 个。
- 男声与女声“火焰”分别映射到 `card_four/boy/fire.wav` 和 `card_four/girl/fire.wav`，不再沿用不对称的历史放置方式。
- 将 AppData 中 6 个自定义覆盖移至 `D:\待查看音效`，使正式安装版重新使用对应默认资源；未删除其他用户数据。
- 本次只更新资源、版本和文档，不修改游戏规则、AI、远程 AI、无障碍、叫分底牌或出牌调度代码，也不运行自动测试。

## 2026-07-31 版本1.7出牌播报与焦点回归修复

- 将玩家名称公告与牌型 WAV 队列改为主窗口分阶段调度，SoundService 恢复立即启动队列，避免等待期间丢失枪、炮轰、火箭等音效。
- 玩家名称通过不改写状态实时区域的公告路径投递，状态栏仍保留“名称、牌面、牌型”完整公开文本。
- 真人出牌后按原浏览位置附近静默恢复手牌光标，不再主动公告最左边剩余牌；AI 出牌不移动真人光标。
- 1.6 的叫分底牌显示与朗读实现保持不变，并继续由主窗口回归测试覆盖。
- 本次复用现有 Qt/UIA、读屏后端、QTimer 和 SoundService，不引入新依赖。

## 2026-07-31 版本1.6出牌姓名与叫分底牌修复

- 修复成功出牌时玩家自定义名称与牌型 WAV 的播报顺序，姓名公告完成后再启动同一音效队列。
- 叫分阶段增加八张公开底牌的持续可见无障碍文本，并合并新局底牌与真人叫分提示。
- AI 首先叫分或重新发牌时等待底牌公告完成后再行动，避免公告被后续流程覆盖。
- 本次继续复用 Qt/UIA、现有读屏后端、QTimer 和 SoundService 队列，不引入新依赖。

## 2026-07-30 版本1.3音效替换可靠性

- 根据真实键盘日志确认，自定义“轮到你”音效曾在替换成功后被连续回车触发的恢复操作删除；三个恢复确认框及替换、导入确认框现统一默认选择“否”。
- 替换成功提示不再被自动试听状态覆盖，并增加运行时自定义路径验证和不含用户源文件路径的诊断记录。
- 新增“导出当前完整音效包”，导出全部实际生效 WAV；保留原有模板导出和当前、分类、全部三种恢复功能。
- 使用 `D:\音效_轮到我出牌.wav` 直接覆盖项目默认 `card_four/din.wav`，旧默认文件未留备份；“轮到你”和总音效开启提示继续共用该文件。
- 本次复用现有 Qt、WinMM、QSaveFile 和 WAV 校验能力，未引入新组件或第三方库，因此无需新增 GitHub 依赖。

## 2026-07-30 音效分类与自定义替换

- 新增11类音效开关、逐文件试听和无障碍音效管理对话框。
- 自定义 PCM WAV 保存到 AppData 覆盖目录，原始资源保持不变并支持逐项、分类和全部恢复。
- 支持标准目录音效包的部分导入和模板导出；未引入第三方库，继续复用 Qt、WinMM 和现有 WAV 解析能力，避免新增解码器影响稳定性。
- 所有游戏音效改为带分类的统一队列请求，保留玩家四牌型播放完成后再播放“轮到你”的顺序保证。

## 2026-07-30 安装版、打赏与安全更新

- 检查了 GitHub 上的 Qt 自动更新组件与 Windows 安装方案。更新接口是本项目专用协议，现有 Qt Network、JSON、临时文件和 QCryptographicHash 已覆盖全部需求，因此不增加第三方运行时更新库，减少依赖和维护面。
- 安装器采用本机现有的开源 Inno Setup 6；使用固定 AppId、Program Files 独立目录、中文软件名和中文安装包文件名，支持以后原地覆盖升级。
- 更新客户端固定使用 product_key=feichuan_ai_doudizhu、platform=windows、channel=stable，只读取服务器返回的 download_url，并在执行 EXE 前强制校验 SHA-256。
- 增加“喜欢作者”、首次详细使用说明、每日自动检查开关、手动检查、忽略当前版本和关闭自动提醒。
- 首次安装的内部 QTextBrowser 虽然包含正文，但读屏只播报控件名称。现改为部署三份 UTF-8 BOM 的真实 TXT，并通过 Windows 默认文本程序打开；快捷键、玩法和详细说明各自独立。
- 修复左右浏览手牌时读屏追加“已选择”的回归。根因是 QListView 仍处于 ExtendedSelection，setCurrentIndex 会产生 Qt Selected 状态；现改为 NoSelection，所有当前索引移动使用 QItemSelectionModel::NoUpdate，游戏拿牌继续只由 HandListModel 管理。
- 最新 UI 修复后 test_app_settings、test_update_service、test_main_window_shortcuts 3/3 通过；未修改规则或 AI，因此没有再次运行 5000 局压力测试。

## 2026-07-28 王炸规则与完整组合音效

### 完成工作
- 新增一张小王加一张大王的“王炸”牌型，等级高于所有四张枪、低于五张及以上同点炸弹；两小王两大王统一称为“双王炸”。
- 本地 AI、远程 AI 合法动作候选和真实引擎流程均支持王炸；修复飞机主体同点剩余对子不能作为翅膀的问题。
- 新增牌型音效规划器，把整手牌拆成完整、有序的 WAV 清单；三张、三带一对、顺子、连对、飞机、飞机带对子和所有炸弹均逐段完整播放。
- 33、44、55 播放为“3、4、5、连对”；333、444 加对2、对3 播放为“3、4、飞机、对2、对3”。
- SoundService 按每个 WAV 的实际时长串行播放，同一出牌结果中的倍数、剩牌警告和胜负音效不再覆盖组合牌面。
- 从玩儿吧可见资源中的原版 king_bomb.ogg 转换出 PCM WAV，音效总数由136个增至137个。

### 定向验证
- 王炸分析器、比较器、文本、真实引擎和 AI 测试逐项通过。
- 飞机同点翅膀、组合文本、全部合法牌型音效路径和主窗口事件测试逐项通过。
- 新增 test_card_pattern_sound_plan，完整测试总数由13项增至14项。

## 2026-07-28 精确选牌、AI 模式和退出入口加固

### 完成工作
- 手牌内容未变化时不再重建模型和清空已拿起牌，连续选择多个点数组可以稳定累加。
- Ctrl+上光标改为原子整组选择，精确返回组内张数；重复选择同组保持原状态并播报“已拿起”。
- 选牌、放牌和出牌类快捷键忽略自动重复，移除重复的窗口级手牌 QShortcut，避免一次按键多次执行。
- 停止注册系统级全局热键，快捷键只在四人斗地主前台时全界面有效。
- 设置中的“高级”替换为“AI模式”；未配置认证时保持 AI 模式并使用本地高级 AI，配置完成后自动调用远程模型。
- 菜单栏新增顶层“退出(X)”，支持 Alt+X；叫分、出牌和暂停阶段退出时统一确认。
- 新增精确三张、四张、五张、多组选牌、三带一对、AI 设置迁移和 Alt+X 退出回归测试。
- 干净 Release 构建成功，13/13 自动测试通过。

## 2026-07-28 大模型 AI 接管、菜单与压牌实测修复

### 完成工作
- 新增“游戏 → AI 接管三个机器人”和“设置 → API 认证管理”。
- 认证界面支持 `Alt+N` 新增认证、`Alt+U` Base URL、受保护密钥、`Alt+F` 获取模型、模型选择和手动模型名。
- 同时支持 OpenAI Responses API 与 OpenAI-compatible Chat Completions API，DeepSeek 默认配置为 `https://api.deepseek.com/v1` 和 `deepseek-v4-pro`。
- API 密钥使用 Windows DPAPI 当前用户范围加密，普通设置、存档和日志不保存明文密钥。
- 三个机器人共用一个活动模型，大模型同时接管叫分和出牌；请求异步执行，30秒超时，失败自动降级本地 AI。
- 模型只接收当前机器人自己的手牌、公开状态和公开历史，并只能从程序生成的合法动作编号中选择。
- 修复 Alt 菜单、模态对话框和组合框中的方向键、回车被低级键盘钩子吞掉的问题。
- 真人出牌改为从唯一的已拿起牌快照生成命令，失败时播报实际提交牌面和具体原因。
- 新增用户精确场景回归：六个A压四个Q、三个7带一对6、一对2压一对9。
- 自动过牌设置范围扩展为3至1800秒。

### 验证
- Release 构建成功。
- 自动测试扩展为13项，包含凭据加密、远程API模拟、Alt菜单和用户精确出牌场景。
- 13/13测试通过，5000局本地AI自动对战继续通过。
- 已通过 `tools/build_release.ps1 -Deploy` 完成干净 Release 构建并覆盖唯一绿色版。
- Release 与 portable 主程序 SHA-256 一致，`Qt6Network.dll` 和 Windows TLS 后端均已部署，桌面快捷方式仍指向 portable。

## 2026-07-28 玩儿吧 card_four 全套音效与规则对齐

### 完成工作
- 已复制玩儿吧参考项目到 `D:\FourPlayerDoudizhu\reference\WanerbaScreenReaderVoice`，后续开发优先从项目目录内参考，不改动原目录。
- 已删除本项目原自生成的 18 个 WAV 音效，不再保留或部署旧音效。
- 已从玩儿吧四人斗地主 `card_four` 目录复制并转换 136 个音效文件，当前使用目录为 `resources/sounds/card_four`。
- `SoundService` 改为从程序目录旁的 `resources/sounds` 懒加载外部 WAV 文件，绿色版部署时必须复制该目录。
- 已按玩家性别目录和牌型映射播放玩儿吧语音：单张、对子、三张、顺子、双顺、三顺、飞机带翅膀、枪、炮、火箭、导弹、天炸、天尊。
- 已按用户给出的玩儿吧四人斗地主规则收紧牌型：不再允许三带一、飞机带单、四带二；三带只允许带一对，飞机只允许带同数量对子。
- 叫分逻辑改为随机起叫；无人叫分时立即重新发牌。
- 已按用户要求清理为单一绿色版策略：对外只保留 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`。
- 已删除旧 `build2` 构建目录和 build 目录内的主程序副本，避免出现多个可直接运行的四人斗地主版本。

### 验证
- `tools/sound_probe` 已对 `resources/sounds/card_four` 全部 136 个 WAV 执行 WinMM `waveOut` 和 `PlaySound` 检测，136/136 通过。
- Release 构建通过。
- Release 自动测试通过，8/8 成功。
- 已覆盖 `D:\FourPlayerDoudizhu\portable`，绿色版内 `resources/sounds/card_four` 为 136 个文件。
- 桌面快捷方式仍指向 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`。
- 项目内非 Qt 目录下只剩 `portable\FourPlayerDoudizhu.exe` 一个主程序。
- 桌面四人斗地主快捷方式数量为 1，开始菜单四人斗地主快捷方式数量为 0。

## 2026-07-28 启动提示和音效修复

### 完成工作
- 启动时不再弹出恢复自动存档的 Yes/No 系统确认框，避免读屏一打开程序就播报按钮提示。
- 新增 Alt 可访问的“设置”菜单，提供“恢复上次牌局”“音效开关”“测试音效”入口。
- 修复 SoundService 播放时互斥锁重入导致音效无法正常播放的问题。
- 统一牌局事件音效触发：发牌、叫分、地主、出牌、过牌、炸弹/火箭、剩牌警告、胜负、无效操作。
- 修正主窗口菜单、状态和读屏播报中的乱码中文。

### 验证
- Release 干净构建通过。
- Release 自动测试通过，8/8 成功。
- 已覆盖 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`。
- 桌面快捷方式仍指向 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`。

## 2026-07-28 绿色版覆盖与验证

### 完成工作
- ✅ Release 自动测试通过，8/8 成功。
- ✅ 使用 `D:\FourPlayerDoudizhu\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe` 重新覆盖部署绿色版。
- ✅ 当前绿色版位置为 `D:\FourPlayerDoudizhu\portable`。
- ✅ 桌面快捷方式 `C:\Users\apple007\Desktop\四人斗地主.lnk` 指向 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`。

### 注意事项
- `portable` 是后续人工测试入口，每次修改后优先覆盖这里。
- 现阶段继续使用绿色版测试，不再维护旧安装版；安装包等到功能和无障碍体验稳定后再考虑。
- `windeployqt` 提示缺少 `dxcompiler.dll`、`dxil.dll` 和未设置 `VCINSTALLDIR`，当前 Qt Widgets 主程序依赖已经部署完成，后续如加入图形特效或需要干净机验证时再专项处理。

## 2026-07-28 Qt 开发环境归入项目目录

### 完成工作
- ✅ 将 `D:\Qt` 整体移动到 `D:\FourPlayerDoudizhu\Qt`。
- ✅ 更新 `CMakePresets.json`，Qt 路径改为 `${sourceDir}/Qt/6.8.3/msvc2022_64`。
- ✅ 更新构建指南、交接说明和最终报告中的 Qt 路径。
- ✅ 将 `Qt/` 加入 `.gitignore`，防止误提交整套 Qt SDK。

### 说明
- `D:\FourPlayerDoudizhu\Qt` 是后续编译、测试和覆盖绿色版的关键开发环境，不要删除。
- 桌面快捷方式继续指向 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`，不受 Qt SDK 移动影响。

## 2026-07-28 玩儿吧快捷键基线同步

### 完成工作
- ✅ 确认快捷键基线改为参考用户既有“玩儿吧”四人斗地主项目。
- ✅ 将 F1 改为开战、停战、恢复入口。
- ✅ 将左右光标改为按组浏览，Shift+左右为单张浏览。
- ✅ 将上光标改为拿起当前牌，Ctrl+上光标拿起当前组，下光标放下全部拿起的牌。
- ✅ 将回车设为出牌，Ctrl+回车设为过牌。
- ✅ 将 Alt+大键盘1/2/3 设为查询下家、对家、上家的身份和剩余手牌。
- ✅ 增加 Alt+D 查看底牌，Alt+F 查看分数和倍数。
- ✅ 新增 docs/keyboard_spec.md 作为当前快捷键唯一规格。

### 修复问题
- 修复过牌失败时仍刷新状态并启动 AI 的问题。
- 停战支持叫分阶段和出牌阶段。

### 参考来源
- 本机参考项目：`C:\Users\apple007\AppData\Local\WanerbaScreenReaderVoice`
- 项目内参考副本：`D:\FourPlayerDoudizhu\reference\WanerbaScreenReaderVoice`
- 参考范围：四人斗地主目录结构、资源命名、帮助说明和用户确认的快捷键说明；`.luac` 字节码不做反编译。

## 2026-07-28 完整版本发布

### 完成工作
- ✅ 完善叫分对话框（BiddingDialog）
- ✅ 实现AI自动出牌（SimpleAi + 定时器）
- ✅ 完善主窗口（键盘快捷键、状态显示）
- ✅ 实现GameState序列化（JSON格式）
- ✅ 完善SaveRepository（存档/读档）
- ✅ 实现自动保存功能
- ✅ 构建Release版本
- ✅ 创建过安装包（后续已停止维护旧安装版）
- ✅ 后续已改为绿色版测试目录 `D:\FourPlayerDoudizhu\portable`
- ✅ 创建桌面快捷方式
- ✅ 桌面快捷方式后续已改为指向绿色版
- ✅ 编写完整交接文档（handover.md）
- ✅ 编写用户手册（user_manual.md）

### 技术细节
- Release版本大小：200KB（主程序）
- 安装包总大小：约58MB（包含Qt依赖）
- 测试通过率：100%（8/8）
- 构建时间：Debug 10秒，Release 35秒

### 当前绿色版信息
- 绿色版路径：D:\FourPlayerDoudizhu\portable
- 桌面快捷方式：C:\Users\apple007\Desktop\四人斗地主.lnk
- 桌面快捷方式目标：D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe

### 当前状态
旧安装版已改为清理对象，后续人工测试使用 `D:\FourPlayerDoudizhu\portable` 绿色版。

### 待完成功能
1. 设置界面
2. 统计系统
3. StandardAi增强
4. 三读屏实测

## 2026-07-27 项目初始化

### 完成工作
- ✅ 环境检测（MSVC、CMake、Ninja、Git）
- ✅ 安装Qt 6.8.3 MSVC2022 x64
- ✅ 创建项目目录结构
- ✅ 实现核心牌模型（Card、Deck、Hand、Player）
- ✅ 实现规则层（RankHistogram、PatternAnalyzer、PatternComparator、ScoringEngine）
- ✅ 实现引擎层（GameEngine、GameState、TurnManager）
- ✅ 实现AI层（SimpleAi、StandardAi、LegalMoveGenerator）
- ✅ 实现UI层（MainWindow、HandListModel、PlayerStatusModel）
- ✅ 实现无障碍层（AccessibilityService、AnnouncementScheduler）
- ✅ 实现持久化层（SaveRepository、LogService）
- ✅ 编写8个自动化测试（100%通过）
- ✅ 编写核心文档（rules_spec、architecture、accessibility_spec）

### 技术栈
- C++20
- Qt 6.8.3
- CMake 4.2.3
- Ninja 1.12.1
- MSVC 19.50.35728

### 项目统计
- 源代码文件：102个
- 测试文件：8个
- 文档文件：9个
- 代码行数：约15,000行
# 开发日志

## 2026-08-02 版本3.0正式发布

- 版本号升为3.0；保留用户已手工验收的“A读尖/对尖”、F1首次直接暂停且不误读窗口标题、最左手牌不读列表位置和未选择状态三项修复，本轮没有扩大运行逻辑修改。
- 清理了自动测试中已删除的远程AI、API凭据对话框和第三方读屏后端引用，并将天尊、对子和拿放牌静默断言更新为当前正式行为；没有恢复任何旧功能。
- 全目标Release构建成功，CTest 14/14通过；其中中级机器人500局、大师级机器人500局，共1000局全部完成，无非法出牌、超出动作上限或结算不平衡。UI快捷键套件36/36通过，并额外连续复验5次全部通过。
- Release、安装阶段和Program Files正式EXE均为676,352字节，SHA-256为`C5EB7B84A7E353037657B5EAD75B6E9BD674B3BF2EA252AB2CCDE4D528186AAB`。
- 安装阶段为`build\installer-stage-3.0-final`，含162个默认WAV；6份唯一TXT文档在`docs`和`resources\docs`各一份，全部为UTF-8 BOM；不含NVDA Controller、Qt6Sql、Qt6Concurrent或sqldrivers。
- 项目和D盘根目录安装包均为57,015,031字节，SHA-256为`BEFCBBD45BC6A6396F0040F86FC1C02E9EC1D410AEBF335EFF69BB28F9469388`。使用最终安装包静默覆盖成功，DisplayVersion为3.0，桌面快捷方式正确，安装后游戏进程为0。
- 已发布到`https://update.327802521.xyz`的`feichuan_ai_doudizhu/windows/stable`，发布记录ID为34。2.9查询返回可更新到3.0，3.0查询返回无更新；从`download_url`下载的EXE文件头、大小和SHA-256均与本地一致，服务器按规则只保留3.0正式记录。

## 2026-07-28 启动音效、F5 设置、自动过牌和 AI 接口

### 完成工作
- 启动主窗口后延迟播放 `card_four/BeginGame.wav`，F1 开战后发牌继续使用 `card_four/start.wav`。
- 新增 F5 打开设置选项；设置支持真人超时自动过牌，范围 3 到 300 秒，默认开启 30 秒。
- 设置中预留机器人等级：初级、中级、高级、大师级。
- AI 定时器改为单次调度，并给叫分对话框增加防重入保护，降低 F1 开战后未响应风险。
- 修复 `SoundService` 的 waveOut 句柄清理和停音持锁问题。
- 新增 AI 接口基类与等级枚举，机器人叫分在牌力评分基础上加入可控随机。
- 新增超时强制过牌入口：手动 Ctrl+回车仍不能在领出时过牌，超时自动过牌可按用户要求跳过。

### 验证
- Release clean 构建通过。
- 自动测试套件新增 `test_app_settings`，当前测试总数为 9 个。
# 开发日志

## 2026-07-28 稳定性加固、键盘焦点修复和实机截图试玩

### 完成工作
- `SoundService` 增加音效相对路径解析缓存，避免每次播放都在 UI 线程重复查文件系统。
- 启动时的自动存档检查改为窗口显示后延迟执行，避免主窗口构造阶段同步检查影响响应。
- 开战后的自动保存改为排队到事件循环后执行，减少开局处理链里的同步写盘。
- 修复 AI 叫分结束直接进入出牌阶段时，如果当前出牌者是 AI 没有继续调度 AI 出牌的问题。
- 扩大主窗口键盘事件覆盖范围，手牌列表视口和主要按钮都接入快捷键处理。
- 进入真人出牌回合时自动把焦点放回手牌列表。
- 新增窗口级快捷键兜底：上光标拿牌、Ctrl+上光标拿组、下光标放下、回车出牌、Ctrl+回车过牌、H 提示、P 过牌。

### 验证
- Release 构建通过。
- Release 自动测试 9/9 通过。
- 已覆盖部署到 `D:\FourPlayerDoudizhu\portable`。
- 截图实测绿色版：键盘菜单开战、真人叫 3 分、上光标拿牌、回车出牌成功，手牌从 33 张变为 32 张，进程保持响应。

## 2026-07-28 快捷键实测修复和 Alt 查询兜底

### 完成工作
- 修复左右光标、Home/End、Shift+左右光标只在手牌列表焦点下生效的问题，改为主窗口级手牌浏览处理。
- F1/F5 菜单动作显式设置为应用级快捷键，减少焦点差异。
- 无障碍播报同步显示到底部状态栏，便于读屏之外的截图验证。
- 增加 Windows 原生 Alt 组合键兜底：Alt+大键盘 1/2/3、Alt+D、Alt+F；Alt+数字按 Windows 大键盘虚拟键处理，避免小键盘混淆。
- 新增 `test_main_window_shortcuts`，覆盖 F5、F1、数字 3 叫分、方向键浏览、拿牌、出牌、Ctrl+回车过牌、Alt 查询。

### 验证
- Release 构建通过。
- Release 自动测试 10/10 通过。
- 已覆盖部署到 `D:\FourPlayerDoudizhu\portable`。
- 截图实测绿色版：F5 打开设置，F1 进入叫分，数字 3 叫分后进入出牌阶段，左右/Home/End/Shift+右浏览生效，上/Ctrl+上/下/回车生效，Alt+1、Alt+D、Alt+F 状态栏查询生效，进程保持响应。

## 2026-07-28 快捷键基线收紧和手牌短播报

### 完成工作
- F1 保持开战、停战、恢复，不改变既有正确行为。
- F11 播报当前轮到谁行动，并区分叫分、出牌、停战、结束等阶段。
- F12 改为播报最后一次实际出牌；即使后续有人过牌，也保留上一手实际出的牌。
- 下光标改为按拿起顺序逐张放下牌，Ctrl+下光标改为一次放下全部拿起的牌。
- 移除窗口级 H 提示、P 过牌旧兜底快捷键；键盘出牌/过牌以回车、Ctrl+回车为准。
- 增加 Windows 原生 `WM_KEYDOWN` 兜底，覆盖 F11/F12、叫分数字、手牌浏览、拿牌、放牌、出牌和过牌，降低 Qt 焦点链差异造成的热键不稳定风险。
- 手牌浏览无障碍文本改为短播报，只读牌名和必要的同点数张数，不再读第几张、总张数、已拿起或未拿起。
- `test_main_window_shortcuts` 扩展覆盖 F11、F12、下光标逐张放下、Ctrl+下光标全部放下。

### 验证
- Release 构建通过。
- Release 自动测试 10/10 通过。
- 已覆盖部署到 `D:\FourPlayerDoudizhu\portable`。
- 使用绿色版截图验证：F1 开战、3 分叫分进入出牌阶段、F11 查询当前行动者、上光标拿起、下光标放下、Ctrl+下光标放下全部、F12 查询最后实际出牌、倒计时显示、进程保持响应。
- `windeployqt` 在 VS 环境中运行后已更新 `dxcompiler.dll`、`dxil.dll` 和 `vc_redist.x64.exe`；没有再出现 `VCINSTALLDIR` 未设置。

## 2026-07-28 玩儿吧格式无花色朗读和前台键盘钩子

### 完成工作
- 手牌浏览播报改为玩儿吧格式：`1张3`、`2张4`、`3张7`、`1张钩`、`1张圈`、`2张k`。
- 牌相关播报统一只读张数和点数，不读花色，不读选中或未选中状态。
- 拿起、放下、底牌、F12 最后一次实际出牌等路径改用统一无花色文本格式。
- 手牌模型不再通过 Qt 勾选角色向读屏暴露选中状态，内部拿牌状态仍用于出牌逻辑。
- 新增 Windows 前台低级键盘钩子兜底；只在四人斗地主窗口前台时处理快捷键，避免影响其他软件。
- F11、F12、Alt+大键盘1/2/3、Alt+D、Alt+F 额外使用 Windows `RegisterHotKey` 注册系统热键兜底，解决读屏环境下函数键和 Alt 查询被焦点链吞掉的问题。
- 新增 `test_card_text_formatter`，并扩展 `test_main_window_shortcuts` 检查花色和选中状态不会出现在状态栏同步播报中。

### 验证
- Release 构建通过。
- Release 自动测试 11/11 通过。

## 2026-07-28 F11/F12 和 Alt 查询实测修复

### 完成工作
- 针对用户反馈“F11/F12 仍没反应、Alt+大键盘 1/2/3 没有任何信息”，增加应用级 Qt 事件过滤和 `QAbstractNativeEventFilter` 原生消息过滤，覆盖主窗口、手牌列表、按钮、菜单和状态栏焦点差异。
- F11、F12、Alt+大键盘1/2/3、Alt+D、Alt+F 继续保留 `RegisterHotKey` 系统热键兜底，并记录注册成功的热键 ID；注册失败会写入 Windows debug 输出，避免静默失败。
- Alt+1/2/3 原生路径改为按 Windows 大键盘虚拟键 `1/2/3` 识别，不再依赖固定扫描码；小键盘数字仍不会误触发。
- 低级键盘钩子只处理真实前台按键，忽略 QtTest/SendKeys 注入键，避免自动测试被钩子截获后卡在设置对话框。
- `ShortcutOverride` 事件只占用快捷键，不执行业务逻辑，避免弹窗在快捷键预处理阶段打开。
- 查询类播报会同步更新状态栏文本、状态栏可访问名称和可访问描述，并发送 `QAccessibleAnnouncementEvent`，让读屏不依赖焦点刚好停在状态栏。
- 手牌模型的 `DisplayRole`、`CardTextRole` 和 `AccessibleTextRole` 全部改为玩儿吧格式无花色文本，防止读屏从显示角色读出花色。
- `test_main_window_shortcuts` 增加手牌显示角色不含花色/选中状态检查，并修复 F5 设置弹窗关闭逻辑。

### 验证
- Release 构建通过。
- Release 自动测试 11/11 通过。
- 已覆盖部署到 `D:\FourPlayerDoudizhu\portable`，绿色版 exe 时间戳与 Release 构建产物一致。
- UI Automation + 键盘实测绿色版未开局：F11 显示“牌局尚未开始”，F12 显示“暂无出牌记录”，Alt+1/2/3 分别显示下家、对家、上家信息。
- UI Automation + 键盘实测牌局中：F11 显示“轮到玩家一出牌”，F12 显示“最后出牌的是玩家四，出的牌是1张钩”，Alt+1/2/3 显示三名玩家身份和剩余手牌，Alt+D 显示无花色底牌，Alt+F 显示基础分和倍数。
- 截图保存为 `D:\FourPlayerDoudizhu\portable\verification_hotkeys_20260728.png`，进程保持响应。

## 2026-07-28 完整可玩 V1 规则、AI 与构建稳定性升级

### 完成工作
- 定位 F1 无反应和退出崩溃根因：Ninja 未正确识别本地化 `/showIncludes` 输出，导致 `main.cpp.obj` 没有随 `MainWindow` 头文件重编译并发生对象大小不一致。
- CMake 固定正确的 MSVC include 前缀，Release 构建输出不再出现海量 include trace；主窗口改由 UI 工厂内部创建。
- 修复前三家不叫时错误提前重新发牌，确保第四家仍有叫分机会。
- 接入胜负结算、春天、反春、最终倍数和四家零和分数变化，并写入版本化存档。
- 合法动作生成器覆盖规则允许的全部普通牌型和六级炸弹。
- AI 改用不包含其他玩家隐藏手牌的 `AiObservation`；四档策略加入合法随机、拆牌代价、最小压制、保留炸弹、地主危险、农民配合和残局评估。
- 炸弹播报改为点数加炸弹名且不报张数，F12 同步使用该格式。
- 自动存档覆盖真人和 AI 的叫分、出牌、过牌与超时操作；统计记录总局数、胜负、角色局数、累计分和最高倍数。
- 新增 `tools/build_release.ps1`，执行全新配置、干净构建、完整测试及可选绿色版部署。

### 验证
- Release 干净构建成功，编译输出不再显示 include trace。
- 自动测试 11/11 通过。
- 自动对战 5000 局全部在 1000 个动作内合法结束，全部结算为零和。

## 2026-07-28 单张拿牌、完整出牌播报和查询优化

### 完成工作
- 上光标逐张拿牌时焦点保持在刚拿起的实体牌，每次只播报该牌点数。
- AI 出牌改为先刷新界面再发布完整牌面公告，并按文本长度延迟下一家 AI 行动。
- `Alt+大键盘1/2/3/4` 改为查询绝对玩家编号，玩家一标识为真人自己，`Alt+F4` 继续退出。
- 自动过牌秒数控件启用按住加速，单按仍每次调整 1 秒。

### 验证
- 标准干净 Release 构建成功，13/13 自动测试通过。
- 最新绿色版已部署到 `D:\FourPlayerDoudizhu\portable`。
- Release 与 portable EXE SHA-256 一致：`7374812FB28E3A9E6EBC821896C61D16C1E7BAD7BEF067A9D05B489B41DED441`。
- 绿色版启动 4 秒后仍保持响应；Qt Network、TLS 插件和 136 个音效均已部署。

## 2026-07-28 大模型真实使用确认与静默运行

### 完成工作
- 删除每次远程叫分和出牌前的“正在使用大模型思考”状态栏文字及读屏公告，不增加提示音。
- 每次启用 AI 模式后，只在首次收到合法远程出牌响应时提示一次“已经用大模型思考出牌”；请求发出、网络失败或非法返回均不能触发确认。
- 首次确认后延迟约 1.6 秒执行动作，防止确认提示被出牌公告立即覆盖。
- 远程失败改为静默降级本地高级 AI，不在牌局中反复播报失败原因。
- 诊断日志新增 remote_ai 事件，区分 request_started、response_accepted、fallback_local、response_discarded 和 confirmed_action_discarded，不记录密钥、提示词或隐藏手牌。
- 切换 AI 模式或保存新的活动认证时重置一次性确认状态。

### 验证
- 标准干净 Release 构建成功，13/13 自动测试通过。
- 最新绿色版已部署到 D:\FourPlayerDoudizhu\portable。
- Release 与 portable EXE SHA-256 一致：3AAEE6A31B9E02364639A43DB18F45166124F2FA7EC49B779906D730E9E293C2。
- Qt6Network.dll、qschannelbackend.dll 和 136 个 WAV 音效已确认。
## 2026-07-29 GitHub open-source-first rule

- Before adding any tool, component, library, or substantial general-purpose capability, search GitHub for a mature open-source option first.
- Evaluate functional fit, license compatibility, maintenance activity, security, Windows/Qt compatibility, accessibility impact, package size, and deployment cost before deciding to reuse, adapt, or implement locally.
- Prefer a suitable mature project to reduce duplicate work and remote-model token usage, but do not add a risky, oversized, unmaintained, or unclear-license dependency merely to claim open-source reuse.
- Record the candidates and the reason for accepting or rejecting them in this development log. Keep this rule in every future detailed handover.
- This round evaluated miniaudio and FFmpeg. The application keeps the stable WinMM/PlaySound short-sound path to minimize regression risk. The already-installed FFmpeg was used only for offline OGG-to-PCM-WAV conversion; the runtime and portable package do not depend on FFmpeg.

## 2026-07-29 Accessibility and audio refactor in progress

- Source changes now cover three-menu Tab cycling, name-only F11 output, sound-only pass handling, exclusion of picked cards from browsing, layered Escape behavior, persistent result browsing, player voice selection, and phase-based background music.
- The complete `test_main_window_shortcuts` run passes 26/26; the focused Tab-menu run passes 3/3.
- The full 15-test CTest suite, clean Release build, deployment, and post-deployment smoke test are still pending. The current portable package must not be treated as updated.

## 2026-07-29 Accessibility and audio refactor completed

- Added the Help menu gameplay explanation and synchronized the built-in shortcut help with the three-menu Tab cycle, name-only F11 behavior, picked-card browsing exclusion, layered Escape behavior, and result-page flow.
- Added automated validation for all three background-music WAV files: each file must exist, use RIFF/WAVE PCM format with 16-bit samples, contain readable audio data, and have a positive duration.
- Fixed `QSqlDatabase` cleanup on error and empty-training paths so connections are removed only after queries and database handles are released. Targeted UI runs no longer report the connection-in-use warning.
- Restored the local AI stress test to 5000 complete games with a 1000-action ceiling per game and a zero-sum score assertion. The CTest and QtTest timeouts are 15 minutes so the stress test is not killed by the framework's former five-minute limit.
- The number 5000 is only a development stress-test iteration count. The shipped single-player game has no game-count limit and can start unlimited new rounds.
- Clean Release validation passed 15/15 tests. The 5000-game test passed in about 6 minutes 30 seconds, and `test_main_window_shortcuts` passed five additional consecutive runs.
- `tools/build_release.ps1 -Deploy` completed successfully and passed 15/15 again before replacing the single portable package.
- Release and portable executable SHA-256 match: `96BBCC91DB23A2E259799821ECB703FFB1415CA0562DB9EAAEE0E824203E7AE0`.
- Portable contains 154 `card_four` WAV files, three PCM 16-bit music WAV files, Qt6Network, Qt6Sql, `qsqlite`, and the Schannel/certificate TLS backends. The desktop shortcut still targets the single portable executable.
- Portable launch from the desktop shortcut, process path, responsiveness, three UIA menu names and menu-item roles, main-window Escape survival, graceful UIA close, legacy autosave absence, and clean recent logs were checked without starting a remote-AI game.
- External Windows UIA does not reliably expose Qt's active top-menu item during synthetic Tab input, and modal settings invocation blocks the external caller. Exact Tab order, settings persistence, voice/music controls, Escape, result browsing, F11, pass silence, and picked-card browsing are therefore supported by the in-process automated tests; real NVDA speech timing, audible voice quality, and music/effect mixing remain explicit user acceptance items.
- The permanent GitHub open-source-first rule remains mandatory: before adding a tool, component, library, or substantial reusable capability, search GitHub first, prefer a suitable maintained and license-compatible project, and record the adoption or rejection decision here and in every detailed handover.

## 2026-07-29 F1 menu-dismissal release fix

- A real portable report showed that pressing F1 after Tab navigation could start the round while the custom top-level menu popup and menu-bar focus remained visible, covering the bidding interface.
- Root cause: F1 is application-scoped, while the three top-level menus are opened with explicit `QMenu::popup()`. Triggering the action did not automatically dismiss those custom popups, and programmatic focus could then remain on the menu bar.
- `MainWindow::dismissMenusForGameAction()` now closes every menu owned by the window, clears the menu-bar active action and stored Tab index, activates the main window, and returns focus before the F1 battle-state transition.
- Added `testF1ClosesOpenMenuBeforeStartingBattle`: it opens the Settings popup, presses F1, verifies the engine enters bidding, verifies no popup or active menu remains, and verifies focus is not left on a menu or menu bar.
- The complete UI suite is now 27/27 and passed five consecutive targeted runs. The clean deployment suite passed 15/15, including the 5000-game local-AI stress test.
- Portable was replaced again. Release and portable SHA-256 match: `24493337BB3F52D4F3882E6A24842852B0EAC74FBA014402C5967D5F72E3A166`.
- Post-deployment direct Windows-message smoke test reproduced the user's order: the Settings popup was visible before F1 and absent after F1; the process remained responsive. Remote AI was temporarily disabled for the smoke test and the original settings file was restored byte-for-byte afterward.
# 2026-07-31 1.4 screen-reader compatibility and diagnostics

- Replaced the placeholder speech backends with official runtime integrations for NVDA Controller Client, ZDSRAPI x64, and Baoyi byctrl x64. No SAPI, OneCore, or built-in TTS fallback is present.
- NVDA Controller Client 2026.1.1 was downloaded from NV Access's official stable release directory. The packaged x64 DLL is 262,808 bytes with SHA-256 `2FE60CF00BE929AAE32E95C1E1507A20ADA4902C8FEC273B3CC2D3BF5472932A`; its LGPL 2.1 license is deployed beside the application.
- ZDSR and Baoyi use their official installed x64 DLLs so users receive the API version matching their installed screen reader. Missing or inactive readers remain silent rather than falling back to a system voice.
- The hand list now accepts real keyboard focus and emits focus, selection, and name-change accessibility events for the current card while preserving the model's separate picked-card state and existing keyboard shortcuts.
- Added Help > Copy diagnostic information. It copies a one-megabyte-capped, allow-listed report containing application/component integrity, Windows and Qt information, reader backend status, focus state, the human-visible game state, and up to 200 recent sanitized trace events.
- The report excludes API credentials, authorization data, remote-AI payloads, other players' hidden cards, custom player names, and raw user-profile paths.
- GitHub candidate review: Qt's existing UIA implementation and NVDA's official Controller Client were adopted. Tolk was rejected because it is no longer actively developed, does not support ZDSR or Baoyi, and includes a SAPI path forbidden by the product requirements. Accessibility Insights for Windows remains a development-only inspection tool and is not shipped.
- Added a user-facing UTF-8 BOM changelog document and a keyboard-accessible Help menu action so release recipients can review the major 1.4 and 1.3 changes without opening the source tree.
