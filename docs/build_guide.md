# 构建指南

## 环境要求
- Visual Studio 2022 Build Tools (MSVC 14.50+)
- CMake 3.24+
- Ninja 1.12+
- Qt 6.8.3 MSVC2022 x64，项目内路径：`D:\FourPlayerDoudizhu\Qt\6.8.3\msvc2022_64`

Qt 目录已经纳入项目根目录统一管理，但不纳入 Git。不要删除 `D:\FourPlayerDoudizhu\Qt`，否则无法继续本地构建。

## 配置
```
cmake --preset=debug-x64
```

## 构建
```
cmake --build --preset=debug-x64
```

## 测试
```
cd build/debug-x64 && ctest --output-on-failure
```
