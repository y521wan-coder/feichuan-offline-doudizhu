# 四人斗地主项目交接说明

## 2026-08-03 版本3.0长期内测当前状态（优先于下方历史记录）

- 用户已确定游戏与训练中心短期内均保持3.0，只继续打磨发牌随机性、三档机器人强度、稳定性和标准无障碍，不新增玩法，不再次发布服务器。
- 正式发牌不再混合时钟种子，直接使用Windows系统加密随机源、拒绝采样和无偏Fisher-Yates洗牌；显式种子仅供测试与训练，使用跨编译器稳定算法，全部不叫后的重发牌仍可复现。
- 初级、中级、大师级统一由StandardAI引擎执行，决策预算依次为150、450、1000毫秒，并分别加载三级模型。AI只看自己的手牌及公开底牌、叫分、出牌、过牌、身份和余牌，完整隐藏牌只留给裁判引擎。
- 活动三级清单为AppData下`models\active_tiers.json`。清单、路径边界、文件SHA-256、模型内容哈希、规则指纹、层级名或资格任一失败时，整套回退内置三级逻辑并显示一次标准Qt可访问警告。
- 训练晋升顺序固定为：新模型成为大师、旧大师变中级、旧中级变初级、旧初级及旧集合删除。游戏运行中收到本机训练安装请求时会安全放弃当前局并正常退出，不接受强制结束。
- 已完成Release全目标构建和14/14 CTest，包含中级500局、大师500局稳定性测试及新增随机、重发牌、公开历史、模型完整性测试。未启动正式游戏，争渡与纯键盘人工验收由用户在长期内测中继续。
- 下方2.9、旧单大师模型及远程AI说明均为历史资料，不代表当前功能。最终本地安装产物校验以根目录`交接说明.txt`最新3.0补充为准。

## 2026-08-02 版本2.9 当前本地修订（历史）

- 远程大模型接管已从源码、设置、菜单、API 控件、诊断、学习链、测试目标和构建依赖中删除；下方旧条目仅保留为历史记录，不代表当前功能。
- 机器人只保留初级、中级、大师级三档，三名电脑玩家全部使用本地逻辑，不读取 API 凭据或训练库，不发送远程决策请求。
- 覆盖安装保留 AppData 用户文件但不再读取旧 `api_credentials.json` 和 `ai_training.sqlite`，安装目录会清理不再需要的 Qt SQL、Concurrent 和 qsqlite 文件。
- 诊断格式为 6；版本按用户本轮要求仍为 2.9。安装后由用户自行手动测试，开发侧不启动软件或牌局。
- 当前正式安装版 EXE SHA-256 为 `CD3ECE4E29F600C360378A6868697F9855E010871B3156425C6A56CAB4E83CCE`；项目 dist 安装包 SHA-256 为 `6EEDAEF1661D9E56FF9B7DDA22414288CDA01F6C3ED7575590843C07B852C916`，静默覆盖安装已成功且软件未启动。

## 2026-08-02 版本2.9交付补充（历史）

- 远程 AI 已精简为单一用户选择模型；DeepSeek 默认 Flash 非思考，使用 compact_v3、24 个候选、64 输出 token、8 秒绝对超时、无自动重试和本地高级 AI 降级。
- API 管理新增仅由用户点击的模型测试；本轮没有读取凭据或调用真实 API。
- 标准无障碍仍只有 Qt/UIA；不内置读屏或语音，不加载 NVDA、争渡、保益接口 DLL。手牌不再人为发送 Focus、Selection、NameChanged 三连事件。
- 键盘重复去重和诊断有界批处理已完成，诊断格式为 5。版本、产物和人工验收详情以根目录 `交接说明.txt` 与 `docs/development_log.md` 顶部为准。

## 2026-07-28 最新规则与音效修复
- 一张小王加一张大王为“王炸”，可压所有四张枪；五张及以上同点炸弹和“天尊”可以压王炸。
- 两张小王加两张大王对外统一播报“天尊”。
- 连对、飞机及带对子等组合按实际点数拆成完整 WAV 序列播放，不再只播放第一组或单一牌型音效。
- 飞机允许主体同点剩余对子作为翅膀，例如333444加对2对3合法并完整播报。
- 自动测试新增 test_card_pattern_sound_plan，当前完整测试总数为14项；运行音效为137个 WAV。

## 2026-07-28 最新补充：稳定选牌、AI 模式和退出

- Ctrl+上光标现在按一次只执行一次，精确拿起当前完整点数组；多组选择累加，同组重复拿起不改变数量。
- 手牌内容未变化时刷新界面不会清空选择，三个相同点数加一个对子可稳定组成三带一对。
- 设置中的“高级”已经替换为“AI模式”；AI 模式未配置时保留选择并使用本地高级 AI，认证可用后自动远程接管。
- 菜单栏新增顶层“退出(X)”，Alt+X、Ctrl+Q 和游戏菜单退出共用同一安全退出流程。
- 快捷键仅在软件前台时全界面有效，不再注册会占用其他软件按键的系统全局热键。
- 干净 Release 构建及 13/13 自动测试已通过；最终交付仍以 `portable` 为唯一测试版本。

## 2026-07-28 上一状态：外部大模型接管

- 已新增大模型接管三个机器人，三家共用当前认证和模型，同时负责叫分与出牌。
- 已支持 OpenAI Responses API 和 OpenAI-compatible Chat Completions API；DeepSeek 默认 Base URL 为 `https://api.deepseek.com/v1`。
- API 密钥通过 Windows DPAPI 加密保存在应用数据目录，不得写入源码、日志、设置或交接文档。
- 大模型只能从 `LegalMoveGenerator` 给出的合法动作编号中选择，最终仍由 `GameEngine` 验证；失败自动使用本地 AI。
- 已修复 Alt 菜单键盘导航，并增加六个A压四个Q、三个7带一对6、一对2压一对9的精确回归。
- 最终干净 Release 构建与13/13自动测试通过，已部署到唯一绿色版；仍需用户使用新生成的有效密钥完成真实联网验收，并用物理键盘确认 Alt 菜单体验。

### 新会话优先事项
- 先让用户物理键盘验证 `Alt+G/S/H` 和 API 对话框 `Alt+N/U/F`，不要把自动 SendKeys 当作最终验收。
- 聊天中公开过的 DeepSeek 密钥已经暴露，未被程序或测试使用；必须撤销后通过游戏内受保护编辑框输入新密钥。
- 使用真实模型完成获取模型、叫分、出牌和断网降级测试，确认失败时牌局立即继续使用本地 AI。
- 修改源码后运行 `powershell -ExecutionPolicy Bypass -File tools/build_release.ps1 -Deploy`，保持唯一运行版本为 `portable`。

## 2026-07-28 前序状态
- F1 故障根因已修复：MSVC/Ninja 头文件依赖本地化识别错误导致旧对象布局污染；现使用正确 include 前缀、干净构建脚本和 UI 工厂分配主窗口。
- 四人叫分、全部合法牌型、六级炸弹比较、最终结算和版本化存档已经接通。
- 四档 AI 改为只读取自己的手牌和公开信息；5000 局自动对战全部合法完局并保持零和结算。
- 炸弹播报采用“点数 + 名称”，不读张数；天尊只读“天尊”。
- 标准构建部署入口：`powershell -ExecutionPolicy Bypass -File tools/build_release.ps1 -Deploy`。

**文档版本：** 1.0
**交接日期：** 2026-07-28
**项目路径：** D:\FourPlayerDoudizhu
**当前阶段：** 外部大模型接管、规则回归和绿色版部署已完成，等待用户进行物理键盘、读屏和新密钥联网验收

---

## 一、项目概述

四人斗地主是一款 Windows 桌面无障碍游戏，使用 C++20、Qt 6.8、CMake 开发。支持 NVDA、保益、争渡三种读屏软件，提供完整的键盘操作和语音播报。

### 核心特性
- 两副牌 108 张，8 张底牌，每人 25 张，地主 33 张
- 单轮 1-3 分叫分制
- 简单 AI 和标准 AI，已预留初级、中级、高级、大师级接口
- 完整的无障碍支持
- 纯键盘操作
- 音效系统（已集成）

### 本地参考资料
- 原玩儿吧目录：`C:\Users\apple007\AppData\Local\WanerbaScreenReaderVoice`
- 项目内参考副本：`D:\FourPlayerDoudizhu\reference\WanerbaScreenReaderVoice`
- 已确认参考副本：2388 个文件，约 150 MB
- 四人斗地主相关重点目录：
  - `reference/WanerbaScreenReaderVoice/src/script/tables/card_four`
  - `reference/WanerbaScreenReaderVoice/src/app/data/help/card_four`
  - `reference/WanerbaScreenReaderVoice/res/sounds/card_four`
  - `reference/WanerbaScreenReaderVoice/res/music/card_four`
  - `reference/WanerbaScreenReaderVoice/res/images/card_four`
- 说明：玩儿吧 `src` 下多为 `.luac` 字节码文件，不做二进制反编译；后续优先参考可读帮助、资源命名、目录结构和用户实测行为。

### 版本保留原则
- 这台电脑对外只保留一个绿色版：`D:\FourPlayerDoudizhu\portable`
- 桌面只保留一个 `C:\Users\apple007\Desktop\四人斗地主.lnk`，目标必须是 `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`
- 不再创建或保留旧安装版、备份版、测试版、开始菜单快捷方式
- 源码、`Qt/`、`reference/` 和 `build/` 属于开发资料；用户测试入口只认 `portable`
- 当前已删除旧 `build2` 和 build 目录内的主程序副本；项目内非 Qt 目录下只剩 `portable\FourPlayerDoudizhu.exe` 一个主程序

---

## 二、当前完成状态

### 已完成模块

#### 1. 核心游戏逻辑 ✅
- **位置：** `src/core/`
- **状态：** 完整实现并通过测试
- **包含：**
  - 牌模型（Card, Deck, Hand）
  - 牌型分析器（PatternAnalyzer）
  - 牌型比较器（PatternComparator）
  - 计分引擎（ScoringEngine）
  - 游戏引擎（GameEngine）
  - 回合管理器（TurnManager）
  - 文本格式化（CardTextFormatter, GameTextFormatter）

#### 2. AI 系统 ✅
- **位置：** `src/ai/`
- **状态：** 简单 AI 完成，标准 AI 基础框架
- **包含：**
  - 合法动作生成器（LegalMoveGenerator）
  - 叫分策略（BiddingStrategy）
  - 简单 AI（SimpleAi）
  - 标准 AI（StandardAi）- 基础实现
  - AI 接口基类和等级枚举，后续可接入训练模型
  - 机器人叫分已在牌力评分基础上加入可控随机
  - 提示服务（HintService）

#### 3. 无障碍服务 ✅
- **位置：** `src/accessibility/`
- **状态：** 完整实现
- **包含：**
  - 无障碍服务（AccessibilityService）
  - 播报调度器（AnnouncementScheduler）
  - 无障碍文本生成（AccessibilityText）
  - 后端实现：
    - Qt 无障碍后端（QtAccessibilityBackend）
    - 保益后端（BoyCtrlBackend）
    - 争渡后端（ZdsrBackend）
    - 空后端（NullBackend）

#### 4. 持久化层 ✅
- **位置：** `src/persistence/`
- **状态：** 完整实现
- **包含：**
  - 数据路径管理（DataPaths）
  - 设置仓库（SettingsRepository）
  - 存档仓库（SaveRepository）
  - 统计仓库（StatisticsRepository）
  - 回放仓库（ReplayRepository）
  - 日志服务（LogService）

#### 5. UI 框架 ✅
- **位置：** `src/ui/`
- **状态：** 主窗口完成，对话框部分完成
- **包含：**
  - 主窗口（MainWindow）- 完整实现
  - 手牌列表模型（HandListModel）
  - 玩家状态模型（PlayerStatusModel）
  - 手牌视图（HandView）
  - 游戏状态控件（GameStatusWidget）
  - 玩家状态控件（PlayerStatusWidget）
  - 叫分对话框（BiddingDialog）
  - 设置对话框（SettingsDialog）：F5 打开，支持超时自动过牌和机器人等级设置
  - 结果对话框（ResultDialog）
  - 关于对话框（AboutDialog）

#### 6. 音效系统 ✅（刚完成）
- **位置：** `src/ui/sound_service.h/.cpp`
- **状态：** 完整实现并集成
- **包含：**
  - 音效服务（SoundService）- 基于 Windows waveOut API
  - 玩儿吧四人斗地主 `card_four` 全套音效，已从 ogg 转为 PCM wav
  - 当前资源目录：`resources/sounds/card_four`
  - 当前音效文件数：136 个
  - 原自生成 WAV 音效已删除，不再保留或部署
- **音效覆盖范围：**
  - 开局、发牌、叫分、不叫、过牌、选牌、放下牌、错误操作、胜负
  - 单张、对子、三张、顺子、双顺、三顺、飞机带翅膀
  - 枪毙、炮轰、火箭、导弹、天炸、天尊
  - boy/girl 两套牌面和牌型语音

#### 7. 应用框架 ✅
- **位置：** `src/app/`
- **状态：** 基础框架完成
- **包含：**
  - 应用类（Application）
  - 服务注册表（ServiceRegistry）
  - 应用设置（AppSettings）：支持 JSON 保存、自动过牌秒数和 AI 等级

#### 8. 测试 ✅
- **位置：** `tests/`
- **状态：** 9 个测试套件全部通过
- **包含：**
  - test_card - 牌模型测试
  - test_deck - 牌堆测试
  - test_hand - 手牌测试
  - test_pattern_analyzer - 牌型分析测试
  - test_pattern_comparator - 牌型比较测试
  - test_scoring_engine - 计分引擎测试
  - test_game_engine - 游戏引擎测试
  - test_app_settings - 应用设置测试
  - test_ai - AI 测试

---

## 三、待完成任务

### 高优先级（V1.0 必须）

#### 1. 设置对话框完善 ⏳
- **文件：** `src/ui/dialogs/settings_dialog.h/.cpp`
- **当前状态：** 只有空壳
- **需要实现：**
  - 游戏设置（AI 难度、延迟、自动开始等）
  - 无障碍设置（后端选择、播报选项、语速等）
  - 界面设置（动画、音效、字体等）
  - 快捷键配置
  - 数据管理（清除存档、统计等）
  - 音效开关和音量控制（音效系统已就绪）

#### 2. 快捷键对话框 ⏳
- **文件：** `src/ui/dialogs/shortcut_dialog.h/.cpp`
- **当前状态：** 未实现
- **需要实现：**
  - 快捷键列表显示
  - 快捷键修改界面
  - 冲突检测
  - 恢复默认值
  - 预设方案（标准、读屏优化）

#### 3. 完整键盘操作验证 ⏳
- **当前状态：** 基础快捷键已实现
- **需要验证：**
  - 所有快捷键按文档要求工作
  - 无键盘陷阱
  - 焦点管理正确
  - F6 区域切换正常
  - 对话框焦点进入和恢复

#### 4. 三读屏实测 ⏳
- **需要测试：**
  - NVDA 完整流程
  - 保益读屏完整流程
  - 争渡读屏完整流程
  - 多读屏共存场景
  - 后端切换

#### 5. 安装包制作 ⏳
- **工具：** Inno Setup 或 NSIS
- **需要包含：**
  - 主程序
  - Qt 依赖（windeployqt）
  - 用户手册
  - 第三方声明
  - 开始菜单快捷方式
  - 卸载程序

### 中优先级

#### 6. 文档完善 ⏳
- **位置：** `docs/`
- **需要完成：**
  - rules_spec.md - 规则规格书（已有初稿）
  - architecture.md - 架构文档
  - accessibility_spec.md - 无障碍规格
  - keyboard_spec.md - 键盘规格
  - user_manual.md - 用户手册
  - build_guide.md - 构建指南
  - test_matrix.md - 测试矩阵
  - development_log.md - 开发日志
  - handover.md - 交接文档（本文档）

#### 7. 性能优化 ⏳
- **目标：**
  - 冷启动 < 3 秒
  - 键盘响应 < 50ms
  - 内存占用 < 200MB
  - AI 决策 < 300ms

#### 8. 标准 AI 增强 ⏳
- **文件：** `src/ai/standard_ai.cpp`
- **需要改进：**
  - 手牌代价评估
  - 拆牌倾向控制
  - 地主/农民策略差异化
  - 对手低牌拦截

---

## 四、构建和测试

### 环境要求
- Windows 10/11 x64
- Visual Studio 2022 Build Tools（已安装）
- CMake 3.24+（已安装）
- Qt 6.8.3 MSVC 2022 x64（已移动到项目内，路径：D:\FourPlayerDoudizhu\Qt\6.8.3\msvc2022_64）
- Ninja（已安装）

### 构建命令

```powershell
# 设置环境变量
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

# 配置（首次或 CMakeLists.txt 变更后）
cd D:\FourPlayerDoudizhu
cmake --preset=debug-x64

# 构建
cmake --build build2/debug-x64

# 运行测试（需要设置 Qt DLL 路径）
$env:PATH = "D:\FourPlayerDoudizhu\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
cd build2/debug-x64
ctest --output-on-failure
```

### 快捷构建脚本

```powershell
# 创建 build.bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=D:\FourPlayerDoudizhu\Qt\6.8.3\msvc2022_64\bin;%PATH%
cd /d D:\FourPlayerDoudizhu\build2\debug-x64
cmake --build .
ctest --output-on-failure
```

### 运行程序

```powershell
# Debug 版本
D:\FourPlayerDoudizhu\build2\debug-x64\FourPlayerDoudizhu.exe

# Release 版本（需要先构建）
cmake --preset=release-x64
cmake --build build2/release-x64
```

---

## 五、关键文件路径

### 源代码
```
D:\FourPlayerDoudizhu\
├── src/
│   ├── main.cpp                      # 程序入口
│   ├── core/                         # 核心游戏逻辑
│   │   ├── model/                    # 数据模型
│   │   ├── engine/                   # 游戏引擎
│   │   ├── rules/                    # 规则实现
│   │   └── text/                     # 文本格式化
│   ├── ai/                           # AI 系统
│   ├── ui/                           # 用户界面
│   │   ├── main_window.h/.cpp        # 主窗口（核心）
│   │   ├── sound_service.h/.cpp      # 音效服务（新增）
│   │   ├── models/                   # 数据模型
│   │   ├── widgets/                  # 自定义控件
│   │   └── dialogs/                  # 对话框
│   ├── accessibility/                # 无障碍服务
│   ├── persistence/                  # 持久化
│   └── app/                          # 应用框架
├── resources/
│   ├── app.qrc                       # Qt 资源文件（已更新）
│   └── sounds/                       # 音效文件（新增）
├── tests/                            # 测试代码
├── docs/                             # 文档
└── build2/                           # 构建目录
    ├── debug-x64/                    # Debug 构建
    └── release-x64/                  # Release 构建
```

### 配置文件
- `CMakeLists.txt` - CMake 配置（已更新，添加音效支持）
- `CMakePresets.json` - CMake 预设
- `cmake/Dependencies.cmake` - 依赖配置

---

## 六、已知问题

### 1. 编译警告
- main_window.cpp 存在字符编码警告（C4828）
- **原因：** 中文字符在 UTF-8 和系统代码页间转换
- **影响：** 无功能影响
- **解决：** 确保所有源文件使用 UTF-8 with BOM 编码

### 2. 设置对话框未完成
- **影响：** 无法通过 UI 修改设置
- **临时方案：** 直接编辑配置文件（位于 AppData）
- **优先级：** 高

### 3. 快捷键对话框未实现
- **影响：** 无法自定义快捷键
- **临时方案：** 使用默认快捷键
- **优先级：** 高

### 4. 音效音量控制未暴露
- **状态：** SoundService 支持音量控制，但未在 UI 中暴露
- **解决：** 在设置对话框中添加音量滑块

---

## 七、下一步工作计划

### 立即任务（按优先级排序）

1. **完善设置对话框**（预计 2-3 小时）
   - 添加游戏设置选项卡
   - 添加无障碍设置选项卡
   - 添加音效设置（开关、音量）
   - 保存和加载设置

2. **实现快捷键对话框**（预计 2-3 小时）
   - 显示所有快捷键
   - 支持修改快捷键
   - 冲突检测
   - 恢复默认值

3. **集成音效到所有事件**（预计 1 小时）
   - 验证所有音效触发点
   - 添加缺失的音效触发
   - 测试音效播放

4. **完整键盘测试**（预计 2 小时）
   - 逐张浏览手牌
   - 按组浏览手牌
   - 选牌和出牌
   - 叫分流程
   - 对话框导航

5. **三读屏测试**（预计 4-6 小时）
   - NVDA 完整测试
   - 保益读屏测试
   - 争渡读屏测试
   - 修复发现的问题

6. **制作安装包**（预计 2-3 小时）
   - 使用 windeployqt 收集依赖
   - 创建 Inno Setup 脚本
   - 测试安装和卸载

### 后续任务

7. 完善文档
8. 性能优化
9. 标准 AI 增强
10. 5000 局自动对战测试

---

## 八、技术要点

### 音效系统集成

**SoundService 架构：**
- 使用 Windows waveOut API（无需额外依赖）
- 从程序目录旁的 `resources/sounds` 懒加载外部 WAV 文件
- 当前使用玩儿吧四人斗地主 `card_four` 全套音效，绿色版必须随程序一起部署该目录
- 支持音量控制（0-100%）
- 异步播放，不阻塞 UI

**使用示例：**
```cpp
// 在 MainWindow 中
playSound(SoundId::CardPlay);  // 出牌时
playSound(SoundId::Bomb);      // 炸弹时
playSound(SoundId::YourTurn);  // 轮到你时
```

**音效触发点：**
- `takeCurrentCard()` - 选牌/取消选牌
- `onPlayCards()` - 出牌
- `onPass()` - 过牌
- `processAiPlay()` - AI 出牌/过牌
- `startNewGame()` - 游戏开始
- 等等...

### 无障碍架构

**三层架构：**
1. **AccessibilityService** - 服务层，管理后端
2. **AnnouncementScheduler** - 调度层，优先级和去重
3. **SpeechBackend** - 后端层，具体实现

**播报流程：**
```
游戏事件 -> Announcement -> Scheduler -> Backend -> 读屏
```

### 键盘操作设计

**核心原则：**
- F1：开战、停战、恢复，作为主流程入口
- 左右光标：按组浏览，光标停在组内最左牌
- Shift+左右：逐张浏览
- 上光标：拿起当前牌
- Ctrl+上：拿起当前组
- 下光标：按拿起顺序逐张放下牌
- Ctrl+下光标：放下所有拿起的牌
- F11：显示轮到谁行动
- F12：显示最后一次实际出牌
- Enter：出牌
- 空格：正式出牌阶段轮到自己时过牌
- Alt+大键盘1/2/3：查询下家、对家、上家的身份和剩余手牌
- F2：查看底牌
- Alt+F：查看分数和倍数
- 手牌和出牌播报采用玩儿吧格式，只读张数和点数，例如 `1张3`、`2张4`、`3张7`、`1张钩`、`1张圈`、`2张k`
- 牌相关播报不读花色，不读选中或未选中状态
- Alt+大键盘1/2/3 按 Windows 大键盘虚拟键处理，小键盘数字不触发
- Windows 前台低级键盘钩子只在四人斗地主窗口前台时兜底处理真实按键，自动测试/SendKeys 注入键交给 Qt 和原生消息路径处理
- F2、F11/F12、Alt+大键盘1/2/3/4、Alt+F 同时走应用级事件过滤、原生消息过滤和低级键盘钩子；除 F2 外的既有查询键继续保留 `RegisterHotKey` 兜底

**实现位置：**
- `MainWindow::handleKeyPress()` - 键盘事件处理
- `MainWindow::eventFilter()` - Qt 应用级键盘事件和 `ShortcutOverride` 处理
- `MainWindow::nativeEventFilter()` / `MainWindow::handleWindowsMessage()` - Windows 原生消息过滤
- `MainWindow::handleNativeShortcut()` - 原生按键、系统热键和键盘钩子统一分发
- `MainWindow::announce()` - 状态栏同步、可访问名称更新和 `QAccessibleAnnouncementEvent`
- `HandListModel` - 手牌分组逻辑
- `MainWindow::takeCurrentCard()` - 选牌逻辑

---

## 九、测试清单

### 功能测试
- [ ] 新游戏流程
- [ ] 叫分流程
- [ ] 出牌流程
- [ ] 过牌流程
- [ ] 炸弹和火箭
- [ ] 春天和反春
- [ ] 结算流程
- [ ] 存档和读档
- [ ] 统计功能

### 无障碍测试
- [ ] NVDA 播报所有关键信息
- [ ] 保益读屏完整流程
- [ ] 争渡读屏完整流程
- [ ] 焦点管理正确
- [ ] 无重复播报
- [ ] 快捷键可用

### 音效测试
- [ ] 出牌音效
- [ ] 过牌音效
- [ ] 炸弹音效
- [ ] 选牌音效
- [ ] 胜利/失败音效
- [ ] 音量控制有效

### 性能测试
- [ ] 启动时间 < 3 秒
- [ ] 键盘响应 < 50ms
- [ ] 内存占用 < 200MB
- [ ] AI 决策 < 300ms

---

## 十、重要提示

### 编码规范
- 所有源文件使用 UTF-8 with BOM 编码
- 类名：PascalCase
- 函数名：camelCase
- 成员变量：m_ 前缀
- 枚举：enum class

### Git 提交规范
```
feat: 新功能
fix: 修复 bug
docs: 文档更新
style: 代码格式
refactor: 重构
test: 测试
chore: 构建/工具
```

### 构建注意事项
1. 必须先运行 vcvarsall.bat 设置环境变量
2. 运行测试需要设置 PATH 包含 Qt bin 目录
3. Release 构建需要 windeployqt 收集依赖
4. 音效文件不是 Qt 资源内置，必须把 `resources/sounds/card_four` 复制到 build 输出目录和 `portable` 目录

### 调试技巧
1. 使用 Debug 构建进行开发
2. 启用日志：`LogService::setLogLevel(LogLevel::Debug)`
3. 查看无障碍树：使用 Accessibility Insights 工具
4. 测试读屏：安装 NVDA（免费）

---

## 十一、联系方式和资源

### 项目文档
- 开发计划：`docs/four_player_doudizhu_complete_plan.md`
- 规则说明：`docs/rules_spec.md`（待完善）
- 架构文档：`docs/architecture.md`（待完善）

### 外部资源
- Qt 文档：https://doc.qt.io/qt-6/
- Windows 音频 API：https://docs.microsoft.com/en-us/windows/win32/multimedia/waveout
- NVDA 开发指南：https://github.com/nvaccess/nvda/wiki

### 测试工具
- Accessibility Insights for Windows
- NVDA 读屏软件
- Visual Studio 调试器

---

## 十二、总结

### 已完成
✅ 核心游戏逻辑完整实现
✅ AI 系统基础功能完成
✅ 无障碍服务完整实现
✅ 音效系统集成完成
✅ 所有测试通过（8/8）
✅ 构建系统稳定

### 待完成
⏳ 设置对话框完善
⏳ 快捷键对话框实现
⏳ 三读屏实测
⏳ 安装包制作
⏳ 文档完善

### 预计工期
- 设置对话框：2-3 小时
- 快捷键对话框：2-3 小时
- 三读屏测试：4-6 小时
- 安装包制作：2-3 小时
- 文档完善：4-6 小时
- **总计：14-21 小时**

---

**交接人：** AI 助手
**接收人：** 新会话 AI 助手
**交接日期：** 2026-07-28

**备注：** 项目基础扎实，核心功能完整，音效系统刚集成完成。下一步重点是完善 UI 对话框和进行三读屏实测。所有代码已测试通过，构建稳定。

---

## 2026-07-28 最新补充：大模型确认提示

- AI 模式当前只在首次合法远程出牌响应成功后提示一次“已经用大模型思考出牌”；每回合思考文字已删除，也没有新增提示音。
- 请求刚发出不能触发确认。网络、认证、超时、解析或非法动作失败时静默降级本地高级 AI。
- 诊断日志 input_trace.jsonl 新增 type=remote_ai；response_accepted 才表示真实远程成功，request_started 仅表示发出请求，fallback_local 表示本地降级。
- 每次关闭后重新启用 AI 模式，或保存新的活动认证后，会重新等待下一次真实成功并只提示一次。
- 当前配置为 AI 模式开启，活动 DeepSeek 认证含 DPAPI 加密密钥，保存模型列表包含 deepseek-v4-flash 和 deepseek-v4-pro；旧版本没有远程结果日志，不能据此断言历史每一手均由大模型完成。
- Release 13/13 测试通过并已部署 portable，Release/portable SHA-256：3AAEE6A31B9E02364639A43DB18F45166124F2FA7EC49B779906D730E9E293C2。
- 详细交接和下一步验收以 D:\FourPlayerDoudizhu\交接说明.txt 第十七节为准。
## 2026-07-29 ????????

- ?????????????????????? GitHub ????????????????????????????? Windows/Qt ???????????????????
- ??????????? `docs/development_log.md`???????????????????????
- ????? miniaudio ? FFmpeg?????????? WinMM/PlaySound ???????????????????? waveOut ?????FFmpeg ???????????? OGG?????? portable ??? FFmpeg?
- ????????????????????? `????.txt` ??????
## 2026-07-29 GitHub open-source-first requirement

- Before adding a tool, component, library, or general-purpose capability, search GitHub for a mature open-source solution first. Prefer a license-compatible, actively maintained, secure option that fits Windows/Qt and preserves accessibility and current stable behavior.
- Record every candidate and adoption/rejection decision in `docs/development_log.md`; every future detailed handover must retain this rule.
- This round evaluated miniaudio and FFmpeg. Short sounds remain on WinMM/PlaySound because the currently available waveOut callback path caused window-close blocking in tests. FFmpeg is used only for offline conversion of user-provided OGG files and is not a runtime dependency.
- The newest complete continuation entry is section 21 of the root `????.txt`.

## 2026-07-29 final accessibility and audio delivery

This section supersedes earlier pending-status notes. The single portable package has now been rebuilt, tested, and replaced.

- Product behavior: only Game, Settings, and Help remain as top-level menus; Tab and Shift+Tab cycle those menus; Alt+G/S/H are released; Help now contains shortcuts, gameplay instructions, and About.
- Round behavior: F11 announces only the active custom player name; passes are sound-only; picked cards leave the browse sequence until dropped; Escape closes one layer or abandons the unfinished round; finished-round results remain browsable until Escape.
- Persistence: unfinished-round save/load and Ctrl+S/Ctrl+L are removed; legacy autosaves are silently deleted; settings, names, encrypted API profiles, statistics, training data, and diagnostics remain.
- Audio: the human voice is persistent and selectable; seats two and three use male sounds and seat four uses female sounds; the package contains 154 formal card-game WAV files and three independent PCM 16-bit background-music tracks.
- Unlimited play: 5000 is only the automated local-AI stress-test count. The released game has no game-count limit.
- Verification: clean Release CTest passed 15/15, including 5000 legal zero-sum games; the full suite passed again during deployment; the UI shortcut suite also passed five consecutive additional runs.
- Release/portable SHA-256: `96BBCC91DB23A2E259799821ECB703FFB1415CA0562DB9EAAEE0E824203E7AE0`.
- Portable dependencies verified: Qt6Network, Qt6Sql, qsqlite, Schannel TLS, certificate TLS, 154 card WAV files, and three music WAV files.
- Desktop shortcut verified: `C:\Users\apple007\Desktop\四人斗地主.lnk` targets `D:\FourPlayerDoudizhu\portable\FourPlayerDoudizhu.exe`.
- Remaining human acceptance: use real NVDA and speakers to judge speech timing, male/female voice quality, pass silence, combination-sound ordering, and simultaneous background music/effects. Do not automatically use a real API while testing.
- Permanent development rule: before adding tools, components, libraries, or reusable capabilities, search GitHub first and prefer a suitable mature open-source implementation; document every decision in the development log and future detailed handovers.

## 2026-07-29 F1 menu overlay hotfix

- Fixed the deployed issue where F1 started a round but left the Tab-focused Settings/Help/Game menu state over the bidding interface.
- F1 now closes all owned menus, clears the active top-menu item and Tab index, and moves focus away from the menu before starting, pausing, or resuming the round.
- New regression: open Settings, press F1, enter bidding, no popup remains, no menu action remains active, and focus is not a menu control.
- UI suite: 27/27; five consecutive targeted runs passed. Full clean deployment suite: 15/15, including 5000 local-AI games.
- Latest Release/portable SHA-256: `24493337BB3F52D4F3882E6A24842852B0EAC74FBA014402C5967D5F72E3A166`.
- Portable direct smoke test confirmed the Settings popup was visible before F1 and absent after F1. Remote AI was disabled only during the smoke test, then the original settings file was restored byte-for-byte.
- The user should now retest the same physical-key sequence with NVDA: launch, Tab to Settings or Help, expand if desired, press F1, and confirm the reader moves to the bidding/game interface without continuing to announce expanded top menus.
# 2026-07-30 版本 1.3 发布交接

- `version.txt` 已升为 1.3；Release 与 portable EXE 大小均为 774,656 字节，SHA-256 均为 `44C718916B19C4A7194EB7320B7B9EE8D3C288D99DCDC1B96F9FB9C00E474C51`。
- 默认 `card_four/din.wav` 已直接替换为 `D:\音效_轮到我出牌.wav`，大小 16,206 字节，SHA-256 为 `F383D238F4C5473495A7D9D0AFB28BBEB321C71A9E0921538BDD4C8AB7756306`；轮到你和总音效开启提示继续共用。
- 已修复替换成功后连续回车可能误触恢复的问题：替换、音效包导入以及三种恢复确认均默认选择“否”，替换成功状态不再被自动试听覆盖。
- 新增“导出当前完整音效包”，导出所有实际生效 WAV；保留模板导出、部分导入及当前、分类、全部三种恢复。
- `tools/build_release.ps1` 已去除对中文 `.iss` 和输出文件名字面量的依赖，可自动查找唯一 Inno Setup 脚本及对应版本安装包，并自动复制 D 盘分发副本。
- 两次完整 Release 构建与 CTest 均为 17/17 通过；未调用真实远程 AI。
- 1.3 安装包大小 56,197,523 字节，SHA-256 为 `249780A8EA600D37A60089CFCCD28E6117FA1F24A1D1F6C714E5911471B29CC1`；本地 dist 与 D 盘副本一致。
- 更新服务器 Release ID 为 26，服务器文件为 `feichuan_ai_doudizhu/windows/stable/1.3/b7ce93e118c4432b850b8de8486a271b.exe`，只保留 1.3；0.1 和 1.2 可更新到 1.3，1.3 和 99.0 无更新。
- 已通过接口返回的 HTTPS 地址实际下载服务器安装包，大小与 SHA-256 和本地一致；主机、容器和本地发布临时文件均已清理。

# 2026-07-31 版本 1.5 本地交接

- version.txt 已升为 1.5；本版本采用“明底牌叫分”，进入叫地主阶段即向真人、本地 AI 和远程 AI 同等公开八张底牌，并自动朗读一次；Alt+D 可在叫分及后续阶段重复查询。
- 设置新增“启用读屏官方接口兼容模式”，默认关闭并使用 Qt/UIA；兼容模式只按争渡、保益、NVDA 的固定优先级激活一个官方后端，未检测到读屏时保持静音，不使用内置语音。
- 诊断报告格式为版本 2，包含模式、路线、后端、UIA 公告抑制、最近模式切换、最近朗读投递和公开底牌状态；普通日志不写完整底牌正文。
- 最终全新 Release、17/17 CTest、portable 部署、windeployqt 和 Inno Setup 打包成功；未调用真实远程 AI。首次完整流程中快捷键套件出现一次无输出时序失败，单项 36/36 与 CTest 单项复验均通过，随后最终完整流程 17/17 通过。
- Release、portable 和 Program Files 安装版 EXE 均为 835,072 字节，SHA-256 均为 738DC0BC7E02562C9B44D88226159BFC04273ED45B8518000B8F8A8EABBCC50F。
- 1.5 安装包两份均为 56,327,994 字节，SHA-256 均为 12A33EE46326BDBF6B1F4F83381FE1066B4C104F8567A3AF1EEBF18C3C8F7B73：D:\FourPlayerDoudizhu\dist\飞船AI斗地主单机版-Setup-1.5.exe 与 D:\飞船AI斗地主单机版-Setup-1.5.exe。
- 已覆盖安装到 C:\Program Files\飞船AI斗地主单机版，注册表 DisplayVersion=1.5，公共桌面快捷方式目标和工作目录正确，AppData 设置、自定义音效、学习库、凭据、统计和日志均保留。
- 普通模式启动安装版时，进程响应正常，模块列表未加载 ZDSRAPI、byctrl 或 nvdaControllerClient。
- 更新服务器保持正式版本 1.3，Release ID 26 未修改；未上传 1.5，未创建服务器 1.5 Release。
