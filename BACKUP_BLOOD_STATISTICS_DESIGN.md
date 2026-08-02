# 备血统计设计文档

本文档记录 `统计分析管理 -> 备血统计` 的当前实现口径。模块只读查询 LIS，不写入任何业务表。

## 当前状态

- 页面入口：`统计分析管理 -> 备血统计(&4)`。
- 查询核心：`search::query_backup_blood_statistics`。
- 页面代码：`src/backup_blood_statistics_module.h/.cpp`。
- 数据来源：`LS_XK_BloodRequestApply`。
- 默认日期：当前自然月第一天至当天，打开页面后不自动查询。
- 支持申请状态、院区和“包含已删除”筛选，支持备血类型内存筛选、状态分布、状态行配色、ListView 本地排序和 UTF-8 BOM CSV 导出。
- MinGW Windows 全量构建已通过；真实 LIS 结果仍需现场对账。

## 字段核查结论

申请主表建表脚本 `temp/LS_XK_BloodRequestApply.sql` 已确认包含：

| 业务含义 | 字段 | 类型 |
| --- | --- | --- |
| 申请单号 | `ApplyFormNO` | `varchar(50)` |
| 申请时间 | `Apply_Time` | `datetime` |
| 申请状态 | `ApplyForm_Statue` | `varchar(10)` |
| 申请类型/输血性质 | `TranProperty` | `varchar(20)` |
| 用血备注 | `UseBloodNote` | `varchar(500)` |
| 输血目的/申请目的 | `Apply_Purpose` | `varchar(50)` |
| 申请科室 | `Apply_Dept` | `varchar(50)` |
| 紧急级别原始值 | `UrgencyLevel` | `varchar(20)` |
| 删除标志 | `Delete_Bit` | `bit` |

`TranPurpose` 不在申请主表中，它存在于 `LS_XK_BloodCrossMatch` 交叉配血表。本统计模块按申请单统计，因此读取申请主表的 `Apply_Purpose`，不读取交叉配血表的 `TranPurpose`。

`UrgencyLevel` 的现场值域未确认，且可能保存代码值。它不再参与备血统计，也不在备血统计明细中展示。

## 统计目标

统计指定申请日期范围内符合下列任一条件的申请单：

```text
申请类型：TranProperty 去空格后精确等于“备血”
或
用血备注：UseBloodNote 内容包含“备血”
或
输血目的：Apply_Purpose 内容包含“备血”
```

同一申请单可能同时满足多个条件，备血申请单总数按唯一申请单号去重，不能直接把三个分项相加。

## 统计指标

| 指标 | 口径 |
| --- | --- |
| 备血申请单总数 | 三个条件任一命中的唯一申请单数 |
| 申请类型为备血 | `LTRIM(RTRIM(ISNULL(TranProperty,'')))='备血'` |
| 用血备注含备血 | `ISNULL(UseBloodNote,'') LIKE '%备血%'` |
| 输血目的含备血 | `ISNULL(Apply_Purpose,'') LIKE '%备血%'` |
| 多项命中 | 同时命中两个或三个条件的唯一申请单数 |
| 空申请单号异常 | 命中备血条件但 `ApplyFormNO` 为空的物理行数 |

备血申请单总数直接按三个条件的并集去重计算；“多项命中”用于解释分项重叠，不参与总数相加。

## 时间和状态口径

时间字段使用：

```text
LS_XK_BloodRequestApply.Apply_Time
```

日期范围按自然日左闭右开：

```sql
a.Apply_Time >= @start_date
AND a.Apply_Time < DATEADD(day, 1, @end_date)
```

结束日期当天全部纳入统计，并避免对 `Apply_Time` 使用日期转换函数。

申请状态默认选择“全部”，也可精确筛选 `未审核 / 已审核 / 已完结 / 已驳回`。

默认删除范围条件为：

```sql
ISNULL(a.Delete_Bit,0)=0
AND LTRIM(RTRIM(ISNULL(a.ApplyForm_Statue,'')))<>'已删除'
```

勾选“包含已删除”后取消上述有效记录限制，同时纳入 `Delete_Bit=1` 或状态为“已删除”的申请。删除状态汇总采用 `Delete_Bit=1 OR ApplyForm_Statue='已删除'`，页面保留原始 `Delete_Bit` 供核查。

## 统计对象和去重

统计对象是唯一申请单，不是数据库物理行或申请成分行。

申请单键：

```text
LTRIM(RTRIM(ApplyFormNO))
```

规则：

1. 空申请单号不进入正式汇总，单列为异常行数。
2. 同一申请单号出现多行时只生成一条明细。
3. 任一重复行满足申请类型条件，该申请单即命中申请类型。
4. 任一重复行的用血备注包含“备血”，该申请单即命中用血备注。
5. 任一重复行的输血目的包含“备血”，该申请单即命中输血目的。
6. 三个布尔标志最终决定命中来源和汇总；至少两个标志为真时计入“多项命中”。

## 查询形态

主查询只读取申请主表必要字段：

```sql
SELECT
    LTRIM(RTRIM(ISNULL(a.ApplyFormNO,''))) AS ApplyFormNO,
    CONVERT(varchar(19),a.Apply_Time,120) AS ApplyTime,
    LTRIM(RTRIM(ISNULL(a.TranProperty,''))) AS TranProperty,
    LTRIM(RTRIM(ISNULL(a.UseBloodNote,''))) AS UseBloodNote,
    LTRIM(RTRIM(ISNULL(a.Apply_Purpose,''))) AS ApplyPurpose,
    LTRIM(RTRIM(ISNULL(a.ApplyForm_Statue,''))) AS ApplyStatus,
    LTRIM(RTRIM(ISNULL(a.Patient_NO,''))) AS PatientNO,
    LTRIM(RTRIM(ISNULL(a.Patient_Name,''))) AS PatientName,
    LTRIM(RTRIM(ISNULL(a.Apply_Dept,''))) AS ApplyDept,
    LTRIM(RTRIM(ISNULL(a.Apply_BedNo,''))) AS BedNo,
    CASE WHEN ISNULL(a.Delete_Bit,0)=1 THEN '1' ELSE '0' END AS DeleteBit
FROM LS_XK_BloodRequestApply a WITH (NOLOCK)
WHERE a.Apply_Time>=@start_date
  AND a.Apply_Time<DATEADD(day,1,@end_date)
  AND (
      LTRIM(RTRIM(ISNULL(a.TranProperty,'')))='备血'
      OR ISNULL(a.UseBloodNote,'') LIKE '%备血%'
      OR ISNULL(a.Apply_Purpose,'') LIKE '%备血%'
  )
  AND (@include_deleted=1 OR (
      ISNULL(a.Delete_Bit,0)=0
      AND LTRIM(RTRIM(ISNULL(a.ApplyForm_Statue,'')))<>'已删除'
  ))
ORDER BY a.Apply_Time DESC,a.ApplyFormNO;
```

实际实现沿用项目现有 ODBC 查询方式，对日期和状态文本做校验、去空格和 SQL 转义。

第一版不关联 `LS_XK_BloodRequestApplySon`，避免同一申请单多条申请成分导致行数放大。

## 页面设计

### 院区口径

院区不作为 SQL 查询条件，也不在主查询中增加 `OR + LIKE`。查询返回并按申请单号去重后，在 C++ 内存中根据 `Apply_Dept` 派生：

| 条件 | 院区 |
| --- | --- |
| `Apply_Dept` 包含“滨水” | 新院 |
| 其他情况，包括空值 | 老院 |

该规则为当前临时口径，暂不读取 `Apply_DeptID` 或关联科室字典。页面提供 `全部 / 老院 / 新院` 下拉框；C++ 派生院区后先按所选院区过滤，再计算备血口径汇总、状态分布和明细。空申请单号异常按对应物理行的 `Apply_Dept` 派生院区后计数。

### 筛选区

- 申请开始日期。
- 申请结束日期。
- 申请状态：全部、未审核、已审核、已完结、已驳回。
- 院区：全部、老院、新院；改变后点击查询重新统计。
- 包含已删除：默认不勾选；勾选后同时纳入删除标志或状态表示已删除的数据。
- 备血类型：全部、申请类型、用血备注、输血目的、多项命中。
- 查询按钮。
- 导出明细按钮。
- 查询状态文本。

申请日期、申请状态、院区或“包含已删除”变化后需要重新查询；备血类型只过滤当前内存结果，不重复访问 LIS。查询失败时保留上一轮成功结果，导出文件名继续使用上一轮成功查询的日期和院区，避免文件名与数据范围不一致。

### 汇总区

汇总列依次为：

1. 备血申请单总数。
2. 申请类型为备血。
3. 用血备注含备血。
4. 输血目的含备血。
5. 多项命中。
6. 空申请单号异常。

汇总区下方另设状态分布：未审核、已审核、已完结、已驳回、已删除和其他状态。已删除优先按 `Delete_Bit=1 OR ApplyForm_Statue='已删除'` 归类，同一申请单只进入一个状态分类。

### 明细区

| 列名 | 字段/来源 |
| --- | --- |
| 院区 | C++ 根据 `Apply_Dept` 派生 |
| 命中来源 | 申请类型、用血备注、输血目的，可组合显示 |
| 申请单号 | `ApplyFormNO` |
| 申请时间 | `Apply_Time` |
| 申请类型 | `TranProperty` |
| 用血备注 | `UseBloodNote` |
| 输血目的 | `Apply_Purpose` |
| 申请状态 | `ApplyForm_Statue` |
| 病人号 | `Patient_NO` |
| 姓名 | `Patient_Name` |
| 申请科室 | `Apply_Dept` |
| 床号 | `Apply_BedNo` |
| 删除标志 | `Delete_Bit`，显示“是/否” |

“紧急程度”列已删除。明细按申请状态使用整行背景色：未审核为橙色、已审核为蓝色、已完结为绿色、已驳回为红色、已删除为灰色；删除标志为真时优先按已删除显示。筛选区右侧参照“已签收条码查询”绘制五种状态的色块图例。包含已删除结果时状态栏显示明确提示。

明细支持点击表头本地排序。CSV 导出使用当前备血类型过滤和当前排序后的内存明细，不再次查询数据库。

顶部筛选区采用两行布局：第一行放置申请日期、申请状态和院区，第二行放置备血类型、包含已删除、查询、导出和状态图例。所有标签按当前 UI 字体动态测量宽度，并由窗口布局函数统一定位，避免高 DPI 或较大系统字体下文字截断。

## CSV 导出

导出格式：UTF-8 BOM CSV。

默认文件名带当前院区：

```text
开始日期-结束日期备血统计明细-{全部|老院|新院}.csv
```

导出的列顺序与当前页面明细列一致，包含院区、申请类型、用血备注、输血目的和删除标志，不包含紧急程度。

## 测试口径

| 场景 | 预期结果 |
| --- | --- |
| 仅 `TranProperty='备血'` | 申请类型数和总数各加 1 |
| 仅 `UseBloodNote` 包含备血 | 用血备注数和总数各加 1 |
| 仅 `Apply_Purpose` 包含备血 | 输血目的数和总数各加 1 |
| 两处或三处命中 | 对应分项及多项命中各加 1，总数只加 1 |
| 三处都不命中 | 不进入查询结果 |
| `TranProperty=' 备血 '` | 去空格后命中 |
| 用血备注为“术前备血，必要时使用” | 用血备注包含匹配后命中 |
| 输血目的为“手术备血” | 输血目的包含匹配后命中 |
| `Apply_Dept` 包含“滨水” | 院区显示“新院” |
| `Apply_Dept` 不含“滨水”或为空 | 院区显示“老院” |
| 选择“新院” | 只汇总、显示和导出院区为新院的申请单 |
| 选择“老院” | 只汇总、显示和导出院区为老院的申请单 |
| `UrgencyLevel='备血'`，另外三处不命中 | 不统计 |
| 同一申请单号重复 | 正式总数只加 1，命中标志做 OR 合并 |
| 空申请单号 | 不进入正式总数，异常数加 1 |
| 空申请单号且选择单一院区 | 按该物理行的 `Apply_Dept` 派生院区后决定是否计入异常数 |
| 状态为“已驳回”且未删除 | 默认统计，可按“已驳回”筛选 |
| `Delete_Bit=1` 或状态为“已删除”，未勾选“包含已删除” | 不统计 |
| `Delete_Bit=1` 或状态为“已删除”，已勾选“包含已删除” | 统计并计入已删除状态分布 |
| 结束日期当天 23:59:59 | 统计 |
| 结束日期次日 00:00:00 | 不统计 |

## 现场验收

正式使用前建议选择一个较短日期范围，执行以下只读值域核验：

```sql
SELECT
    LTRIM(RTRIM(ISNULL(TranProperty,''))) AS ApplyType,
    CASE WHEN ISNULL(UseBloodNote,'') LIKE '%备血%' THEN '备注含备血' ELSE '备注不含备血' END AS NoteMatch,
    CASE WHEN ISNULL(Apply_Purpose,'') LIKE '%备血%' THEN '目的含备血' ELSE '目的不含备血' END AS PurposeMatch,
    COUNT(DISTINCT NULLIF(LTRIM(RTRIM(ApplyFormNO)),'')) AS ApplyFormCount
FROM LS_XK_BloodRequestApply WITH (NOLOCK)
WHERE ISNULL(Delete_Bit,0)=0
  AND Apply_Time>=@start_date
  AND Apply_Time<DATEADD(day,1,@end_date)
GROUP BY
    LTRIM(RTRIM(ISNULL(TranProperty,''))),
    CASE WHEN ISNULL(UseBloodNote,'') LIKE '%备血%' THEN '备注含备血' ELSE '备注不含备血' END,
    CASE WHEN ISNULL(Apply_Purpose,'') LIKE '%备血%' THEN '目的含备血' ELSE '目的不含备血' END
ORDER BY ApplyFormCount DESC;
```

再与页面汇总和明细申请单号逐项核对。数据库当前从开发环境不可达，因此真实值域和最终对账仍需在现场 Windows 环境完成。
