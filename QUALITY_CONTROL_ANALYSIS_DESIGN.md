# 质控分析页设计方案

本文档记录基于“常规报告”页中部分样本号对应质控品结果，新增“质控分析”页的设计方案与阶段性实现边界。当前阶段已按“LIS 只读 + 本机 SQLite 长期镜像 + 手动导入”的方式实现：`查询` 只读取本机 SQLite，`导入质控` 才访问 LIS 并更新镜像。

## 背景

当前项目已经有较完整的常规报告工作台：

- 右侧报告列表按检验日期和检验仪器查询 `LS_AS_REPORT`。
- 选中报告后，通过 `REP_NO` 查询 `LS_AS_REPENTRY` 明细。
- 明细中包含项目代码、项目名称、结果、单位、参考范围和异常标记等信息。
- 常规报告页已有右键菜单、趋势图入口、批量打印、保留状态刷新、按报告号跳转定位等能力。
- 统计分析管理下已有 HIV 抗体检测统计和急诊样本统计两个独立统计页，均采用“独立模块 + 共享查询层 + 明细回跳常规报告”的模式。

现场观察到：常规报告左侧/右侧列表中的部分样本号对应质控品检测结果。这些质控品结果可以作为质控分析的数据来源。

## 设计目标

新增一个独立的“质控分析”页，用于按用户配置管理质控样本和项目，从本机 SQLite 镜像读取质控点并完成质控趋势查看、失控规则判断和原始报告追溯；需要更新事实数据时，由用户手动触发 `导入质控`。

质控结果的事实来源是 LIS 中的 `LS_AS_REPORT + LS_AS_REPENTRY`，但页面日常 `查询` 不直接访问 LIS。SQLite 保存本机质控结果镜像和配置，界面必须标识 `本机缓存` 或 `LIS导入`，避免把本机镜像误认为实时 LIS。

## LIS 只读边界

这是本功能必须遵守的硬边界：**质控分析功能不得修改原 LIS 数据库**。

允许的 LIS 操作只有只读查询：

- 只允许对 LIS 执行 `SELECT` 查询，包括读取 `LS_AS_REPORT`、`LS_AS_REPENTRY`、`LS_AS_ITEM`、`LS_AS_MACHINE` 和必要字典表。
- 不允许对 LIS 执行 `INSERT`、`UPDATE`、`DELETE`、`MERGE`、存储过程写入、触发业务状态变更或任何 DDL。
- 不允许写入、修改或删除 LIS 中的报告结果、审核状态、发送状态、质控样本号、项目结果、仪器配置和人员字典。
- 不允许为了保存质控批号、靶值、SD、失控处理记录、缓存结果或查询历史而写 LIS 表。
- 所有本软件产生的数据只能写入本机 SQLite、配置文件，或后续独立于 LIS 的集中配置服务。

后续代码实现时，所有 LIS 查询接口命名和注释都应明确为 read-only；如果新增数据库访问 helper，默认不得暴露写 LIS 能力。任何涉及 `INSERT / UPDATE / DELETE / MERGE` 的 SQL 只能作用于本机 SQLite，不能作用于 LIS 连接。

当前主流程为：

```text
系统设置维护质控样本号 -> 导入质控只读同步本机 SQLite 镜像 -> 查询读取 SQLite -> 内存计算统计和规则 -> 展示/导出/回跳常规报告
```

SQLite 是当前页面的本机查询入口：

- 保存本机质控品配置。
- 长期保存已同步的 LIS 质控结果镜像。
- 支持离线临时查看，但必须明确标识“缓存数据”。
- 提供手动导入质控的入口，镜像不得替代 LIS 事实来源。
- SQLite 缓存、配置和批号记录均为本机数据，不得反向写入 LIS。

第一阶段目标：

- 不写入、不更新、不删除任何 LIS 业务表；LIS 访问只读。
- 在系统设置中维护“仪器 + 固定样本号 + 项目/水平”的质控品配置。
- 质控分析页打开后不自动查询；用户选择仪器后点击“查询”只读取本机 SQLite，点击“导入质控”弹出日期范围选择，确认后按配置和所选日期范围只读查询 LIS 明细并更新镜像；导入完成后不改动主页面日期筛选，也不自动刷新当前卡片。
- 查询结果在内存中聚合、排序、统计并展示。
- 展示质控明细表。
- 预留 Levey-Jennings 质控图入口，完整绘图放到阶段 2。
- 预留 Westgard 规则状态字段，完整规则判断放到阶段 3。
- 支持从明细双击回跳常规报告并定位原始报告。
- 在本机 SQLite 中维护质控批号、开始日期、可空结束日期、靶值和 SD；LIS 仍作为质控结果事实来源。
- 本地 SQLite 同时保存配置和质控结果镜像；镜像更新必须由 `导入质控` 触发。

非第一阶段目标：

- 不做 LIS 业务表写入。
- 不修改原始报告结果。
- 不做跨电脑共享的批号生命周期管理。
- 不做自动审核、自动拦截或自动发送控制。
- 不做复杂月报、年报和监管格式报表。
- 不要求多台电脑共享同一份 SQLite 文件。

## 页面入口

建议新增菜单：

```text
统计分析管理 -> 质控分析(&3)
```

当前 `统计分析3` 仍是占位模块，可替换为质控分析模块。

## 分支与改动边界

质控分析后续实现应新开独立 Git 分支开发，避免影响其他并行开发分支。推荐分支名：

```text
feat/quality-control-analysis
```

当前设计文档分支：

```text
feat/quality-control-analysis-design
```

代码实现时应尽量保持改动边界清晰：

- 新增质控分析模块、LIS 查询层和图表渲染层。
- SQLite 存储层保留配置和本机结果镜像能力，质控分析页 `查询` 以 SQLite 为入口。
- 只在必要位置接入菜单、系统设置页质控配置卡片、常规报告右键跳转入口。
- 非必要不修改常规报告、HIV 统计、急诊统计、输血查询等既有业务模块。
- 如必须修改共享结构或工具函数，应保持向后兼容，避免改变已有页面行为。
- 不做顺手重构，不做无关格式化，不批量改动现有文档和代码风格。
- 优先通过新增文件承载质控分析逻辑，让其他分支后续合并时冲突最小。

建议新增模块文件：

```text
src/quality_control_module.h
src/quality_control_module.cpp
src/quality_control_chart_renderer.h
src/quality_control_chart_renderer.cpp
src/quality_control_lis_query.h
src/quality_control_lis_query.cpp
src/quality_control_store.h        # SQLite 配置和本机结果镜像
src/quality_control_store.cpp
```

建议新增两类接口：

1. LIS 查询接口
   负责按质控配置从 LIS 查询质控结果，返回内存行集。

2. 分析接口
   负责对内存行集做分组、统计、规则判断和图表数据准备。

```cpp
struct QualityControlQuery;
struct QualityControlRule;
struct QualityControlPoint;
struct QualityControlSummary;
struct QualityControlAnalysisResult;

bool query_quality_control_points_from_lis(
    const search::DbSettings& db,
    const QualityControlQuery& query,
    std::vector<QualityControlPoint>& rows,
    std::string& error,
    LogFn log = {});

bool analyze_quality_control_points(
    const QualityControlQuery& query,
    const std::vector<QualityControlPoint>& rows,
    QualityControlSummary& summary,
    QualityControlAnalysisResult& result,
    std::string& error);
```

## 总体布局

参考 `temp/参考1.png` 和 `temp/参考2.png` 后，质控分析页建议采用“两级视图”：

1. 今日质控概览  
   参考 `参考1.png` 的实时监控页，用卡片快速呈现当前仪器、当前批次或当前日期下各项目质控状态。

2. 项目质控详情  
   参考 `参考2.png` 的项目 L-J 图窗口，围绕某个项目展示 L-J 图、数据列表、规则判定和查询记录。

第一阶段先实现今日质控概览的筛选区、分组/明细列表和 LIS 直查。L-J 图可以预留入口，但完整绘图放到第二阶段。

推荐主页面结构：

```text
┌────────────────────────────────────────────────────────────┐
│ 日期范围  院区/检验科  仪器  质控品配置  项目  水平  查询 │
│ 查询/导入质控  最近查询时间  只看异常/失控                 │
├───────────────┬────────────────────────────────────────────┤
│ 项目/水平列表 │ 今日质控概览卡片区                         │
│               │ 无数据 / 在控 / 警告 / 失控未处理 / 已处理  │
├───────────────┴────────────────────────────────────────────┤
│ 质控明细表：时间、样本号、报告号、项目、结果、查询来源     │
└────────────────────────────────────────────────────────────┘
```

项目详情可使用独立弹窗或页内 Tab：

```text
┌────────────────────────────────────────────────────────────┐
│ L-J图 | 数据列表 | 规则判定 | 查询记录                     │
├────────────────────────────────────────────────────────────┤
│ 开始日期 结束日期  水平 L1 L2 L3  查询  导入质控  选项       │
├────────────────────────────────────────────────────────────┤
│ 水平1 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
│ 水平2 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
│ 水平3 L-J图：均值 / ±1SD / ±2SD / ±3SD                     │
└────────────────────────────────────────────────────────────┘
```

## 顶部筛选区

建议控件：

| 控件 | 说明 |
| --- | --- |
| 开始日期 / 结束日期 | 默认最近 30 天，后续可按现场习惯改为当月 |
| 院区 / 检验科 | 全部、老院、新院或具体检验科 |
| 仪器 | 复用常规报告仪器字典逻辑，按 `LS_AS_ROOM / LS_AS_MACHINE` 加载；选中后显示仪器名称，内部保留仪器代码用于查询 |
| 质控品配置 | 从系统设置读取，如“全部质控品 / 血常规 L1 / 血常规 L2” |
| 项目 | 不在顶部单独输入项目代码；项目查看和定位通过卡片选择完成 |
| 水平 | 全部、L1、L2、L3 |
| 只看失控 | 勾选后明细只显示命中失控规则的点 |
| 查询 | 只读取本机 SQLite 质控结果镜像，并套用当前配置重新分析 |
| 导入质控 | 弹出日期范围选择，默认当天；确认后按当前仪器和已配置质控样本号只读拉取 LIS 明细并更新 SQLite 镜像；不改动主页面日期筛选，不自动刷新当前卡片 |
| 最近查询 | 显示本机数据最近导入质控的时间、返回点数和耗时 |
| 导出 Excel | 阶段 1 导出当前明细视图；如果已选卡片，则只导出该卡片对应项目明细 |

参考图中的“部门、仪器组、仪器、批”筛选链路可以简化为本项目的实际配置：`院区/检验科 -> 仪器 -> 质控品配置/样本号 -> 项目/水平`。第一阶段不强制实现“批”概念，除非现场能提供稳定批号来源。

## 右侧项目列表与明细抽屉

主页面不再常驻左侧分组列表。参考图右侧项目树中 `(WBC) 白细胞`、`(RBC) 红细胞` 这类项目列表，当前实现把项目列表放到右侧操作侧栏下方，用原生 ListView 展示项目和当天状态。

侧栏项目列表层级：

```text
项目 -> 状态
```

项目列表列：

| 列 | 说明 |
| --- | --- |
| 项目 | `（项目英文名）项目名` |
| 状态 | 当前质控日卡片状态 |

点击项目列表行会同步选中中部卡片。底部明细 ListView 默认收起，右侧 `明细` 按钮展开后显示当前选中卡片对应项目的所有可见质控点，仍支持双击明细回跳常规报告。

## 今日质控概览卡片

参考 `参考1.png` 的卡片式状态区，主页面中部可展示当前质控日内的项目质控状态。

卡片建议字段：

| 字段 | 说明 |
| --- | --- |
| 项目 | 标题使用 `（项目英文名）项目名` |
| 批号 | 当前命中的质控批号；未维护则为空 |
| 靶值 | 已维护项目靶值时显示靶值；未维护时显示“均值” |
| 操作人 | 当前质控日内该项目最新一次结果对应检验者 |
| 水平状态 | L1 / L2 / L3 右侧圆点显示当天状态 |
| 状态 | 无数据 / 未判定 / 在控 / 警告 / 失控 |
| 数据来源 | LIS导入 / 本机缓存 |

卡片动作：

- `L-J图`：打开项目质控详情。
- `明细`：展开明细抽屉并定位当前项目。
- `刷新`：按该卡片对应仪器、样本号和项目重新查询 LIS。
- `处理`：后续用于失控备注，第一阶段可先预留不实现。

状态颜色建议：

| 状态 | 颜色建议 | 第一阶段含义 |
| --- | --- | --- |
| 无数据 | 灰色 | LIS 当前查询范围内没有该配置对应结果 |
| 在控 | 绿色 | 有 LIS 结果，未命中警告/失控 |
| 警告 | 黄色 | 预留；第一阶段可不计算完整规则 |
| 失控未处理 | 红色 | 预留；第一阶段可不计算完整规则 |
| 失控已处理 | 浅蓝色 | 预留；后续有处理记录后使用 |

第一阶段如果暂不做 Westgard 规则，可先按“是否查询到 LIS 结果”展示 `无数据 / 有数据`，并预留状态字段和颜色映射。

当前实现已改为卡片优先的原生 Win32 自绘状态看板：

- 顶部筛选区压缩为单行，并移除项目代码输入；左侧分组列表默认隐藏，底部明细默认收起，把主要可视面积让给中部卡片看板。
- 页面右侧参考 `参考1.png` 增加操作侧栏：上方放置日期、仪器、水平、状态和查询按钮；中部提供 `L-J图 / 明细 / 导出` 操作；下方项目列表可反向选中卡片。不再在侧栏顶部重复显示当前卡片的项目、状态、操作人和水平状态。
- 右侧概览区顶部增加摘要条，按卡片项目汇总当前质控日的失控、警告、在控、无数据和未判定数量。
- 右侧卡片按“项目”聚合，一个卡片代表一个质控项目；同一项目下的多个水平在卡片右侧用圆点显示。
- 卡片使用固定推荐宽度和高度，按窗口宽度自动换列；项目较多时卡片区使用垂直滚动条，避免在大屏上被横向拉伸导致视觉不稳定。
- 卡片整体颜色按该项目在当前质控日内各水平结果的最严重状态决定：任一水平当天失控则红色；无失控但有警告则黄色；当天有可判定结果且无红黄则绿色；当天无结果或无法判定则灰色。
- 水平圆点保留当前质控日内单水平状态：绿色为在控，黄色为警告，红色为失控，灰色为未判定/无数据。
- 卡片标题使用项目英文名和项目名，格式为 `（项目英文名）项目名`；不再混入质控名称。英文名优先取 `LS_AS_ITEM.ENG_NAME`，缺失时回退 `LS_AS_REPENTRY.ITEM_ENG`。
- 卡片内展示项目、批号、靶值、操作人和状态；靶值未维护时显示“均值”，不再显示今日结果、最近时间和点数。
- 单击卡片时同步右侧项目列表选择，并刷新可展开明细抽屉为该项目的所有可见水平结果。
- 卡片标题栏作为 L-J 图入口，标题栏右侧用小图表图标提示；鼠标只有移到标题栏时才显示手形光标，卡片正文保持默认箭头。
- 单击标题栏打开该项目的 L-J 图，多水平仍按独立曲线纵向展示。
- 当前圆点来源于 LIS 返回结果对应的水平；若要显示“固定样本号已配置但本次没有 LIS 结果”的灰色圆点，后续需要在卡片聚合时额外合并系统设置里的质控品配置。

## 右侧 Levey-Jennings 图

该部分主要参考 `参考2.png`，建议作为第二阶段重点实现。

图表核心要求：

- 横轴按检测时间或结果顺序等距展示。
- 横轴标签显示真实检测时间。
- 纵轴显示结果值。
- 显示均值线。
- 显示 `+1SD / -1SD`、`+2SD / -2SD`、`+3SD / -3SD`。
- 正常点、警告点、失控点使用不同颜色。
- 鼠标悬停或选中点时显示报告号、样本号、时间、项目、结果、Z 值和命中规则。
- 点击图中点位时，明细抽屉同步定位。
- 从明细表选择行时，图中点位同步高亮。

参考图中有 `L-J图 / 数据列表 / Youden图 / 测试信息 / 试剂信息` Tab。本项目第一版详情页建议先保留：

| Tab | 阶段 | 说明 |
| --- | --- | --- |
| L-J图 | 阶段 2 | 多水平纵向堆叠图 |
| 数据列表 | 阶段 1 | LIS 查询明细，缓存仅作可选来源 |
| 规则判定 | 阶段 3 | Westgard 命中详情 |
| 查询记录 | 后续 | 本机查询历史或缓存刷新记录 |
| Youden图 | 后续 | 暂不实现 |
| 试剂信息 | 后续 | 暂不实现，除非有稳定数据来源 |

阶段 2 首版使用 Win32 GDI 绘制，绘图逻辑集中到 `quality_control_chart_renderer.*`，避免窗口过程里混入大量绘图计算。首版从当前查询内存结果打开独立 L-J 图窗口，不重新访问 LIS。

## 明细抽屉

阶段 1 明细表列建议：

| 列 | 来源 / 说明 |
| --- | --- |
| 时间 | 优先 `LS_AS_REPORT.REP_TIME`，为空时退到 `CHK_DATE`，再退到 `REP_DATE` |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 报告号 | `LS_AS_REPORT.REP_NO` |
| 仪器 | 显示 `MACH_NAME`，内部保留 `MACH_CODE` 用于查询和回跳 |
| 检验者 | `JC_EMPLOYEE_PROPERTY.NAME`，由 `LS_AS_REPORT.OPER_CODE` 关联 |
| 项目 | `LS_AS_REPENTRY.ITEM_NAME` 或 `LS_AS_ITEM.ITEM_NAME` |
| 质控水平 | 本机配置派生，如 L1 / L2 / L3 |
| 结果 | `LS_AS_REPENTRY.RESULT` |
| 单位 | `LS_AS_ITEM.UNIT` |
| 来源 | 显示 `LIS导入` 或 `本机缓存` |

阶段 1 明细抽屉不展示 `条码号`、`审核`、`发送`。这些字段可作为后续追溯或规则扩展的内部数据，但不进入当前明细列表，避免质控分析页与常规报告页的报告流程字段混杂。

交互：

- 点击表头做内存排序，不重新访问 LIS。
- 双击明细行打开或激活常规报告页，并按 `REP_NO` 精确定位。
- 提供“导出 Excel”按钮，导出当前明细抽屉视图。
- 右键可提供“查看原始报告”“复制该点信息”等操作。

## 质控品识别规则

这是本功能最关键的边界。

质控品不通过样本号前缀、模糊匹配或报告字段自动推断，而是由用户在系统设置中明确指定：

```text
某个检验仪器 + 某几个固定样本号 = 质控结果
```

也就是说，质控品识别应使用精确配置。只有当前报告行同时满足以下条件，才视为质控点：

1. `LS_AS_REPORT.MACH_CODE` 匹配用户配置的检验仪器。
2. `LS_AS_REPORT.OPER_NO` 精确等于该仪器下配置的某个固定质控样本号。
3. 当前结果项目 `LS_AS_REPENTRY.ITEM_CODE` 在配置的质控项目范围内，或该样本号配置为“纳入全部项目”。

第一阶段建议将配置放在系统设置页，不直接写 LIS 业务数据库。配置只能保存在本机配置文件、本机 SQLite，或后续独立于 LIS 的集中配置服务中；如果后续要求多台电脑共享同一套质控品配置，应优先考虑集中配置表或轻量服务，而不是共享 SQLite 文件。

推荐的目标配置粒度应拆成三类主数据和一类批号明细，不再把“样本号、项目、批号、靶值/SD”压在同一条配置里：

| 层级 | 说明 |
| --- | --- |
| 质控样本配置 | `仪器 + 样本号`，表示这个固定样本号是某台仪器上的一个质控品入口 |
| 质控项目清单 | 该样本号下哪些 `LS_AS_REPENTRY.ITEM_CODE` 纳入质控分析；项目可从 LIS 指定日期结果读取后保存，质控名称和水平优先按项目单独维护 |
| 质控批号 | 挂在质控样本配置下，而不是挂在单个项目下；包含批号、开始日期、可空结束日期和备注 |
| 批号项目靶值 | 同一批号下，每个已纳入质控的项目单独维护靶值和 SD |

示例配置：

| 仪器 | 样本号 | 质控名称 | 水平 | 批号 | 项目 | 靶值 | SD | 启用 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4001 | 1001 | 血常规质控 L1 | L1 | BC202606 | WBC | 5.20 | 0.15 | 是 |
| 4001 | 1001 | 血常规质控 L1 | L1 | BC202606 | RBC | 4.65 | 0.12 | 是 |
| 4001 | 1001 | 血常规质控 L1 | L1 | BC202606 | HGB | 138.0 | 4.0 | 是 |

配置判定时必须使用精确样本号匹配，不使用 `LIKE`、前缀或包含匹配。这样可以避免患者样本号偶然符合文本规则而被误纳入质控分析。

如果同一台仪器的同一个固定样本号下存在多个质控项目，`读取项目` 用于一次性生成项目清单；如果读取时右侧质控名称、水平、批号、开始/结束日期、靶值、SD 或备注非空，则批量覆盖到本次读取到的项目。后续仍可选中左侧某个项目行，再精确维护该项目的质控名称、水平、批号靶值和 SD。

## 系统设置页设计

建议将“系统设置”页组织为 Tab 页，并增加独立的“质控品设置”Tab。由于系统设置页已经有常规报告条码打印机和快捷检验仪器配置，并且已具备检验仪器选择器，质控品设置应复用这套仪器选择能力。

推荐设置方式：

- 在系统设置页内提供“质控品设置”Tab，不再打开独立配置弹窗。
- Tab 左侧维护本机质控项目配置列表：仪器、样本号、项目、项目级质控名称、水平、当前批号。
- Tab 右侧维护当前样本配置下的项目、批号、有效期和靶值/SD；当前 Win32 实现采用双列表单，左列偏仪器/样本/读取项目，右列偏项目/批号/靶值，避免内嵌设置页纵向高度过长。
- 仪器字段提供“选择”按钮，复用现有检验仪器选择器填充科室代码、仪器代码和仪器名称。
- 项目清单通过 `读取项目` 按 `仪器 + 样本号 + 指定日期` 从 LIS 读取；读取时右侧非空字段会批量覆盖到本次读取到的项目，空字段不清空已有项目配置。
- 读取日期、批号开始日期和批号结束日期使用系统日历选择器；读取日期默认当天且不能为空，批号结束日期允许为空，表示当前批号仍在使用。
- 支持新增、删除、启用/停用、保存。
- 保存后广播设置变更，已打开的质控分析页下次查询时使用新配置。

质控样本列表列建议：

| 列 | 说明 |
| --- | --- |
| 启用 | 是否启用 |
| 仪器代码 | `MACH_CODE` |
| 仪器名称 | `MACH_NAME` |
| 样本号 | 固定质控样本号，精确匹配 `OPER_NO` |
| 质控名称 | 用户自定义显示名 |
| 水平 | L1 / L2 / L3 |
| 批号 | 当前或最近批号 |
| 开始日期 | 批号启用日期 |
| 结束日期 | 批号停用日期；为空显示当前 |

项目清单列建议：

| 列 | 说明 |
| --- | --- |
| 纳入 | 是否作为质控项目保存到 SQLite |
| 项目代码 | `LS_AS_REPENTRY.ITEM_CODE` |
| 项目名称 | `LS_AS_ITEM.ITEM_NAME` 或 `LS_AS_REPENTRY.ITEM_NAME` |
| 英文名 | `LS_AS_ITEM.ENG_NAME`，缺失时回退 `LS_AS_REPENTRY.ITEM_ENG` |
| 单位 | `LS_AS_ITEM.UNIT` |
| 最近结果 | 指定日期内该项目最新结果 |
| 最近时间 | 指定日期内该项目最新时间 |
| 出现次数 | 指定日期内同一项目出现次数，用于排查重复报告 |

当前批号项目靶值列建议：

| 列 | 说明 |
| --- | --- |
| 项目代码 | 来自已保存的质控项目清单 |
| 项目名称 | 便于用户核对 |
| 单位 | 便于用户填写靶值 |
| TargetMean | 当前批号下该项目靶值，可为空 |
| TargetSd | 当前批号下该项目标准差，可为空 |

如果当前批号某个项目的 `TargetMean / TargetSd` 为空，页面可以按当前查询周期自动计算均值和 SD，但必须在界面上标明“本期统计值”，不能当作正式靶值。新增下一批号时，上一条仍为空结束日期的批号自动回填为新批号开始日期的前一天；可提供“复制上一批项目靶值/SD作为初始值”的辅助动作，但保存后仍属于新批号。

## 本地 SQLite 定位

修正后的方案中，本地 SQLite 不再是质控结果的主数据源。质控结果以 LIS 手动查询结果为准，SQLite 只作为可选的单机辅助能力。

SQLite 可以承担：

- 本机质控品配置保存。
- 本机质控批号生命周期保存。
- 已手动同步过的 LIS 质控结果镜像。
- 查询历史、耗时和错误记录。
- 离线临时查看。

SQLite 不应承担：

- 多台电脑共享的质控结果主库。
- 替代 LIS 作为事实来源。
- 共享文件夹上的多人并发写入数据库。
- 向 LIS 回写配置、批号、缓存、规则判断或处理状态。

原因：

- 不同电脑各自本地 SQLite 无法天然互通，会导致质控分析结果不一致。
- Windows 局域网共享 SQLite 容易遇到锁、权限、延迟和损坏风险。
- LIS 已经是多电脑共同访问的数据源，因此 `导入质控` 必须以只读方式从 LIS 同步；日常 `查询` 为减少压力只读本机 SQLite。

建议数据库文件：

```text
data/quality_control.sqlite
```

如果安装目录不可写，可放到用户数据目录或与 `ClientConfig.ini` 同目录。具体路径通过 `ClientConfig.ini` 配置：

```ini
[QualityControl]
LocalCachePath=quality_control.sqlite
```

如果保留 SQLite，继续采用 SQLite amalgamation 源码静态编译进主程序，源码放在 `external/sqlite/`。这样 Windows/MinGW 构建不需要预装 SQLite 开发库，发布包也不需要额外携带 `sqlite3.dll`。但质控分析页不得强依赖“先导入 SQLite 才能查看”。

### 表结构

当前至少包含配置表、批号表、靶值表和本机结果镜像表；结果镜像由 `导入质控` 写入本机 SQLite。

#### `qc_sample_config`

保存系统设置页维护的质控样本识别规则：某台仪器的某个固定样本号是一个质控品入口。这一层不保存项目靶值和 SD，也不随批号更换而重复创建；质控名称和水平允许作为历史兼容默认值，但当前实现优先使用项目级配置。

```sql
CREATE TABLE IF NOT EXISTS qc_sample_config (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  enabled INTEGER NOT NULL DEFAULT 1,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  qc_name TEXT,
  level TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(mach_code, sample_no, level)
);
```

#### `qc_sample_item`

保存某个质控样本号下纳入质控分析的项目清单。项目清单由 `query_quality_control_sample_items()` 按 `仪器 + 样本号 + 指定日期` 从 LIS 读取后勾选保存，后续换批号时继续复用，不需要重新设置一次。

```sql
CREATE TABLE IF NOT EXISTS qc_sample_item (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  sample_config_id INTEGER NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  item_code TEXT NOT NULL,
  item_name TEXT,
  item_eng TEXT,
  unit TEXT,
  qc_name TEXT,
  level TEXT,
  sort_order INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(sample_config_id) REFERENCES qc_sample_config(id) ON DELETE CASCADE,
  UNIQUE(sample_config_id, item_code)
);
```

#### `qc_lot`

保存某个质控样本号下的质控品批号和有效期。批号按样本号管理，而不是按项目管理；同一批号下多个项目通过 `qc_lot_item_target` 单独维护靶值和 SD。`valid_to` 为空表示当前仍在使用；启用新批号时，上一条未结束批号自动关闭。

```sql
CREATE TABLE IF NOT EXISTS qc_lot (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  sample_config_id INTEGER NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  lot_no TEXT NOT NULL,
  valid_from TEXT NOT NULL,
  valid_to TEXT,
  note TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(sample_config_id) REFERENCES qc_sample_config(id) ON DELETE CASCADE
);
```

#### `qc_lot_item_target`

保存某个批号下各项目独立的靶值和 SD。一个质控品通常只会上机一个样本号，但这个样本号会检测多个项目，因此靶值和 SD 必须落在“批号 + 项目”这一层。

```sql
CREATE TABLE IF NOT EXISTS qc_lot_item_target (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  lot_id INTEGER NOT NULL,
  sample_item_id INTEGER NOT NULL,
  target_mean REAL,
  target_sd REAL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  FOREIGN KEY(lot_id) REFERENCES qc_lot(id) ON DELETE CASCADE,
  FOREIGN KEY(sample_item_id) REFERENCES qc_sample_item(id) ON DELETE CASCADE,
  UNIQUE(lot_id, sample_item_id)
);
```

质控分析查询 LIS 结果后，按结果有效日期匹配批号：

```text
valid_from <= 质控日期
且 (valid_to 为空 或 质控日期 <= valid_to)
```

命中批号后，再按 `item_code` 找到该批号下对应项目的靶值和 SD。未命中批号或该批号下未维护项目靶值/SD 时，该点标记为“未判定”或退回“本期统计值”，界面必须明确标识基准来源。

历史版本曾使用 `qc_config + qc_lot` 过渡结构，其中 `qc_config` 包含项目字段，`qc_lot` 挂在单个项目配置下。当前代码启动时会保留旧表并把旧数据自动迁移到目标结构：

```text
旧 qc_config 的 mach_code/sample_no -> qc_sample_config
旧 qc_config 的 item_code/item_name/item_eng/unit/qc_name/level -> qc_sample_item
旧 qc_lot 的 lot_no/valid_from/valid_to/note -> qc_lot
旧 qc_lot 的 target_mean/target_sd -> qc_lot_item_target
```

#### `qc_result_cache`

保存从 LIS 只读同步到本机的结果明细镜像。LIS 仍是事实来源；`查询` 始终从 SQLite 读取，`导入质控` 才更新 SQLite，并在界面上标识“本机缓存”或“LIS导入”。

```sql
CREATE TABLE IF NOT EXISTS qc_result_cache (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source_rep_no TEXT NOT NULL,
  source_entry_key TEXT NOT NULL,
  room_code TEXT,
  mach_code TEXT NOT NULL,
  mach_name TEXT,
  sample_no TEXT NOT NULL,
  barcode_no TEXT,
  tester_name TEXT,
  report_date TEXT,
  inspect_date TEXT,
  report_time TEXT,
  effective_time TEXT NOT NULL,
  chk_flag TEXT,
  conf TEXT,
  item_code TEXT NOT NULL,
  item_name TEXT,
  result_text TEXT,
  result_value REAL,
  has_numeric_value INTEGER NOT NULL DEFAULT 0,
  unit TEXT,
  normal TEXT,
  qc_name TEXT,
  level TEXT,
  lot_no TEXT,
  target_mean REAL,
  target_sd REAL,
  cache_key TEXT,
  deleted_in_lis INTEGER NOT NULL DEFAULT 0,
  last_seen_at TEXT,
  cached_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(source_rep_no, source_entry_key)
);
```

`source_entry_key` 用于稳定标识同一报告中的同一条明细。当前质控专用 SQL 可使用 `LS_AS_REPENTRY.ID`；如果后续遇到特殊 LIS 版本无法取到 `ID`，才退化为 `REP_NO + ITEM_CODE + 行序号`。

#### `qc_query_cache_meta`

保存某次查询条件对应的缓存元信息，用于判断相同查询条件是否可以直接复用 SQLite 数据。

```sql
CREATE TABLE IF NOT EXISTS qc_query_cache_meta (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  cache_key TEXT NOT NULL UNIQUE,
  mach_code TEXT NOT NULL,
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  level TEXT,
  item_scope TEXT,
  sample_scope TEXT,
  row_count INTEGER NOT NULL DEFAULT 0,
  latest_effective_time TEXT,
  max_entry_id TEXT,
  cached_at TEXT NOT NULL,
  refreshed_at TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT 'LIS'
);
```

`cache_key` 由查询条件归一化后生成，建议包含：

```text
mach_code
start_date
end_date
level
启用质控样本配置版本
启用项目清单版本
```

如果后续支持多仪器或多样本号批量查询，`sample_scope / item_scope` 使用排序后的样本号和项目代码摘要，避免顺序不同导致缓存误判为不同查询。

#### `qc_query_log`（可选）

记录每次 LIS 查询或缓存刷新范围和结果，便于页面显示最近查询状态。

```sql
CREATE TABLE IF NOT EXISTS qc_query_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  started_at TEXT NOT NULL,
  finished_at TEXT,
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  mach_code TEXT,
  result_count INTEGER NOT NULL DEFAULT 0,
  elapsed_ms INTEGER NOT NULL DEFAULT 0,
  source TEXT NOT NULL,
  status TEXT NOT NULL,
  error TEXT
);
```

### 索引

```sql
CREATE INDEX IF NOT EXISTS idx_qc_result_time
  ON qc_result_cache(effective_time);

CREATE INDEX IF NOT EXISTS idx_qc_result_group
  ON qc_result_cache(mach_code, sample_no, item_code, level, effective_time);

CREATE INDEX IF NOT EXISTS idx_qc_result_report
  ON qc_result_cache(source_rep_no);

CREATE INDEX IF NOT EXISTS idx_qc_result_cache_key
  ON qc_result_cache(cache_key, deleted_in_lis, effective_time);

CREATE INDEX IF NOT EXISTS idx_qc_query_cache_meta_key
  ON qc_query_cache_meta(cache_key);

CREATE INDEX IF NOT EXISTS idx_qc_sample_config_lookup
  ON qc_sample_config(mach_code, sample_no, level);

CREATE INDEX IF NOT EXISTS idx_qc_sample_item_lookup
  ON qc_sample_item(sample_config_id, item_code);

CREATE INDEX IF NOT EXISTS idx_qc_lot_dates
  ON qc_lot(sample_config_id, valid_from, valid_to);

CREATE INDEX IF NOT EXISTS idx_qc_lot_item_target_lookup
  ON qc_lot_item_target(lot_id, sample_item_id);
```

## LIS 查询设计

质控分析页默认不自动访问 LIS。用户先选择仪器，再点击“查询”或“导入质控”；查询应后台执行，完成后把结果放入内存模型，页面筛选、排序、绘图和导出优先基于当前内存结果完成。

本节所有 LIS SQL 都必须按只读查询实现。示例 SQL 只允许使用 `SELECT` 或只读 CTE；缓存、配置、批号、处理记录等任何写入 SQL 都必须走本机 SQLite 连接，不能走 LIS 连接。

推荐查询入口：

- 质控分析页增加“查询”和“导入质控”两个动作；“查询”只读取 SQLite 镜像，“导入质控”弹出日期范围选择，并按所选范围直接从 LIS 只读拉取明细。
- 页面打开后只初始化日期范围和列表，不自动查询 LIS。
- 未选择仪器时点击查询，应提示先选择检验仪器。
- 系统设置的“质控品设置”Tab 中可增加“测试查询最近 30 天”按钮，用于验证配置是否能命中 LIS 结果。
- 状态栏应显示“本地查询”或“已导入质控”等来源状态，避免用户误以为本机镜像就是实时 LIS。

查询流程：

1. 读取启用的质控样本配置和项目清单。
2. 用户点击“查询”时，按仪器和日期范围直接从 `qc_result_cache` 读取本机镜像。
3. 用户点击“导入质控”时，弹出日期范围选择，默认当天；确认后按仪器、固定样本号和所选日期范围只读查询 LIS。
4. LIS 查询结果成功返回后，按所选范围覆盖同步本机 SQLite 的 `qc_result_cache`，并更新本机 SQLite 的 `qc_query_cache_meta`。
5. 导入完成后仅更新状态栏；当前页面日期、卡片、明细和选中状态保持不变。
6. 后续用户点击“查询”时，从本机镜像读取结果并逐条解析为内存中的 `QualityControlPoint`。
7. 仅保留已保存到 `qc_sample_item` 的项目。
8. 按结果日期匹配 `qc_lot`，再按 `lot_id + sample_item_id` 匹配 `qc_lot_item_target` 的靶值和 SD。
9. 按 `mach_code + sample_no + item_code + level` 聚合。
10. 计算均值、SD、CV%、Z 值和规则状态。
11. 刷新当前页面列表、概览卡片和明细。

内存结果必须去重：同一个 `REP_NO + REPENTRY.ID` 在一次查询结果中只保留一条。缓存写入也必须幂等。

### LIS 查询 SQL

查询基础表：

- `LS_AS_REPORT r`
- `LS_AS_REPENTRY e`
- `LS_AS_ITEM i`
- `LS_AS_MACHINE m`
- 必要时关联 `LS_AS_BARCODE b`

SQL 方向：

```sql
SELECT
  CAST(e.ID AS varchar(30)) AS ENTRY_ID,
  r.REP_NO,
  r.OPER_NO,
  r.TXM_NO,
  r.ROOM_CODE,
  r.MACH_CODE,
  m.MACH_NAME,
  r.REP_DATE,
  r.CHK_DATE,
  r.REP_TIME,
  r.CHK_FLAG,
  r.CONF,
  e.ITEM_CODE,
  ISNULL(i.ITEM_NAME, e.ITEM_NAME) AS ITEM_NAME,
  ISNULL(NULLIF(RTRIM(i.ENG_NAME), ''), ISNULL(e.ITEM_ENG, '')) AS ITEM_ENG,
  e.RESULT,
  i.UNIT,
  e.NORMAL
FROM LS_AS_REPORT r WITH (NOLOCK)
JOIN LS_AS_REPENTRY e WITH (NOLOCK)
  ON e.REP_NO = r.REP_NO
 AND ISNULL(e.DELETE_BIT, 0) = 0
LEFT JOIN LS_AS_ITEM i WITH (NOLOCK)
  ON i.ITEM_CODE = e.ITEM_CODE
 AND ISNULL(i.DELETE_BIT, 0) = 0
LEFT JOIN LS_AS_MACHINE m WITH (NOLOCK)
  ON m.MACH_CODE = r.MACH_CODE
 AND ISNULL(m.DELETE_BIT, 0) = 0
WHERE ISNULL(r.DELETE_BIT, 0) = 0
  AND r.CHK_DATE >= @start_date
  AND r.CHK_DATE < DATEADD(day, 1, @end_date)
  AND r.MACH_CODE = @mach_code
  AND r.OPER_NO IN (...)
  AND (
    e.ITEM_CODE IN (...)
    OR @include_all_items = 1
  )
ORDER BY e.ITEM_CODE, r.CHK_DATE, r.REP_NO;
```

查询实现时仍沿用项目现有 SQL 拼接工具风格，并注意：

- 所有文本条件必须 `trim` 和转义。
- 日期范围默认按 `CHK_DATE` 查询。
- 质控图显示时间优先使用 `REP_TIME`，其次 `CHK_DATE`，最后 `REP_DATE`。
- 质控样本号必须按用户配置的 `MACH_CODE + OPER_NO` 精确匹配。
- 结果必须保留原始文本，数值解析失败的记录显示在明细中，但不参与图表和规则判定。
- LIS 查询连接只允许执行只读 SQL，不允许复用该连接保存缓存、配置或处理记录。

### 指定日期读取质控项目

系统设置页需要新增项目读取能力，用于解决“一个质控样本号下有多个检测项目，用户需要选择哪些项目纳入质控”的问题。新增查询接口：

```cpp
struct QualityControlSampleItemsQuery {
    std::string connection_string;
    std::string inspect_date;
    std::string mach_code;
    std::string sample_no;
};

struct QualityControlSampleItemRow {
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string unit;
    std::string latest_result;
    std::string latest_time;
    std::string latest_rep_no;
    std::string latest_entry_id;
    int point_count = 0;
};

bool query_quality_control_sample_items(
    const QualityControlSampleItemsQuery& query,
    std::vector<QualityControlSampleItemRow>& rows,
    std::string& error,
    LogFn log = {}
);
```

查询条件使用指定日期，不使用日期范围：

```text
r.MACH_CODE = 指定仪器
r.OPER_NO = 指定样本号
r.CHK_DATE >= 指定日期
r.CHK_DATE < 指定日期 + 1
```

SQL 方向：

```sql
WITH src AS (
  SELECT
    CAST(e.ID AS varchar(30)) AS entry_id,
    CAST(r.REP_NO AS varchar(30)) AS rep_no,
    CAST(e.ITEM_CODE AS varchar(20)) AS item_code,
    ISNULL(i.ITEM_NAME, e.ITEM_NAME) AS item_name,
    ISNULL(NULLIF(RTRIM(i.ENG_NAME), ''), ISNULL(e.ITEM_ENG, '')) AS item_eng,
    ISNULL(RTRIM(i.UNIT), '') AS unit,
    ISNULL(e.RESULT, '') AS result_text,
    CONVERT(varchar(19), COALESCE(r.REP_TIME, r.CHK_DATE, r.REP_DATE), 120) AS effective_time,
    ROW_NUMBER() OVER (
      PARTITION BY e.ITEM_CODE
      ORDER BY COALESCE(r.REP_TIME, r.CHK_DATE, r.REP_DATE) DESC, r.REP_NO DESC, e.ID DESC
    ) AS rn,
    COUNT(*) OVER (PARTITION BY e.ITEM_CODE) AS point_count
  FROM LS_AS_REPORT r WITH (NOLOCK)
  JOIN LS_AS_REPENTRY e WITH (NOLOCK)
    ON e.REP_NO = r.REP_NO
   AND ISNULL(e.DELETE_BIT, 0) = 0
  LEFT JOIN LS_AS_ITEM i WITH (NOLOCK)
    ON i.ITEM_CODE = e.ITEM_CODE
   AND ISNULL(i.DELETE_BIT, 0) = 0
  WHERE ISNULL(r.DELETE_BIT, 0) = 0
    AND r.MACH_CODE = @mach_code
    AND r.OPER_NO = @sample_no
    AND r.CHK_DATE >= @inspect_date
    AND r.CHK_DATE < DATEADD(day, 1, @inspect_date)
)
SELECT
  item_code,
  item_name,
  item_eng,
  unit,
  result_text AS latest_result,
  effective_time AS latest_time,
  rep_no AS latest_rep_no,
  entry_id AS latest_entry_id,
  point_count
FROM src
WHERE rn = 1
ORDER BY item_code;
```

设置页读取后，项目列表默认只展示本次指定日期实际检出的项目。当前实现会按 `mach_code + sample_no + level` 复用已有 `qc_sample_config`，并将读取到的项目写入或更新 `qc_sample_item`；后续换批号时不再强制重新读取项目，除非现场项目组成变化。`读取项目` 会把右侧非空字段批量覆盖到本次读取到的项目，空字段不清空已有项目配置；用户也可以选中具体项目行后再精确维护。

这里的“写入 `qc_sample_item`”指写入本机 SQLite，不得写入 LIS。

## 缓存查询设计

质控结果的事实来源仍然是 LIS，但为减少重复查询压力，可以把相同查询条件下的 LIS 结果写入本机 SQLite。后续相同条件查询时，先判断缓存是否可用；可用则直接读取 SQLite，不可用或用户要求刷新时再查询 LIS。

页面区分两个动作：

| 动作 | 行为 |
| --- | --- |
| 查询 | 只读取本机 SQLite 质控结果镜像，不访问 LIS；读取后按当前批号、靶值和 SD 重新分析 |
| 导入质控 | 弹出日期范围选择，默认当天；确认后按当前仪器和已配置质控样本号只读拉取 LIS 明细并更新 SQLite 镜像；不改动主页面日期筛选，不自动刷新当前卡片 |

界面必须明确显示当前数据来源：

```text
来源：LIS导入
来源：本机缓存，最近导入质控 2026-06-22 09:30:00
本地查询暂无该范围质控数据，请先导入质控
已导入质控，当前页面日期和卡片结果未改变，点击“查询”可读取本地数据
```

当前实现不自动访问 LIS。SQLite 作为本机质控结果镜像长期保留；用户需要更新事实数据时手动点击 `导入质控`。

缓存读取 SQL：

```sql
SELECT *
FROM qc_result_cache
WHERE deleted_in_lis = 0
  AND mach_code = @mach_code
  AND effective_time BETWEEN @start_date AND @end_date
ORDER BY mach_code, item_code, level, effective_time, source_rep_no;
```

当前缓存回写规则：

1. LIS 查询成功后，以 `source_rep_no + source_entry_key` 作为唯一键 upsert 到本机 SQLite 的 `qc_result_cache`。
2. 同步更新 `cache_key / cached_at / updated_at / last_seen_at`，其中 `cache_key` 只表示刷新范围，不包含批号/靶值配置签名。
3. 本次刷新范围内 LIS 没返回、但 SQLite 里曾存在的记录，只在本机 SQLite 中标记 `deleted_in_lis=1` 或从该缓存范围移除，避免 LIS 修改或删除后仍显示旧数据。
4. 更新本机 SQLite 中 `qc_query_cache_meta` 的 `row_count / latest_effective_time / max_entry_id / refreshed_at`；其中 `latest_effective_time` 保存该范围内最大报告时间 `MAX(REP_TIME)`，字段名沿用旧表结构。

时间口径已拆分：

- 质控日期归属、日期范围筛选、批号有效期匹配仍使用 `LS_AS_REPORT.CHK_DATE` 写入的 `effective_time`。
- 每个质控数据点的显示时间、卡片最近结果、L-J 图点位时间、Excel 时间列和规则顺序优先使用常规报告页同源的 `LS_AS_REPORT.REP_TIME`。
- `REP_TIME` 为空时，显示侧才回退到 `effective_time`。

`导入质控` 不再做自动跳过；用户选择日期范围后，程序按该范围直接从 LIS 只读拉取质控明细并覆盖同步本机 SQLite。默认范围为当天，因此日常使用的 LIS 压力可控；需要补历史数据时由用户主动选择更大的日期范围。

内存分析层负责：

- 按当前筛选条件返回质控点。
- 按 `mach_code + item_code + level` 聚合分组。
- 计算均值、SD、CV%、Z 值和 Westgard 规则。
- 标记每个点的正常、警告或失控状态。
- 标识数据来源：LIS导入 / 本机缓存。

## 统计和质控计算

每个分组按以下键聚合：

```text
RoomCode + MachCode + ItemCode + Level
```

基础指标：

- 点数 N。
- 均值 Mean。
- 标准差 SD。
- 变异系数 CV%。
- 最小值、最大值。
- 警告点数。
- 失控点数。

Z 值：

```text
Z = (ResultValue - TargetMean) / TargetSd
```

如果命中当前结果日期对应批号，且该批号下当前项目维护了靶值和 SD，优先使用 `qc_lot_item_target` 的配置值。否则使用当前查询结果计算均值和 SD，并在界面上标明“本期统计值”。

## Westgard 规则第一版

第一版建议实现以下规则：

| 规则 | 类型 | 说明 |
| --- | --- | --- |
| `1-2s` | 警告 | 单个质控点超过 ±2SD |
| `1-3s` | 失控 | 单个质控点超过 ±3SD |
| `2-2s` | 失控 | 连续两个点在同侧超过 ±2SD |
| `R-4s` | 失控 | 相邻两个点一高一低，跨度超过 4SD |
| `4-1s` | 警告或失控 | 连续四个点在同侧超过 ±1SD，具体口径可配置 |
| `10x` | 警告或失控 | 连续十个点在均值同侧，提示系统偏移 |

规则判断应保留命中详情，不只给最终状态。例如：

```text
状态：失控
命中规则：1-3s, 2-2s
```

## 与常规报告联动

建议实现两个方向的联动。

从常规报告到质控分析：

- 在常规报告右侧列表右键菜单增加“查看质控趋势”。
- 如果当前报告行的 `MACH_CODE + OPER_NO` 匹配系统设置中的质控品配置，则打开质控分析页并带入仪器、样本号、项目和日期范围。
- 如果不匹配质控品配置，提示“当前样本未在系统设置中配置为质控品”。

从质控分析回到常规报告：

- 明细表双击打开或激活常规报告。
- 通过 `REP_NO` 精确定位对应报告。
- 如果缺少 `REP_NO`，提示无法定位原始报告。

## 配置持久化设计

第一阶段优先放在系统设置页维护，不写 LIS 业务库；所有配置持久化都只能落到本机 SQLite、配置文件或后续独立配置服务。

如果只考虑本机使用，可以保存到本地 SQLite 的 `qc_sample_config / qc_sample_item / qc_lot / qc_lot_item_target` 或 `ClientConfig.ini`。如果后续要求多台电脑共享配置，应新增集中配置方案，例如：

- 在 LIS 之外建立一张本软件专用配置表；该表不得位于原 LIS 业务库内。
- 由一个轻量 HTTP 服务统一提供配置。
- 使用可人工分发的配置导入/导出文件。

不建议把 SQLite 文件放到共享文件夹作为多人共享配置库。

`ClientConfig.ini` 可保存质控模块默认行为：

```ini
[QualityControl]
DefaultDays=30
DefaultUseConfiguredTarget=1
FourOneSAsOutOfControl=0
TenXAsOutOfControl=0
UseLocalCache=0
LocalCachePath=quality_control.sqlite
```

质控品配置应写入 `qc_sample_config / qc_sample_item / qc_lot / qc_lot_item_target`。这样比把多条配置塞进 INI 更容易做新增、删除、启用、停用、排序和后续批量维护。但需要在界面上明确这是“本机配置”，除非后续实现集中配置。

CSV 只作为批量导入/导出配置的格式，不作为运行时主配置源：

```csv
MachCode,SampleNo,QcName,Level,LotNo,ValidFrom,ValidTo,ItemCode,ItemName,TargetMean,TargetSd,Enabled
4001,1001,血常规质控 L1,L1,BC202606,2026-06-01,,WBC,白细胞,5.20,0.15,1
4001,1001,血常规质控 L1,L1,BC202606,2026-06-01,,RBC,红细胞,4.65,0.12,1
4001,1001,血常规质控 L1,L1,BC202606,2026-06-01,,HGB,血红蛋白,138.0,4.0,1
```

系统设置页负责读写配置，避免要求用户手工编辑 SQLite 或 CSV 文件。

## 导出设计

第一阶段使用 Excel 工作簿导出：

- 当前分组明细。
- 当前查询全部明细。
- 汇总列表。

后续再考虑：

- Excel 质控月报。
- Word/PDF 质控图报告。
- 按仪器和项目批量导出图片。

## 开发阶段建议

### 阶段 1：配置和 LIS 直查原型

阶段 1 目标是打通“配置 -> LIS 查询 -> 内存分析 -> 原始报告追溯”的最小闭环，不实现完整质控图和完整规则判断。

必须实现：

- 新增质控分析菜单和空页面。
- 在系统设置页增加“质控品设置”Tab，维护质控样本、指定日期读取项目清单、批号有效期，以及当前批号下各项目靶值和 SD。
- 已增加“查询”和“导入质控”动作；前者只读取本机 SQLite，后者按用户选择日期范围只读同步质控结果明细。
- 仪器筛选显示仪器名称，内部保留仪器代码用于查询。
- 质控分析页显示卡片看板、右侧操作侧栏和可展开明细表。
- 导出当前明细视图为 Excel 工作簿，便于现场核对查询结果。
- 显示数据来源、最近刷新时间、返回点数和耗时。
- 双击明细回跳常规报告。
- SQLite 用于配置和本机结果镜像；LIS 仍是事实来源，且只读访问。

阶段 1 暂不实现：

- Levey-Jennings 绘图。
- 完整 Westgard 规则判断。
- 失控处理记录闭环。
- 自动定时同步或定时缓存刷新。
- Youden 图、试剂信息、跨电脑共享批号管理。
- Excel/Word/PDF 正式质控报表。
- 多电脑共享配置服务。

阶段 1 验收标准：

- 用户可以在系统设置中为某台仪器配置固定质控样本号。
- 点击查询后，程序只读取匹配 `MACH_CODE + OPER_NO` 的 LIS 结果。
- 同一查询结果中不会重复显示同一个 `REP_NO + REPENTRY.ID` 质控点。
- 不同电脑在相同 LIS 数据、相同配置、相同日期范围下应得到一致结果。
- 明细行能通过 `REP_NO` 回跳常规报告。
- 除菜单、系统设置入口和必要回跳入口外，不改动其他页面行为。

### 阶段 2：质控图

- 新增 Levey-Jennings 图绘制。
- 支持分组切换。
- 支持图点和明细联动。
- 支持正常、警告、失控颜色区分。
- 支持参考图中的 L1/L2/L3 多水平纵向堆叠。
- 支持图表区垂直滚动，避免多项目或多水平挤在一个图中。

阶段 2 首版已实现：

- 在质控分析页增加 `L-J图` 入口。
- 按当前选中卡片对应项目打开独立 L-J 图窗口；未选择卡片时默认使用第一组。
- 使用当前内存查询结果绘制数值点、折线、均值线和 `±1SD / ±2SD / ±3SD` 参考线。
- 图表窗口内支持点位 Tooltip。
- 点击图中点位时同步选中下方数据列表。
- 选择下方数据列表时同步高亮图中点位。
- 同仪器同项目的多个水平可在同一个窗口内纵向堆叠显示；现场测试数据确认常见形态是一个固定样本号对应一个水平，如 `5001=L1`、`5002=L2`、`5003=L3`，因此 L-J 图聚合时应跨样本号收集同项目的多个水平。
- 多水平纵向图放在可滚动图表区内，每张水平图按图表区宽度动态计算高度，并限制在合理最小/最大值内，不因窗口缩小或水平数量增加而被压扁；窗口变大时可显示更多图，窗口变小时通过滚动查看完整图。
- 图片导出和正式报表作为后续阶段 2 增量。

### 阶段 3：规则判断

- 实现 `1-2s / 1-3s / 2-2s / R-4s / 4-1s / 10x`。
- 明细显示命中规则。
- 汇总显示警告和失控次数。
- 增加“只看失控”筛选。

阶段 3 当前已实现：

- 已覆盖单点规则 `1-2s`、`1-3s`，以及连续规则 `2-2s`、`R-4s`、`4-1s`、`10x`。
- `2-2s` 按同一质控分组内时间顺序判断；连续两个已判定点在均值同侧且均超过 `±2SD` 时，后一个点命中 `2-2s` 并按失控处理。
- `R-4s` 按同一质控分组内相邻两点判断；两个点一高一低且 Z 值跨度超过 4SD 时，后一个点命中 `R-4s` 并按失控处理。
- `4-1s` 按同一质控分组内时间顺序判断；连续四个点在均值同侧且均超过 `±1SD` 时，第 4 个点命中 `4-1s`。当前首版按警告处理；如果该点已因其他规则失控，则不降级。
- `10x` 按同一质控分组内时间顺序判断；连续十个点均在均值同侧时，第 10 个点命中 `10x`。当前首版按警告处理；如果该点已因其他规则失控，则不降级。
- 规则计算优先使用当前结果日期命中的批号项目靶值和 SD；如果未配置，则使用当前查询周期内同分组的均值和 SD。
- 内存分组统计中的均值、SD、CV% 和 L-J 图参考线使用同一套规则基准；有项目靶值时 L-J 图中心线使用靶值，不再回退本期均值，基准来源显示为 `靶值` 或 `本期`。
- 每个质控点保留 Z 值、状态和命中规则；主页面明细表、Excel 导出、L-J 图下方数据列表和图点 Tooltip 均展示这些字段。
- 卡片看板和右侧项目列表按当前质控日的点级规则汇总为 `无数据 / 未判定 / 在控 / 警告 / 失控`。
- 已增加状态筛选：`全部 / 警告+失控 / 仅失控 / 仅警告`。筛选基于当前内存结果完成，不重新查询 LIS，并同步影响卡片看板、右侧项目列表、明细、Excel 导出和 L-J 图入口。
- 规则处理流程仍作为后续阶段 3 增量。

### 阶段 4：缓存、配置和导出

- 完善质控品设置 Tab，支持批量导入/导出配置。
- Excel 导出已实现，当前导出内容跟随页面状态筛选和当前明细视图。
- 本机质控结果镜像已实现：`查询` 只读取 SQLite，`导入质控` 按用户选择日期范围拉取 LIS 明细并覆盖同步。
- 当前不使用自动刷新；SQLite 结果长期保留，事实更新由用户手动导入质控触发。
- LIS 查询成功后按 `source_rep_no + source_entry_key` upsert 到本机 SQLite 的 `qc_result_cache`，并维护 `qc_query_cache_meta`。
- 页面状态栏、明细来源列和 Excel 导出已显示 `LIS导入 / 本机缓存`。
- 不再使用自动判断；是否导入由用户通过日期范围明确控制。
- 页面必须明确展示当前数据来源和缓存时间，避免用户把 SQLite 缓存误认为实时 LIS。
- 如果需要多电脑统一配置，设计集中配置方案。
- 后续按现场格式做 Excel/Word 报表。

## 风险和待确认问题

1. 固定质控样本号如何维护  
   当前确认由用户在系统设置中指定某个检验仪器的几个样本号为固定质控结果。需要进一步确认每台仪器通常配置几个样本号、是否分水平、是否需要按项目单独设置靶值和 SD。

2. 同一样本号是否会跨项目或跨水平复用  
   如果同一个固定样本号包含多个项目，默认可全部纳入；如果同一个样本号代表多个质控水平，需要增加项目或质控名称区分。

3. 靶值和 SD 来源  
   需要确认靶值、SD 是来自质控品说明书、仪器设置、LIS 配置，还是由本软件本地维护。

4. 时间口径  
   当前已确认：查询范围和质控日期归属按 `CHK_DATE`，每个质控点的显示时间、图表时间和导出时间优先按常规报告页同源的 `REP_TIME`。

5. 同一样本多项目、多报告问题  
   如果同一个样本号存在多个报告或同一项目重复结果，需要明确取全部点、取最新点还是按 `REP_NO + ITEM_CODE` 去重。

6. 失控规则口径  
   `4-1s` 和 `10x` 在不同实验室可能作为警告或失控，需要配置化。

7. 多电脑配置一致性
   如果多台电脑都使用质控分析页，本机配置可能不一致。阶段 1 可以先接受“本机配置”，但需要在界面或文档中说明；如果现场要求统一配置，应优先做集中配置或导入/导出流程。

8. LIS 查询性能
   大日期范围、多仪器、多项目直接访问 LIS 可能较慢。当前已把动作拆成 `查询` 和 `导入质控`：日常查询读取本机 SQLite，用户需要更新事实数据时再手动导入质控。

## 推荐结论

质控分析页适合做成独立统计模块，入口放在“统计分析管理”下。第一版应坚持“不写 LIS、可追溯、设置页可维护、查询读 SQLite、导入质控只读同步”。基于用户配置的“仪器 + 固定样本号”从 `LS_AS_REPORT + LS_AS_REPENTRY` 精确同步质控点，之后在内存中完成分组、统计、Levey-Jennings 图数据准备、基础 Westgard 规则和明细回跳常规报告。

不建议第一版写 LIS 业务表、做跨电脑批号共享、自动审核控制，或把 SQLite 作为多电脑共享主库。先把“能配置、能导入质控、能查本机镜像、能分析、能画图、能判定、能追溯”做稳定，再根据现场性能和多电脑一致性需求，决定是否增加集中配置和正式报表能力。
