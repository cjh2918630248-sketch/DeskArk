# DeskArk

明日方舟的轻量级桌宠项目，技术栈为 **C/C++ · Qt6 · OpenGL · Spine**。

## 项目结构

```
DeskArk
├── models/                 # 角色基建小人模型（.atlas / .skel / 贴图）
├── src/                    # 项目源代码
├── spine_output/           # Spine 导出的资源
├── 3rd_party/spine-cpp/    # spine-cpp 运行时（第三方库）
├── RoleSelect.json         # 角色映射表（显示名 → 模型目录）
└── CMakeLists.txt          # CMake 构建脚本
```

## 源码文件职责

### `src/main.cpp`
程序入口。

- 创建 `QApplication`，设置应用名，`setQuitOnLastWindowClosed(false)` 保证关闭窗口不退出（桌宠常驻托盘）。
- 配置 `QSurfaceFormat`：OpenGL 3.3 Core Profile，8 位 alpha（透明窗口必需），关闭多重采样。
- 提供 spine-cpp 必需的 `spine::getDefaultExtension()` 实现（内存分配 / 文件读取的默认扩展）。
- 实例化并显示 `PetWindow`，进入事件循环。

### `src/PetWindow.h` / `PetWindow.cpp`
桌宠主窗口，负责窗口行为与交互逻辑，**不直接处理 OpenGL 渲染**。

**窗口特性**
- 无边框、置顶、`Qt::Tool`（不在任务栏显示）+ `WA_TranslucentBackground` 透明背景。
- 内部用 `QVBoxLayout` 只放一个 `SpineWidget`（实际渲染控件）。

**核心职责**
- **角色加载与切换**：`loadRoles()` 读取 `RoleSelect.json`；`switchRole()` 解析模型路径并调用 `SpineWidget::loadModel()` 换模型（切换前先 `stopMove()` 重置状态，新角色自动回到 Relax）。
- **动画分发**：`playPetAnimation()` 统一入口——点击右键菜单或点击窗口走这里；`Move` 动作特殊处理为启动移动模式，其余交给 Spine 播放。
- **Move 移动状态机**（`MovePhase` 枚举 + 三个 QTimer）：
  - `move_timer_`：16ms 高频驱动窗口位移，用 `QElapsedTimer` 按真实时间计算步进，保证平滑。
  - `break_timer_`：移动中 5~10 秒随机触发一次随机休息。
  - `pause_timer_`：暂停阶段计时（边界 Relax / 休息过渡 / Sit/Sleep）。
  - 完整流程：移动 → 随机休息（Relax 2s → Sit/Sleep 10s → Relax 3s，或一次性动画播完）→ 继续移动；到达屏幕边界 → Relax 5s → 反向移动。
- **边界判定**：用 `SpineWidget::hitRegion()` 的角色可见左右边缘贴屏幕 `availableGeometry()` 两端，避免透明留白导致角色跑出屏幕。
- **鼠标交互**：拖拽移动窗口（拖拽时暂停自动移动）、点击循环切换动画、右键弹出菜单。
- **透明穿透**：`nativeEvent()` 处理 `WM_NCHITTEST`，鼠标落在角色不透明区域外时返回 `HTTRANSPARENT` 让点击穿透到下层（高 DPI 下按 `devicePixelRatioF()` 换算坐标）。
- **系统托盘**：`setupTray()` 创建托盘图标与「隐藏/显示」「退出」菜单。

### `src/SpineWidget.h` / `SpineWidget.cpp`
Spine 渲染控件，继承 `QOpenGLWidget`，负责模型加载与 OpenGL 绘制。

**模型加载**
- `loadSkeleton()`：加载 `.atlas` + `.skel`（`SkeletonBinary`），构建 `Skeleton` / `AnimationState` / `AnimationStateData`，设置 0.2s 动画混合过渡，默认播放 Relax。
- `loadModel()`：运行时换模型——先删旧 GL 纹理，再释放旧 Spine 对象，加载新模型后重建纹理、重新 `fitToView()`。

**渲染管线**
- `initializeGL()`：初始化 OpenGL、编译 shader、上传图集纹理、创建 VAO/VBO。
- `paintGL()`：按帧间隔驱动 `AnimationState` 更新骨骼，遍历 draw order 逐 slot 绘制；每 6 帧更新一次窗口命中区（移动期间冻结以消除回读卡顿）。
- `drawSlot()`：处理 `RegionAttachment` / `MeshAttachment` / `ClippingAttachment`，计算顶点、UV、颜色，按混合模式绘制。
- 自定义 GLSL shader：预乘 alpha 混合，纹理用 `GL_NEAREST` 避免边缘黑点。
- `updateWindowMask()`：从帧缓冲回读角色不透明区域，生成 `QRegion` 供鼠标穿透判定（用 `grabFramebuffer` 而非 `setMask`，避免半透明像素被挖洞）。

**对外接口**
- `setAnimation()` / `playAnimation()`：播放动画（`Interact` 为一次性触发，播完自动回 Relax）。
- `setFlipped()`：水平翻转角色（Move 反向时朝向反转）。
- `animationDuration()`：查询动画自然时长。
- `setAutoReturnToRelax()`：控制触发动画播完是否自动回 Relax。
- `setMaskFrozen()`：冻结/恢复窗口形状更新。
- `hitRegion()` / `animations()`：供主窗口读取命中区域与动画列表。

### `RoleSelect.json`
角色映射表，键为显示名、值为 `models/` 下的模型目录名，例如：

```json
{
    "溯光星源" : "1047_halo2",
    "克莱门莎" : "4231_clemnt"
}
```

### `CMakeLists.txt`
构建脚本。

- C++17，`AUTOMOC` 自动处理 Qt 元对象。
- `find_package(Qt6 ... Widgets OpenGL OpenGLWidgets)`。
- 将 `3rd_party/spine-cpp` 编译为静态库 `spine-cpp`。
- 链接 `Qt6::Widgets / Qt6::OpenGL / Qt6::OpenGLWidgets / spine-cpp`。
- 构建后自动：复制 `models/`、`RoleSelect.json`、`windeployqt` 部署 Qt 依赖、复制 MinGW 运行时 DLL 到 exe 旁。

> 注意：编译器（MinGW）与 Qt 版本必须匹配。若链接报大量 Qt 符号 `undefined reference`，检查 `build/CMakeCache.txt` 中 `Qt6_DIR` 是否指向了 `msvc2022_64`，需改用 `mingw_64` 版 Qt 重新配置。

## 构建与运行

spin-cpp仓库地址：https://github.com/Chobolabs/spine-cpp
nlohmann_json仓库地址：https://github.com/azadkuh/nlohmann_json_release
Ark-Models仓库地址：https://github.com/isHarryh/Ark-Models

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:/Development/Qt/6.8.3/mingw_64 -DCMAKE_CXX_COMPILER=D:/Development/mingw64/bin/g++.exe
cmake --build build
# 运行 build/DeskArk.exe
```
## 版权声明
本仓库中所有素材其版权归属 上海鹰角网络有限公司 所有。不得用于商业用途，不得损害版权方的利益。