# 主程序开发计划

## 目标

将当前 `检验结果查询` 从独立程序升级为主程序平台下的一个功能模块。主程序采用 Win32 原生界面，作为未来所有检验相关功能的统一入口。

## 主界面布局

```
┌─────────────────────────────────────────────────────────┐
│  检验管理    系统                              主程序标题 │  ← 菜单栏
├─────────────────────────────────────────────────────────┤
│                                                         │
│                    MDI 客户区                            │  ← 子窗口区域
│              （各功能模块作为子窗口打开）                   │
│                                                         │
│                                                         │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  状态栏                                                 │
└─────────────────────────────────────────────────────────┘
```

- 默认打开全屏（`SW_MAXIMIZE`）
- 顶部菜单栏
- 中间为 MDI 客户区，各功能模块作为子窗口嵌入
- 底部状态栏

## 菜单结构

```
LIS 工作台
├── 检验管理
│   ├── 检验结果查询    → 打开检验结果查询窗口
│   └── 输血结果查询    → 打开输血申请查询窗口
│
├── 工具
│   ├── 已签收条码查询  → 打开已签收条码查询窗口
│   └── 常规报告        → 打开常规报告录入/查看界面
│
└── 系统
    └── 参数设置         → 打开数据库/系统配置窗口
```

## 开发阶段

### 阶段 1：主窗口壳（Win32）

| 任务 | 说明 | 状态 |
|------|------|------|
| 1.1 创建主窗口 | `main_frame.cpp` — 注册窗口类，全屏显示 | ✅ |
| 1.2 菜单栏 | 检验管理 / 工具 / 系统，菜单项占位 | ✅ |
| 1.3 状态栏 | 4 栏百分比宽度 + 系统字体 + IP + 时钟 | ✅ |
| 1.4 图标+标题 | `resource/app.ico`，标题 "LIS 工作台" | ✅ |
| 1.5 MDI 客户区 | `main_frame.cpp` — MDI 子窗口容器 + 窗口菜单 + 占位子窗口 | ✅ |
| 1.6 构建脚本 | `scripts/build_main.ps1` | ✅ |

### 阶段 2：模块接入

| 任务 | 说明 | 状态 |
|:-----|------|------|
| 2.1 检验结果查询接入 | `query_module.cpp` — 检验结果查询单实例 MDI 子窗口 + QueryState 每实例独立 + 通用拖条控件 + 分割器持久化 | ✅ |
| 2.2 输血结果查询模块 | `blood_module.cpp` — 独立 `BloodModuleChild` 类，接入 `LS_XK_BloodRequestApply` 只读查询；支持病人编号、病人姓名、申请单号、申请状态、申请日期过滤，其中申请状态按 `ApplyForm_Statue` 中文值匹配；LIS 结果弹窗中组合项目列表和摘要信息独立后台查询 | ✅ |
| 2.3 系统设置接入 | `main_frame.cpp` — 复用 `MdiPlaceholderChild` 类 + 设置表单控件 + 单实例模式 | ✅ |
| 2.4 已签收条码查询模块 | `barcode_module.cpp` — 接入 `LS_AS_BARCODE` 只读查询，支持日期、条形码、姓名、病人号、院区、专业组、上机状态和取消签收状态过滤；院区按申请科室派生，专业组按 `LS_AS_ROOM.Dept_Code=102/401` 与院区联动；业务修改按钮保持禁用 | ✅ |
| 2.5 常规报告模块 | `regular_report_module.cpp` — 按 `temp/模版2.png` 基本完成三栏报告工作台；支持按检验日期和检验仪器查询 `LS_AS_REPORT`，右侧选中行回填左侧信息并联动中间项目明细；中间组合项目按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，图象页按 `REP_NO` 按需展示 `LS_AS_ITEMPICTURE.PICTURE` 并通过滚动视口查看大图；底部 `图形(T)` 可按当前选中报告打开独立结果图窗口，跟随右侧选中行刷新，并保存弹窗尺寸；右侧列表支持本地排序、首末行跳转、今天/前一天/后一天快捷切换检验日期、样本号回车定位、顶部动态统计、保留状态刷新和勾选批量打印；底部 `1/2/3` 可按系统设置快速切换检验仪器；右侧行右键菜单接入 `LabelPrint` 执行 `打印条码`，条码组合项目取中间明细组合项目而非右侧项目名称，打印机型号和 TSPL/ZPL 后端由 LabelPrint 统一入口选择；构建时优先 `find_package(LabelPrint 1.2)`，再回退本地源码；中间/右侧拖条可调整并持久化；左侧滚动表单采用自绘分组框 + 内部控件 `WS_CLIPSIBLINGS`，右侧顶部摘要采用父面板自绘并自动换行，减少拖动和滚动残影 | ✅ |

### 阶段 3：统一配置

| 任务 | 说明 | 状态 |
|------|------|------|
| 3.1 统一 INI | `default_ini_path()` 全模块共用；`wWinMain` 启动加载；系统设置保存回写 `g_ctx` | ✅ |
| 3.2 统一数据库连接 | `g_ctx.dbSettings` 启动加载，`ModuleContext` 传递给各模块，设置保存回写 | ✅ |
| 3.3 关于对话框 | `IDM_ABOUT` 弹出 MessageBox（版本号 + 作者信息） | ✅ |

### 阶段 4：Qt 主程序（后期）

主程序先以 Win32 实现，但架构上为 Qt 迁移预留接口。阶段 2 完成后，`feat/main-app-qt` 分支将：

| 任务 | 说明 |
|------|------|
| 4.1 Qt 主窗口 | `QMainWindow` + `QMenuBar` + `QMdiArea`，平移 Win32 菜单结构 |
| 4.2 Qt 模块接入 | 现有 `src_qt/` 下组件（MainWindow、TrendWindow）接入 QMdiArea |
| 4.3 Win32 共存 | CMake `BUILD_MAIN_APP` 选项切换 Win32/Qt 主程序入口 |
| 4.4 Qt 版 CI | 在现有 `build-qt` job 中增加主程序构建目标 |

### Qt 架构预留设计

当前 Win32 主程序的关键抽象，为 Qt 迁移做准备：

```
                    ┌─────────────────┐
                    │  search_core    │  ← 数据库层（双版共用）
                    └────────┬────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
    ┌─────────┴─────────┐       ┌──────────┴──────────┐
    │  Win32 主窗口      │       │  Qt 主窗口（后期）    │
    │  main_frame.cpp    │       │  src_qt/main_app.cpp │
    │  main_menu.cpp     │       │  QMenuBar            │
    │  main_mdi.cpp      │       │  QMdiArea            │
    └─────────┬─────────┘       └──────────┬──────────┘
              │                             │
    ┌─────────┴─────────┐       ┌──────────┴──────────┐
    │  子模块（Win32）    │       │  子模块（Qt）        │
    │  main.cpp (查询)   │       │  main_window.cpp    │
    │  blood_query.cpp   │       │  blood_window.cpp   │
    └───────────────────┘       └─────────────────────┘
```

- 主窗口与子模块之间通过函数调用（Win32）或 signal/slot（Qt）通信
- 数据库连接由主窗口持有，通过 `DbSettings` 结构体传递给子模块
- 子模块不依赖主窗口的具体实现（Win32 或 Qt），只接收数据结构和回调

## 文件结构（现状）

```
src/
  main_frame.cpp          ← 主窗口入口 + g_modules[] 注册表 + 自动菜单/分发
  main_app.h              ← 主程序上下文（共享状态）
  module_registry.h       ← ModuleContext + ModuleDef 统一接口
  menu_toolbar.cpp/h      ← 自绘菜单风格工具栏组件
  query_module.cpp/h      ← 检验结果查询 MDI 子窗口
  barcode_module.cpp/h    ← 已签收条码查询 MDI 子窗口
  regular_report_module.cpp/h ← 常规报告 MDI 子窗口
  settings_module.cpp/h   ← 系统设置 MDI 子窗口

  main.cpp                ← 独立查询工具（不变）
  search_ui_layout.cpp    ← 查询界面控件布局（共享）
  ...                     ← 其余文件不变
```

## 当前现状与兼容

| 项目 | 状态 |
|------|------|
| Win32 检验结果查询 | 完整，独立运行 |
| Qt 检验结果查询 | 完整，独立运行 |
| Win32 主程序平台 | 阶段 1~3 完成，模块系统就绪，查询+设置已接入 |
| 核心数据库层 | `search_core` 静态库，双版共用 |
| CI 构建 | Win32 + Qt 双绿 |

主程序开发期间：
- Win32 版查询仍可独立编译运行（`result_search.exe`）
- Qt 版不受影响
- 新增主程序入口编译为独立 exe（`lis_workbench.exe`），链接 `search_core`

## 技术选型

| 维度 | 选择 | 理由 |
|------|------|------|
| 主窗口框架 | Win32 原生 | 与现有查询界面一致，零额外依赖 |
| 子窗口模式 | MDI | 多文档接口，各模块作为独立子窗口 |
| 配置共享 | `ClientConfig.ini` | 延用现有格式 |
| 数据库连接 | 主窗口持有 `DbSettings` | 避免各模块重复配置 |

## 设计原则

### 模块接口标准化

每个功能模块对外暴露统一接口，主窗口不关心模块内部实现：

```cpp
// main_module.h — 所有模块遵循此约定
struct ModuleDef {
    const wchar_t* menuParent;   // 所属菜单，如 L"检验管理"
    const wchar_t* menuLabel;    // 菜单项文字，如 L"检验结果查询"
    HWND (*create)(HINSTANCE, HWND parent, const DbSettings&, HFONT);
    void (*destroy)(HWND);
};

// 各模块注册示例
ModuleDef g_queryModule = {
    L"检验管理", L"检验结果查询(&Q)...",
    create_query_window, destroy_query_window
};
```

新增模块只需编写 `ModuleDef` + 窗口过程，主窗口代码零修改。

### 构建隔离

```
CMake 目标：
  result_search.exe       ← 独立查询工具（现有，不受影响）
  lis_workbench.exe            ← 主程序
    ├── main_frame.cpp        ← 主窗口 + g_modules[] 注册表
    ├── query_module.cpp      ← 查询 MDI 子窗口
    ├── settings_module.cpp   ← 设置 MDI 子窗口
    └── menu_toolbar.cpp      ← 自绘工具栏组件
```

- `result_search.exe` 的 CMake 目标和源码不变
- 主程序作为独立的 `add_executable(main_app WIN32 ...)`
- 两个目标链接同一个 `search_core` 静态库
- 新增模块直接添加到 `main_app` 的 CMake 源文件列表

### 共享状态

```cpp
// main_app.h — 主程序全局上下文
struct AppContext {
    HINSTANCE instance;
    HWND mainWindow;
    HWND mdiClient;
    HFONT uiFont;
    search::DbSettings dbSettings;
    int fontSize;
};
```

- 主窗口创建时初始化 `AppContext`，通过 `GWLP_USERDATA` 或参数传递给子模块
- 子模块不直接访问全局变量，只通过上下文结构体获取所需资源
- 避免各模块各自读 INI、各自连数据库

### MDI 子窗口消息处理规则

在 `mdiChildProc` 中处理消息时需遵循以下规则，否则会导致菜单栏残留、窗口控件异常等 bug：

| 消息 | 规则 | 原因 |
|------|------|------|
| `WM_NCCREATE` | **不拦截**，交 `DefMDIChildProcW` 全权处理 | MDI 框架内部初始化子窗口跟踪数据（窗口菜单、激活状态） |
| `WM_NCDESTROY` | **不拦截**，交 `DefMDIChildProcW` 全权处理 | MDI 框架清理最大化控件（菜单栏图标、还原/关闭按钮） |
| `WM_SIZE` | **必须 `return 0`**，不可落入 `DefMDIChildProcW` | `DefMDIChildProcW` 的 WM_SIZE 处理与 MDI 客户端状态交互会导致菜单栏出现残留 |
| `WM_DESTROY` | 可拦截做用户态清理，但不可跳过 `DefMDIChildProcW` | 框架需感知子窗口销毁以移除窗口菜单条目 |
| `GWLP_USERDATA` | **禁止使用** | `DefMDIChildProcW` 内部使用，覆盖会导致 MDI 状态损坏 |
| 状态存储 | 用 `SetProp`/`GetProp`/`RemoveProp` | 窗口属性独立于 MDI 内部结构，无冲突 |

核心原则：**所有 MDI 生命周期消息（NCCREATE/NCDESTROY）必须原封不动传给 `DefMDIChildProcW`；WM_SIZE 必须直接 return 0 不传给框架。**

### 向后兼容

| 保证项 | 措施 |
|--------|------|
| 查询工具独立运行 | `result_search.exe` 目标不变，不引入主程序依赖 |
| INI 格式扩展 | 主程序沿用 `ClientConfig.ini`，新增可选 `[LisSummary]` 项目代码配置，缺失时使用内置默认值 |
| 核心库不变 | `search_core` 只加功能不改接口 |
| Qt 版不受影响 | 主程序只做 Win32 入口，Qt 版保持原样 |
| CI 双版继续 | `build-win32` + `build-qt` job 不动，新加 `build-main-app` job |

### 测试策略

每个模块可脱离主程序独立测试：

```
独立模式：
  result_search.exe   ← 查询模块独立运行
  blood_query.exe     ← 输血模块独立运行（调试用）
  settings.exe        ← 参数设置独立运行

集成模式：
  lis_workbench.exe        ← 所有模块在主窗口中运行
```

- 每个模块保留独立的 `WinMain` + 独立窗口入口
- 主程序模式下，模块的独立入口被替换为子窗口创建函数
- 用 `#ifdef STANDALONE` 或不同的 `.cpp` 入口文件控制

## 后续扩展

主程序平台建立后，新增功能只需：
1. 在菜单中加一项
2. 创建对应的子窗口 `.cpp`
3. 注册到 MDI 客户区
4. 共享主窗口的数据库连接
