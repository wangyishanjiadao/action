# mp3player

Android 命令行 MP3 播放器，通过读取 `/dev/input/event*` 实现硬件音量键控制。

## 功能

- 扫描当前目录下所有 `.mp3` 文件，编号选择播放
- 终端实时进度条 `[████████░░░░░░] 45%  02:15 / 05:00`
- **双击音量下键** → 暂停 / 继续
- **双击音量上键** → 退出程序
- 键盘回退：`Space` 暂停，`Q` 退出

## 使用

```bash
# 扫描当前目录，交互式选择
./mp3player

# 直接播放指定文件
./mp3player song.mp3
```

## 构建

交叉编译需要 Android NDK。GitHub Actions 已配置好 CI — push 到 `main` 分支即自动编译 arm64-v8a 产物。

本地构建：

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 运行环境

- Android 9+ (API 28)
- **Root 权限**（访问 `/dev/input/event*` 必需）
- MT 管理器或其他终端模拟器

## 问题排查

**音量键没反应？** 检查 SELinux 是否拦截了 input 设备读取：

```bash
su -c setenforce 0
./mp3player
su -c setenforce 1
```

**找不到 .mp3 文件？** 确保当前工作目录下有 `.mp3` 文件。MT 管理器执行脚本时注意工作目录路径。

**系统音量弹窗也出现了？** 程序独占 input 设备失败（SELinux 拦截），先执行 `setenforce 0`。

## 技术栈

- C11 + miniaudio（单文件音频库）
- CMake 构建系统
- Android NDK 交叉编译
- Linux input 子系统 (`/dev/input/event*`)

## 文件结构

```
├── main.c                          # 全部源代码（单文件）
├── CMakeLists.txt                  # 构建配置
├── .github/workflows/build.yml     # GitHub Actions CI
└── README.md
```
