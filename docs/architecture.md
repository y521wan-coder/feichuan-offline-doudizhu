# 架构文档

## 分层
1. Application层：启动、生命周期
2. Domain层：牌、玩家、牌型、规则、计分
3. Engine层：命令、状态机、回合、事件
4. AI层：合法动作、策略、提示
5. Presentation层：Qt窗口、模型、视图
6. Accessibility层：播报、后端
7. Persistence层：设置、存档、统计、日志

## 数据流
键盘输入 → UI Command → GameCommand → GameEngine验证 → GameEvent → UI更新 → 播报 → 存档

## 关键约束
- UI不直接修改手牌
- AI不绕过Engine
- AccessibilityService不改变游戏状态
- 核心规则不依赖Qt Widgets
