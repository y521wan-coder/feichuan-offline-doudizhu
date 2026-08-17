# 构建指南

## 依赖

- Windows x64
- MSVC Build Tools
- CMake 3.24+
- Ninja
- Qt 6.8.x MSVC x64，包含 Core、Gui、Widgets、Network、Test

游戏本体不链接 Qt Network；该模块仅由独立更新器和更新器测试使用。

默认预设在项目根目录的 `Qt/6.8.3/msvc2022_64` 查找 Qt。也可在配置时显式传入：

```powershell
cmake -S . -B build/debug-x64 -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build/debug-x64
ctest --test-dir build/debug-x64 --output-on-failure
```

发布构建使用 `tools/build_release.ps1`，产物只能写入 `artifacts/` 与 `releases/`。
