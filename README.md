# DeepSeek 桌宠 (DeepSeekPet)

一个悬浮在桌面上的小桌宠：**鼠标悬浮显示 DeepSeek 余额和功能菜单，点击固定/取消固定面板**，
余额低于阈值（默认 1 元）时自动换成"顶锅哭哭"的形象，支持**滚轮缩放**，
并提供一键启动 **DeepSeek Harness** 的选项。

- 语言/技术：**C++17 + 纯 Win32 API**（GDI+ 绘图、WinHTTP 请求余额、分层窗口做逐像素透明）
- 无任何第三方运行库依赖，编译出来是**单文件 exe**（静态链接 MSVC 运行库）
- 素材：两张图为原型，动图逐帧抠图导出为竖排 strip，运行时切片播放

## 1. 目录结构

```
DeepSeekPet/
├─ build.bat                 一键构建（自动定位 MSVC、编译、复制素材）
├─ config.ini.example        配置示例
├─ assets/                   素材
│  ├─ pet_wave.webp          你提供的原始动图（49 帧）
│  ├─ frames_normal.png      常态动画：49 帧竖排 strip（程序主用这个）
│  ├─ frames_normal.meta.json  strip 元数据：帧数/宽高/帧间隔
│  ├─ frames_normal/         逐帧 PNG（备用格式，strip 缺失时使用）
│  ├─ pet_normal.png         常态静态图（内嵌进 exe，作为兜底）
│  ├─ pet_low.png            低余额形象（已去背景，内嵌进 exe）
│  └─ pet.ico                程序/托盘图标
├─ src/
│  ├─ main.cpp               全部程序逻辑（单文件，中文注释）
│  ├─ pet.rc                 资源脚本
│  └─ pet.manifest           清单（DPI 感知 + 通用控件 v6）
├─ build/                    构建输出
│  ├─ DeepSeekPet.exe        成品
│  └─ assets/                运行时素材副本（动画帧从这里读）
└─ tools/                    辅助脚本（不影响程序运行）
   ├─ make_assets.py         一站式：抠图 + 逐帧导出 + 打包 strip + 图标
   ├─ make_anim_frames.py    从动图导出逐帧 PNG
   ├─ make_strip.py          把逐帧 PNG 打包成竖排 strip
   ├─ render_check.cpp       离线渲染面板/桌宠，用于核对排版
   └─ test_zoom.py           缩放/面板/动画的自动化测试
```

> 关于素材加载方式：早期版本逐个解码 49 个 PNG 文件会导致进程异常退出，
> 现在改为**一张竖排 strip 图 + 内存切片**，一次解码得到全部帧，稳定且更快。
## 2. 构建

### 方式 A：命令行（build.bat）

双击 `build.bat`（或在命令行运行）。它会：

1. 找到 Visual Studio 的 `VsDevCmd.bat`（会检查该目录下是否真有 `cl.exe`，
   避免 VS 更新中途目录在、编译器不在的情况）；**VS 不可用时自动退到 CLion 自带的
   Visual Studio 工具链**（`C:\Program Files\JetBrains\CLion <版本>\Microsoft Visual Studio\...`）
2. `rc` 编译资源 → `cl` 编译链接 → 输出 `build\DeepSeekPet.exe`
3. 把 `assets` 复制到 `build\assets`

> 换素材不用改代码：替换 `assets/` 里的图片后重新运行 `build.bat` 即可。

### 方式 B：CLion（CMake）

根目录已提供 `CMakeLists.txt`，直接 **Open 整个文件夹** 即可：

1. CLion → `File | Open` → 选 `DeepSeekPet` 文件夹（**不是** 单个 cpp 文件）
2. 首次打开会提示 CMake 工程 → 选 `Load CMake project`
3. `Settings | Build, Execution, Deployment | Toolchain` 选 **Visual Studio**
   （CLion 会自己找 VS；找不到就在 Toolchain 里手动指定 VS 安装目录）
4. 直接点运行/调试按钮。产物在 `cmake-build-<配置>\DeepSeekPet.exe`

CMake 工程已经帮你处理好这几件事：

- `/utf-8`（源码含中文，必须）、`/MT`（静态 CRT，便于分发）
- 用 `rc` 编译 `src\pet.rc`（图标 + DPI 清单）
- 构建后自动把 `assets\` 复制到 exe 旁边（否则运行时读不到动画帧）

如果你更想用命令行验证 CMake 配置：

```bat
cmake -S . -B cmake-build-release -G "Visual Studio 17 2022" -A x64
cmake --build cmake-build-release --config Release
```

> 注：本机 Windows SDK 是 10.0.26100，CMake 会自动使用它。

## 3. 使用

1. 运行 `build\DeepSeekPet.exe`
2. 首次启动会延迟 1 秒弹出一个**非模态**设置窗口，填入 DeepSeek API Key
   （在 <https://platform.deepseek.com/api_keys> 获取），按「确定」保存
3. 设置窗口的按钮行为：

| 操作 | 效果 |
| --- | --- |
| **确定**（或回车） | 保存配置并关闭；未填 API Key 时会再确认一次 |
| **取消**（或 Esc） | 放弃修改关闭；如果改过内容会先问一次"是否放弃" |
| 右上角 **叉号** | 如果改过内容，会询问「保存 / 不保存 / 继续编辑」，不会静默丢配置 |

4. 操作方式：

| 操作 | 效果 |
| --- | --- |
| 鼠标悬浮到桌宠身上 | 自动弹出余额面板（余额、状态、菜单） |
| 单击桌宠 | **固定**面板；再单击一次取消固定 |
| 按住拖动 | 移动桌宠位置（自动记忆） |
| **在桌宠上滚动鼠标滚轮** | **放大 / 缩小（每格 ±15%，范围 50%~200%）** |
| 面板项：放大 / 缩小 / 恢复原始大小 | 同上；缩放比例自动保存，下次启动沿用 |
| 面板项：刷新余额 | 立即重新查询余额 |
| 面板项：复制余额到剪贴板 | 复制 `¥ xx.xx` |
| 面板项：配置 API Key / 阈值 | 打开设置窗口 |
| 面板项：启动 DeepSeek Harness | 启动 DSH |
| 面板项：总在最前 / 取消 | 切换置顶 |
| 面板项：退出桌宠 | 退出 |
| 键盘 `+` / `-` | 放大 / 缩小（窗口获得焦点时） |
| 键盘 `Esc` | 取消固定并收起面板 |
| 右键桌宠 | 快捷菜单（刷新 / 设置 / 退出） |
| 双击托盘图标 | 打开余额面板 |

> 缩放时桌宠以"身体中心"为锚点，不会跳位；面板本身大小不变，保证文字始终清晰。
> **桌宠和面板都允许超出屏幕**：程序不会把它们强行拉回屏幕内，可以放到屏幕边缘或屏幕外。

余额每 5 分钟（可配）自动刷新一次，刷新失败时面板会显示原因（Key 无效 / 网络不通等）。

## 4. 配置文件

位置：`%LOCALAPPDATA%\DeepSeekPet\config.ini`（UTF-8），字段见 `config.ini.example`。
日志：`%LOCALAPPDATA%\DeepSeekPet\pet.log`（排查问题时看它）。

## 5. 关于启动 DeepSeek Harness

点"启动 DeepSeek Harness"时按以下顺序查找：

1. 配置里的 `dsh_command`（可写完整 exe 路径）
2. PATH 中的 `dsh.exe` / `dsh.cmd` / `dsh.bat`
3. 常见安装位置：`%LOCALAPPDATA%\Programs\DeepSeek Harness\DeepSeek Harness.exe` 等

找不到时会提示你填写 `dsh_command`。

## 6. 形象切换规则

| 条件 | 形象 |
| --- | --- |
| 余额 ≥ `low_threshold`（默认 1.00 元）或查询失败 | 图 1：常态（49 帧动画，约 1 秒一循环） |
| 余额 < `low_threshold` | 图 2：顶锅哭哭（去背景静态图） |

阈值可在设置窗口里改。

## 7. 素材生成（可选）

原始图已放在 `tools/` 与 `assets/` 下，需要重新生成时（需 Python + Pillow + numpy）：

```bash
python tools/make_assets.py        # 一站式：抠图 + 逐帧导出 + 打包 strip + 生成图标
python tools/make_anim_frames.py   # 仅从 assets/pet_wave.webp 导出逐帧 PNG
python tools/make_strip.py         # 仅把 frames_normal/ 打包成 frames_normal.png
```

程序读取素材的顺序：

1. `assets\frames_normal.png` + `.meta.json`（**竖排 strip 图**，推荐，一次解码 49 帧）
2. `assets\frames_normal\frame_%03d.png`（逐帧目录，兼容小规模帧数）
3. exe 内嵌的 `pet_normal.png`（静态兜底）

- 图 2 是照片（有墙面、地面、投影），抠图用"边界漫水 + 区域生长 + 边缘去污"组合完成；
  仍会保留脚下一小圈原始地面投影，属于有意保留（看起来是站在地上而不是浮着）。
- 想彻底去掉，可在 `tools/make_assets.py` 里把 `foot_top` 的比例调小或加大 `d_floor <= 150.0` 的阈值重新生成。

## 8. 已知说明

- 程序是**透明分层窗口**，某些截屏工具（如 `CopyFromScreen`、GDI `GetPixel`）抓不到它的内容，
  这是 Windows 合成器对 per-pixel alpha 分层窗口的行为，**不是显示异常**。
  想确认桌宠是否真的显示出来了，可以看 `WindowFromPoint` 命中测试是否返回桌宠窗口
  （`tools/test_zoom.py` 里就是这么验证的）；核对界面排版可用 `tools/render_check.cpp` 离线渲染。
- 首次运行会延迟约 1 秒弹出设置窗口；如果直接改配置文件而不填 Key，程序不会打扰你，
  但面板会显示"未配置 API Key"。
- 缩放范围 50%~200%（`config.ini` 的 `scale`）。再大也只是把 320px 的素材拉伸，会变糊。

## 9. 开发记录（踩过的坑）

1. **设置窗口空白 / 只能按叉号**：对话框模板用内存组装时 `cdit`（控件数）必须等于实际控件数，
   写小了会导致排在后面的控件全部不被创建。现已修正为 13 并逐项验证。
2. **设置窗口阻塞桌宠**：`DialogBoxIndirectParam` 是模态的，会卡住消息循环，
   导致桌宠不响应悬停。现已改为 `CreateDialogIndirectParam`（非模态）。
3. **逐个解码 49 个 PNG 导致进程异常退出**：改成一张竖排 strip 图 + 内存切片后稳定。
4. **CRT 的 `_wfopen`/`fwprintf` 在此环境下会卡死**：日志与配置读写全部改用 Win32 文件 API。
5. **悬停时桌宠会位移**：早期实现里"面板展开"会重新计算窗口位置并把窗口夹回屏幕，
   导致桌宠被挤偏。现在改为**以桌宠在屏幕上的位置为唯一基准**：
   `SetSpritePos()` 反推窗口位置，`SetPanelShown()` / `ApplyZoom()` 都只改窗口尺寸、不动桌宠；
   所有"夹回屏幕"的逻辑已删除（桌宠和面板都允许超出屏幕）。
6. **坐标验证要注意 DPI**：本机缩放 150%，非 DPI 感知的测试脚本读到的是虚化坐标（会差 1.5 倍）。
   验证窗口几何时脚本里要加 `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`。
7. **悬停抖动**：面板展开会让窗口"向上生长"，把图标下方的区域挤进面板范围，
   于是"在面板上→不收起、一离开→立刻收起"来回翻转。现改为
   **面板贴窗口顶部、桌宠贴窗口底部**，并在面板翻转/光标移动后加 250ms 静默期（防抖）。
8. **判定区比图标小**：命中图阈值从 alpha>24 放宽到 alpha>8，并对命中区做 3 像素膨胀。
9. **二级菜单窗口高度变化会把菜单项挪走**：改为按"见过的最大面板高度"保留窗口，
   主菜单↔二级菜单切换时窗口高度不变。
10. **素材切换时重复解码 strip 会卡住**：释放 20MB 位图后再解码同尺寸图，GDI+ 会挂住。
    改成**内置素材常驻内存**（`fsBuiltin`），自定义素材单独放 `fsCustom`，切换只换指针。
11. **`.bat` 文件不能写非 ASCII 注释**：cmd.exe 会按本地代码页解析，中文注释会被当成命令。
    `build.bat` 因此保持纯 ASCII。
12. **Visual Studio 后台自动更新会临时搬走 `cl.exe`/`link.exe`**（本次是 18.9 → 18.10.2）。
    表现为"昨天还能编译，今天说找不到编译器"。更新完成后文件会自动回来；
    VS 不可用时可临时用 CLion 自带的那份工具链。
