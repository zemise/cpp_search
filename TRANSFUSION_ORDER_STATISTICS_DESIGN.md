# 输血单统计设计文档

## 1. 文档状态

本文档用于指导 `统计分析管理 -> 输血单统计` 模块的开发、测试和验收。

- 当前阶段：业务口径已冻结，第一版代码已实现，等待现场数据库对账。
- 页面性质：只读统计页，不写入 LIS 业务表。
- 数据来源：仅使用输血申请主表 `LS_XK_BloodRequestApply`。
- 统计对象：指定申请日期范围内的唯一输血申请单号 `ApplyFormNO`。
- 状态依据：申请状态字段 `ApplyForm_Statue`，已确认包含 `未审核 / 已审核 / 已完结` 等值。
- 明确取消：不再统计“已启动/未启动”，不查询交叉配血、血液制品、出库或临床输血记录。
- 参考模块：页面结构、申请状态口径、删除处理、院区筛选、明细和 CSV 主要参照 `备血统计`。

## 2. 建设目标

在指定申请日期范围内：

1. 统计唯一输血申请单总数。
2. 按申请状态统计未审核、已审核、已完结等状态的申请单数量。
3. 默认排除已驳回、已删除申请，并允许通过复选框显式纳入。
4. 根据申请科室派生院区，支持 `全部 / 老院 / 新院` 下拉筛选。
5. 提供申请单明细、表头排序、CSV 导出和输血结果查询跳转。

本模块统计的是“输血申请单及其申请状态”，不是申请成分数量、配血量、出库量或实际输注量。

## 3. 可行性结论

模块可以直接基于现有代码开发，不存在数据库字段阻塞：

- `LS_XK_BloodRequestApply.ApplyFormNO` 已用于输血结果查询、备血统计和大量输血统计。
- `Apply_Time` 已作为现有输血统计的申请日期字段。
- `ApplyForm_Statue` 已确认具有 `未审核 / 已审核 / 已完结 / 已驳回 / 已删除` 等业务值。
- `Apply_Dept` 已用于现有输血统计的院区派生。
- `Delete_Bit` 已用于现有模块的删除过滤。
- 备血统计已经实现申请单去重、状态分布、院区过滤、CSV 导出和申请单跳转，可复用其实现模式。

不需要修改 LIS 数据库结构，也不需要创建本地缓存表。

## 4. 数据来源

第一版只读取：

```text
LS_XK_BloodRequestApply
```

需要的字段：

| 字段 | 用途 |
| --- | --- |
| `ApplyFormNO` | 输血申请单号和统计主键 |
| `Apply_Time` | 申请日期范围字段 |
| `ApplyForm_Statue` | 申请状态和状态汇总依据 |
| `Patient_NO` | 病人号，仅展示 |
| `Patient_NOType` | 病人类型，仅展示 |
| `Patient_Name` | 姓名，仅展示 |
| `Apply_Dept` | 申请科室和院区派生依据 |
| `Apply_DeptID` | 申请科室 ID，预留精确院区映射 |
| `Apply_BedNo` | 床号，仅展示 |
| `Apply_Doctor` | 申请医生，仅展示 |
| `TranProperty` | 申请类型/输血性质，仅展示 |
| `Delete_Bit` | 删除标志和“包含已删除”筛选 |

第一版不关联：

- `LS_XK_BloodRequestApplySon`
- `LS_XK_BloodCrossMatch`
- `LS_XK_BloodOutInfo`
- `LS_XK_BloodInfo`

这样可以避免申请成分或多袋血数据造成申请单行数放大，也避免把交叉配血或出库状态误当作申请状态。

## 5. 统计口径

### 5.1 时间范围

按输血申请时间 `Apply_Time` 统计，使用左闭右开的自然日范围：

```sql
a.Apply_Time >= @start_date
AND a.Apply_Time < DATEADD(day, 1, @end_date)
```

开始日期和结束日期默认均为当天，结束日期当天全部纳入。

### 5.2 统计主键和去重

主统计键：

```text
LTRIM(RTRIM(ApplyFormNO))
```

规则：

1. 同一申请单号只计一个申请单，不按数据库物理行计数。
2. 空申请单号不进入正式申请单总数，单列为“空申请单号异常”。
3. 明细每个申请单号只显示一行。
4. 对重复物理行，优先补齐非空的病人、科室、床号和申请医生信息。
5. 重复行状态冲突时，优先级为 `已删除 > 已驳回 > 已完结 > 已审核 > 未审核 > 其他`，并在数据状态列标记“状态冲突”。

### 5.3 默认状态范围

默认纳入：

- 未审核
- 已审核
- 已完结

默认排除：

- 已驳回
- 已删除
- `Delete_Bit=1`

页面提供两个独立复选框：

- `包含已驳回`：默认不勾选；勾选后纳入 `ApplyForm_Statue='已驳回'`。
- `包含已删除`：默认不勾选；勾选后纳入 `ApplyForm_Statue='已删除'` 或 `Delete_Bit=1`。

其他未知状态不进入正式总数，单列为“其他状态异常”。若后续确认新的有效状态，应更新本文档和程序映射。

### 5.4 状态归类

| 判断 | 状态分类 |
| --- | --- |
| `Delete_Bit=1` 或状态为 `已删除` | 已删除 |
| 状态为 `已驳回` | 已驳回 |
| 状态为 `未审核` | 未审核 |
| 状态为 `已审核` | 已审核 |
| 状态为 `已完结` | 已完结 |
| 其他值或空值 | 其他状态异常，不进入正式申请单总数 |

删除判断优先于状态文本，避免 `Delete_Bit=1` 但状态仍保留旧值时被误归为有效申请。

### 5.5 汇总指标

| 指标 | 计算规则 |
| --- | --- |
| 输血申请单总数 | 当前日期、院区和复选框条件下的唯一有效申请单数 |
| 未审核 | 状态分类为未审核的申请单数 |
| 已审核 | 状态分类为已审核的申请单数 |
| 已完结 | 状态分类为已完结的申请单数 |
| 已驳回 | 勾选“包含已驳回”后纳入的已驳回申请单数 |
| 已删除 | 勾选“包含已删除”后纳入的已删除申请单数 |
| 其他状态异常 | 日期范围内无法映射到已知状态的唯一申请单数，不进入正式总数 |
| 空申请单号异常 | 查询范围内申请单号为空的物理行数 |

汇总必须满足：

```text
输血申请单总数
= 未审核 + 已审核 + 已完结 + 已驳回 + 已删除
```

默认未勾选已驳回和已删除时，这两个汇总值显示 `0`。

## 6. 院区口径

按申请主表的申请科室 `Apply_Dept` 派生：

| 条件 | 院区 |
| --- | --- |
| `Apply_Dept` 包含“滨水” | 新院 |
| 其他情况，包括空值 | 老院 |

页面提供 `全部 / 老院 / 新院` 下拉框。处理顺序：

1. 按日期和状态条件读取申请主表。
2. 按申请单号去重。
3. 根据 `Apply_Dept` 派生院区。
4. 按选择的院区过滤。
5. 根据过滤后的明细计算总数和状态分布。

院区暂不下推 SQL，保持与备血统计一致，避免主查询增加 `LIKE '%滨水%'` 条件。空申请科室暂归老院，并在数据状态列标记“申请科室为空”。

若后续用于正式绩效或上报，可再按 `Apply_DeptID` 关联科室字典并增加“未知院区”。

## 7. 推荐数据模型

建议在 `search_core.h` 增加：

```cpp
struct TransfusionOrderStatQuery {
    std::string connection_string;
    std::string start_date;
    std::string end_date;
    std::string campus;  // 全部/老院/新院
    bool include_rejected = false;
    bool include_deleted = false;
};

struct TransfusionOrderStatDetailRow {
    std::string campus;
    std::string apply_form_no;
    std::string apply_time;
    std::string apply_status;
    std::string patient_no;
    std::string patient_no_type;
    std::string patient_name;
    std::string apply_dept;
    std::string apply_dept_id;
    std::string bed_no;
    std::string apply_doctor;
    std::string tran_property;
    std::string data_status;
    bool delete_bit = false;
};

struct TransfusionOrderStatSummary {
    int total_count = 0;
    int unreviewed_count = 0;
    int reviewed_count = 0;
    int completed_count = 0;
    int rejected_count = 0;
    int deleted_count = 0;
    int other_status_count = 0;
    int missing_apply_form_no_count = 0;
    int conflict_count = 0;
};
```

## 8. 推荐查询方案

### 8.1 查询形态

主查询只读取申请主表必要字段：

```sql
SELECT
    LTRIM(RTRIM(ISNULL(a.ApplyFormNO, ''))) AS ApplyFormNO,
    ISNULL(CONVERT(varchar(19), a.Apply_Time, 120), '') AS ApplyTime,
    LTRIM(RTRIM(ISNULL(a.ApplyForm_Statue, ''))) AS ApplyStatus,
    LTRIM(RTRIM(ISNULL(a.Patient_NO, ''))) AS PatientNO,
    LTRIM(RTRIM(ISNULL(a.Patient_NOType, ''))) AS PatientNOType,
    LTRIM(RTRIM(ISNULL(a.Patient_Name, ''))) AS PatientName,
    LTRIM(RTRIM(ISNULL(a.Apply_Dept, ''))) AS ApplyDept,
    ISNULL(CONVERT(varchar(32), a.Apply_DeptID), '') AS ApplyDeptID,
    LTRIM(RTRIM(ISNULL(a.Apply_BedNo, ''))) AS BedNo,
    LTRIM(RTRIM(ISNULL(a.Apply_Doctor, ''))) AS ApplyDoctor,
    LTRIM(RTRIM(ISNULL(a.TranProperty, ''))) AS TranProperty,
    CASE WHEN ISNULL(a.Delete_Bit, 0)=1 THEN '1' ELSE '0' END AS DeleteBit
FROM LS_XK_BloodRequestApply a WITH (NOLOCK)
WHERE a.Apply_Time >= @start_date
  AND a.Apply_Time < DATEADD(day, 1, @end_date)
ORDER BY a.Apply_Time DESC, a.ApplyFormNO;
```

主查询读取日期范围内的全部状态，C++ 去重后再按两个复选框决定是否纳入已驳回和已删除；未知状态只计异常，不进入正式明细和总数。这样能够发现现场新增状态，又不会让未知值悄悄改变正式统计。实际实现沿用项目现有 ODBC 查询方式，对日期进行校验与 SQL 转义。

### 8.2 聚合方式

建议拆为两层：

1. `query_transfusion_order_statistics()`：连接数据库并读取申请行。
2. `build_transfusion_order_statistics()`：按申请单号去重、处理状态冲突、派生院区、过滤并计算汇总。

纯聚合函数可在非 Windows 环境执行单元测试。

### 8.3 性能原则

- `Apply_Time` 不做函数转换，保持索引可用性。
- 第一版不关联任何子表，避免行数放大和额外 JOIN。
- 后台线程执行查询，UI 线程只负责整体展示结果。
- 查询失败时保留上一轮成功结果，不能把失败显示成零。

## 9. 页面设计

### 9.1 菜单入口

- 菜单：`统计分析管理 -> 输血单统计(&6)`。
- 模块标识：`TransfusionOrderStatistics`。
- 窗口标题：`输血单统计`。

### 9.2 筛选区

第一版提供：

- 申请开始日期，默认当天。
- 申请结束日期，默认当天。
- 院区：全部、老院、新院。
- 包含已驳回，默认不勾选。
- 包含已删除，默认不勾选。
- 查询。
- 导出明细。
- 查询状态文本。

日期、院区或两个复选框变化后需要重新查询。

### 9.3 汇总区

建议分为两行或两个横向表，避免列宽过窄：

第一行主汇总：

| 输血申请单总数 | 未审核 | 已审核 | 已完结 |
| --- | --- | --- | --- |

第二行补充状态：

| 已驳回 | 已删除 | 其他状态异常 | 空单号异常 | 状态冲突 |
| --- | --- | --- | --- | --- |

### 9.4 明细区

推荐列：

| 列 | 来源/说明 |
| --- | --- |
| 院区 | 根据申请科室派生 |
| 申请单号 | `ApplyFormNO` |
| 申请时间 | `Apply_Time` |
| 申请状态 | `ApplyForm_Statue` |
| 病人号 | `Patient_NO` |
| 病人类型 | `Patient_NOType` |
| 姓名 | `Patient_Name` |
| 申请科室 | `Apply_Dept` |
| 床号 | `Apply_BedNo` |
| 申请医生 | `Apply_Doctor` |
| 申请类型 | `TranProperty` |
| 删除标志 | `Delete_Bit` |
| 数据状态 | 正常、状态冲突、申请科室为空等 |

默认按申请时间倒序，支持点击任意表头对当前内存结果排序。

### 9.5 状态颜色

沿用备血统计配色：

| 状态 | 颜色 |
| --- | --- |
| 未审核 | 浅橙 |
| 已审核 | 浅蓝 |
| 已完结 | 浅绿 |
| 已驳回 | 浅红 |
| 已删除 | 浅灰 |
| 其他状态 | 白色或单独警示色 |

颜色只作视觉提示，统计口径仍以字段判断为准。

### 9.6 明细跳转

双击未删除的申请单明细时：

1. 打开或激活 `输血结果查询`。
2. 清空病人编号、姓名等可能冲突的筛选。
3. 将申请状态设置为全部。
4. 按申请单号和申请日期精确查询。
5. 选中目标申请单并刷新详情。
6. 不自动切换右侧页签。

已删除明细不跳转，并提示输血结果查询默认不显示删除记录。

### 9.7 CSV 导出

导出当前院区、复选框条件和排序下的内存明细，不再次访问数据库。

默认文件名：

```text
开始日期-结束日期输血单统计明细-{全部|老院|新院}.csv
```

CSV 使用 UTF-8 BOM，列顺序与页面一致。文件名使用最后一次成功查询的日期和院区，避免查询失败后条件与数据不一致。

## 10. 异常数据处理

| 情况 | 处理 |
| --- | --- |
| 申请单号为空 | 不进入正式总数，计入空申请单号异常 |
| 申请状态为空或未知 | 默认不进入正式总数；核查时归入其他状态 |
| `Delete_Bit=1` 但状态不是已删除 | 按已删除处理 |
| 同一申请单存在不同状态 | 按状态优先级合并并标记状态冲突 |
| 同一申请单病人号不一致 | 保留优先非空值并标记数据冲突 |
| 申请科室为空 | 暂归老院并标记申请科室为空 |
| 申请时间为空 | 正常日期查询不会返回；若出现则标记异常 |

未知状态建议保留日志和异常数，不自动映射到未审核、已审核或已完结。

## 11. 代码改造范围

| 文件 | 改造内容 |
| --- | --- |
| `src/transfusion_order_statistics_module.h` | 新模块工厂声明 |
| `src/transfusion_order_statistics_module.cpp` | 页面、后台查询、汇总、排序、导出和跳转 |
| `src/search_core.h` | 查询、明细和汇总模型 |
| `src/search_core.cpp` | 申请主表查询及聚合实现 |
| `src/main_frame.cpp` | 增加 `IDM_STAT6` 和菜单注册 |
| `CMakeLists.txt` | 加入新模块源文件和测试目标 |
| `tests/transfusion_order_statistics_test.cpp` | 去重、状态和院区测试 |
| `README.md` | 增加功能摘要 |
| `QUERY_DESIGN.md` | 补充最终查询口径 |
| `CHANGELOG.md` | 记录新增模块 |

## 12. 开发阶段

### 阶段 1：核心查询和聚合

- 增加查询、明细和汇总模型。
- 实现申请主表只读查询。
- 实现申请单号去重、状态归类、状态冲突和院区过滤。
- 增加纯聚合单元测试。

### 阶段 2：页面接入

- 新增 `输血单统计(&6)` 菜单和 MDI 页面。
- 实现日期、院区、包含已驳回和包含已删除筛选。
- 实现汇总、明细、状态配色、本地排序和 CSV 导出。
- 实现双击申请单跳转。

### 阶段 3：现场对账

- 按日期、院区和申请状态与人工统计逐单核对。
- 检查重复申请单、删除标志和状态冲突数据。
- 检查较大日期范围的查询性能。
- 更新 README、查询设计和变更记录。

## 13. 测试用例

### 13.1 状态和去重

| 用例 | 预期 |
| --- | --- |
| 一个未审核申请 | 总数 1，未审核 1 |
| 一个已审核申请 | 总数 1，已审核 1 |
| 一个已完结申请 | 总数 1，已完结 1 |
| 已驳回且未勾选包含 | 不进入总数 |
| 已驳回且勾选包含 | 总数和已驳回各增加 1 |
| 已删除且未勾选包含 | 不进入总数 |
| 已删除且勾选包含 | 总数和已删除各增加 1 |
| `Delete_Bit=1` 但状态为已审核 | 按已删除处理 |
| 相同申请单出现两行 | 总数只增加 1 |
| 相同申请单状态冲突 | 按优先级归类并标记冲突 |
| 空申请单号 | 不进入总数，异常加 1 |
| 状态为空或未知 | 默认不进入正式总数并计入异常核查 |

### 13.2 时间和院区

| 用例 | 预期 |
| --- | --- |
| 申请时间等于开始日期 00:00 | 纳入 |
| 申请时间等于结束日期 23:59:59 | 纳入 |
| 申请时间等于结束日期次日 00:00 | 不纳入 |
| 申请科室包含“滨水” | 新院 |
| 申请科室不包含“滨水” | 老院 |
| 申请科室为空 | 老院并标记异常 |
| 选择新院 | 汇总、明细和导出仅包含新院 |
| 选择老院 | 汇总、明细和导出仅包含老院 |

### 13.3 页面行为

- 查询期间不重复启动查询线程。
- 查询失败保留上一轮成功结果。
- 总数等于各状态数量之和。
- 表头排序后 CSV 顺序与页面一致。
- 导出文件名使用最后一次成功查询条件。
- 双击未删除明细定位到正确申请单。
- 已删除明细不跳转并给出提示。
- 高 DPI 和较大字体下筛选标签、复选框和按钮不截断。

## 14. 验收标准

第一版完成需满足：

1. 统计日期严格按 `Apply_Time` 左闭右开范围处理。
2. 唯一 `ApplyFormNO` 只计一次。
3. 未审核、已审核、已完结状态统计正确。
4. 已驳回和已删除默认排除，两个复选框分别生效。
5. `Delete_Bit=1` 始终按已删除处理。
6. 输血申请单总数等于各已纳入状态数量之和。
7. 院区筛选同时影响汇总、明细和导出。
8. 至少 20 个典型申请单与人工核查结果一致。
9. 双击未删除明细能定位到正确的输血申请单。
10. 模块只读访问 LIS，不修改任何业务数据。

## 15. 暂不纳入第一版

- 已启动、未启动或启动率。
- 交叉配血数量和配血状态。
- 血液制品、申请量、审核量或实际用血量。
- 血袋出库、输血开始、完成和不良反应统计。
- 趋势图和图表分析。
- 自动生成正式上报 Word 模板。
- 写入统计缓存或定时统计任务。

后续如需上述指标，应单独确认事实表和业务口径，不直接改变本模块的申请状态统计定义。
