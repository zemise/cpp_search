# 检验结果查询复刻设计说明

本文档记录 `lis-workbench` 当前查询逻辑和截图界面字段的对应关系。

## 当前查询链路

当前程序直接查询 LIS 数据库。报告主列表以 `LS_AS_REPORT` 为主表，右侧项目明细通过 `REP_NO` 联查 `LS_AS_REPENTRY`：

```text
LS_AS_REPORT  -- REP_NO -->  LS_AS_REPENTRY
```

截至目前已确认的辅助关系：

```text
LS_AS_REPORT.TYPE       = LS_AS_PATTYPE.TYPE
LS_AS_REPORT.SEX        = LS_AS_SEX.SEX_CODE
LS_AS_REPORT.OPER_CODE  = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.REP_OPER   = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.TXM_NO     = LS_AS_BARCODE.BARCODE
LS_AS_REPORT.ROOM_CODE  = LS_AS_ROOM.ROOM_CODE
LS_AS_REPORT.MACH_CODE  = LS_AS_MACHINE.MACH_CODE
LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE
```

说明：

- `LS_AS_REPORT` 是报告主表，负责中间报告列表。
- `LS_AS_REPENTRY` 是项目明细表，负责右侧项目结果。
- `JC_EMPLOYEE_PROPERTY` 是本项目当前确认的人员字典表，用于把 `OPER_CODE` 转为检验者姓名。
- `LS_AS_BARCODE` 用于按“病人号”筛选报告，关联键为 `BARCODE = TXM_NO`。
- `LS_AS_RESULTP.EditName` 覆盖率较低，当前不再作为“检验者”主来源。

流程：

1. 左侧输入查询条件。
2. 点击 `查询(&Q)`。
3. 程序按条件查询 `LS_AS_REPORT`，展示中间报告列表。
4. 选中某条报告后，用该行 `REP_NO` 查询 `LS_AS_REPENTRY`，展示右侧项目结果。

数据库连接信息通过主界面底部 `设置` 按钮维护，并保存到程序同目录 `ClientConfig.ini`。

设置页当前只保留 `初始数据库`，它直接用于生成查询连接串里的 `Initial Catalog`。

连接串格式：

```text
packet size=4096;user id=...;password=...;data source=...;persist security info=True;initial catalog=...
```

底层再自动尝试转换为以下 ODBC 驱动候选：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

连接层会在本次运行内缓存相同连接串上一次成功的 driver candidate，后续优先尝试该候选，失败时仍回退到完整候选列表。首次分配 ODBC 环境句柄前启用 Driver Manager 连接池，并使用严格连接匹配；DBC 句柄设置 5 秒登录超时，用于限制不可达服务器、网络阻断或不可用驱动导致的建连等待，不限制 SQL 执行时长。

## 报告列表字段

当前报告列表以 `LS_AS_REPORT` 为主表，部分列通过字典表显示名称：

| 界面列 | 数据库字段 |
| --- | --- |
| 样本号 | `OPER_NO` |
| 姓名 | `NAME` |
| 条码号 | `TXM_NO` |
| 上机时间 | `CHK_DATE` |
| 性别 | `LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE`，显示 `LS_AS_SEX.SEX_NAME` |
| 年龄 | `AGE` |
| 床号 | `BED_CODE` |
| 病人类型 | `LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE`，显示 `LS_AS_PATTYPE.TYPE_NAME` |
| 检验者 | `LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，显示 `JC_EMPLOYEE_PROPERTY.NAME` |
| 审核者 | `LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，显示 `JC_EMPLOYEE_PROPERTY.NAME` |
| 项目名称 | `GROUP_NO` |
| 审核 | `CONF` |
| 确认 | `CHK_FLAG` |

## 项目明细字段

当前项目明细以 `LS_AS_REPENTRY` 为主表，项目字典来自 `LS_AS_ITEM`：

| 界面列 | 数据库字段 |
| --- | --- |
| 项目名称 | `LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE`，优先显示 `LS_AS_ITEM.ITEM_NAME` |
| 结果 | `RESULT` |
| 下限 | `DOWNBOUND` |
| 上限 | `UPBOUND` |
| 单位 | `LS_AS_ITEM.UNIT` |
| 英文名称 | `LS_AS_ITEM.ENG_NAME` |

## 当前支持的筛选条件

| 界面输入 | 查询字段 |
| --- | --- |
| 诊疗卡号 | `LS_AS_REPORT.REG_NO` |
| 条码号 | `TXM_NO` |
| 病人姓名 | `NAME LIKE '%输入%'` |
| 病人号 | `LS_AS_BARCODE.REG_NO`，并通过 `LS_AS_BARCODE.BARCODE = LS_AS_REPORT.TXM_NO` 关联到报告 |
| 样本号 | `OPER_NO` |
| 开始日期 | `CHK_DATE >= 开始日期` |
| 结束日期 | `CHK_DATE < 结束日期 + 1天` |
| 检验科室 | `ROOM_CODE` |
| 仪器 | `MACH_CODE` |
| 组合项目 | `GROUP_CODE` |
| 项目代码 | `EXISTS LS_AS_REPENTRY.ITEM_CODE` |

## 已确认的数据表作用

| 表名 | 当前用途 | 关键字段 |
| --- | --- | --- |
| `LS_AS_REPORT` | 报告主表，中间报告列表主来源 | `REP_NO`, `TXM_NO`, `REG_NO`, `OPER_NO`, `NAME`, `SEX`, `TYPE`, `ROOM_CODE`, `MACH_CODE`, `GROUP_CODE`, `OPER_CODE`, `REP_OPER`, `REP_DATE`, `CHK_DATE`, `CONF`, `CHK_FLAG`, `ZYMZ_PRINT`, `ZZJ_PRINT` |
| `LS_AS_REPENTRY` | 报告项目明细，右侧结果列表主来源 | `REP_NO`, `ITEM_CODE`, `RESULT`, `UPBOUND`, `DOWNBOUND`, `NORMAL` |
| `LS_AS_ITEM` | 项目字典，用于右侧项目名称、单位、英文名 | `ITEM_CODE`, `ITEM_NAME`, `UNIT`, `ENG_NAME` |
| `LS_AS_PATTYPE` | 病人类型字典 | `TYPE`, `TYPE_NAME` |
| `LS_AS_SEX` | 性别字典 | `SEX_CODE`, `SEX_NAME` |
| `LS_AS_ROOM` | 检验科室字典 | `ROOM_CODE`, `ROOM_NAME` |
| `LS_AS_MACHINE` | 检验仪器字典，筛选 `DELETE_BIT=0` 且 `RUL='启用'` | `ROOM_CODE`, `MACH_CODE`, `MACH_NAME`, `PY_CODE`, `WB_CODE`, `RUL` |
| `LS_AS_BARCODE` | 条码/病人号关联表，用于“病人号”和已签收条码查询 | `BARCODE`, `REG_NO`, `OPER_STATE`, `CANCEL_DATE`, `CANCEL_OPER` |
| `JC_DEPT_PROPERTY` | 科室字典，覆盖率更高，当前优先用于报告表 `DEPT_CODE -> 科室名称` | `DEPT_ID`, `NAME`, `DELETED` |
| `JC_dept_mz_zy` | 临床申请科室字典，根据 `TYPE / TYPENAME` 区分门诊或住院后解析 `DEPT_CODE -> DEPT_NAME` | 门诊：`mzksid`, `mzksmc`；住院：`zyksid`, `zyksmc`；`delete_bit` |
| `JC_EMPLOYEE_PROPERTY` | 人员字典，用于“检验者”和“审核者”显示 | `EMPLOYEE_ID`, `NAME`, `D_CODE`, `YS_CODE`, `TYPENAME` |
| `LS_AS_RESULTP` | 原候选检验者来源，但实测覆盖率低，当前不作为主来源 | `REP_NO`, `EditName`, `ChkNAME`, `TXM_NO` |
| `LS_XK_BloodRequestApply` | 输血申请主表，输血结果查询列表主来源 | `ApplyFormNO`, `Apply_Time`, `Plan_Date`, `ApplyForm_Statue`, `Patient_NO`, `Patient_NOType`, `Patient_Name`, `TranProperty` |
| `LS_XK_BloodRequestApplySon` | 输血申请子表，用于按申请单聚合申请成分 | `ApplyFormNO`, `ApplyComposition`, `ApplyNum`, `ApplyUnit` |
| `LS_XK_BloodCrossMatch` | 交叉配血记录表，用于按输血申请号关联病人和配血审核信息 | `ApplyFormNO`, `Patient_NO`, `Patient_NOType`, `Patient_Name`, `VerifyState`, `Match_Date` |

### 临床申请科室映射

`LS_AS_BARCODE.DEPT_CODE / DEPT_NAME` 表示临床申请科室，不是检验科室、签收科室或仪器科室。当前默认优先用覆盖率更高的 `JC_DEPT_PROPERTY.DEPT_ID -> NAME` 补全科室名称，查询时应排除 `DELETED=1` 的科室记录。

当确实需要按门诊/住院来源分别确认条码表申请科室时，再使用 `JC_dept_mz_zy` 作为辅助关系：

- 门诊：`DEPT_CODE` 对应 `JC_dept_mz_zy.mzksid`，显示 `JC_dept_mz_zy.mzksmc`。
- 住院：`DEPT_CODE` 对应 `JC_dept_mz_zy.zyksid`，显示 `JC_dept_mz_zy.zyksmc`。

如果 `LS_AS_BARCODE.DEPT_NAME` 已有值，当前查询展示可优先直接使用；当需要修正空值、代码值或重新计算申请科室名称时，再按上述字典关系补全。当前 `HIV 抗体检测统计` 明细的“科室”列不再依赖 `LS_AS_BARCODE.DEPT_NAME`，而是直接按 `LS_AS_REPORT.DEPT_CODE -> JC_DEPT_PROPERTY.DEPT_ID -> NAME` 取值；字典缺失时回退显示 `LS_AS_REPORT.DEPT_CODE`。

## 输血结果查询

`输血结果查询` 当前以 `LS_XK_BloodRequestApply` 为申请主表做只读检索，并通过 `LS_XK_BloodRequestApplySon.ApplyFormNO` 聚合申请成分。

### 申请主表

| 界面/含义 | 数据库字段 |
| --- | --- |
| 申请单号 | `LS_XK_BloodRequestApply.ApplyFormNO` |
| 输血申请时间 | `Apply_Time` |
| 输血计划时间 | `Plan_Date` |
| 申请状态 | `ApplyForm_Statue`，当前已确认值为 `已审核`、`未审核`、`已完结` |
| 病人号 | `Patient_NO` |
| 病人类型 | `Patient_NOType` |
| 病人姓名 | `Patient_Name` |
| 紧急程度/输血性质、申请类型当前显示来源 | `TranProperty` |
| 用血备注 | `UseBloodNote` |
| 输血目的/申请目的 | `Apply_Purpose` |
| 紧急级别原始值 | `UrgencyLevel`，当前输血查询原样显示，未确认值域 |

### 申请成分

`LS_XK_BloodRequestApplySon` 通过 `ApplyFormNO` 与申请主表关联。当前“申请成分”显示格式为：

```text
ApplyCompositionApplyNumApplyUnit;
```

如果同一申请单对应多行子表记录，则在同一列表单元格中用分号拼接。

### 备血统计

`统计分析管理 -> 备血统计` 直接读取申请主表，不关联申请成分子表。筛选时间为 `Apply_Time`，使用左闭右开的自然日范围：

```text
Apply_Time >= 开始日期
AND Apply_Time < DATEADD(day, 1, 结束日期)
```

默认只查询 `ISNULL(Delete_Bit,0)=0` 且 `ApplyForm_Statue<>'已删除'` 的有效记录；申请状态可精确筛选 `未审核 / 已审核 / 已完结 / 已驳回`。页面勾选“包含已删除”后取消有效记录限制，将 `Delete_Bit=1` 或状态为“已删除”的申请纳入查询。备血命中条件为：

```text
LTRIM(RTRIM(ISNULL(TranProperty,''))) = '备血'
OR
ISNULL(UseBloodNote,'') LIKE '%备血%'
OR
ISNULL(Apply_Purpose,'') LIKE '%备血%'
```

统计按去空格后的唯一 `ApplyFormNO` 去重。分别汇总申请类型命中、用血备注命中、输血目的命中、多项命中和任一命中的备血申请单总数；同一申请单命中多个条件时在总数中只计一次。页面另按未审核、已审核、已完结、已驳回、已删除和其他状态显示分布，并在明细与 CSV 中保留 `Delete_Bit` 删除标志。申请状态筛选为 `全部 / 未审核 / 已审核 / 已完结 / 已驳回`，备血类型筛选为 `全部 / 申请类型 / 用血备注 / 输血目的 / 多项命中`。院区下拉提供 `全部 / 老院 / 新院`；C++ 按 `Apply_Dept` 是否包含“滨水”派生新院或老院，并在派生后按所选院区过滤，再计算汇总、状态分布和明细，不在 SQL 中增加院区 `OR + LIKE`。空申请单号不进入正式总数，异常数按物理行的 `Apply_Dept` 派生院区后过滤。`UrgencyLevel` 不参与备血统计。主查询返回同批必要明细，页面备血类型筛选、排序和 CSV 导出均使用当前内存结果，不额外查询 LIS；导出文件名使用最后一次成功查询的日期和院区。双击未删除明细会通过申请单号和申请日期打开或激活“输血结果查询”，自动精确查询、选中对应行并刷新详情，但不切换其右侧页签；已删除明细不跳转。顶部采用两行筛选布局并按当前字体动态测量标签宽度。

### 输血单统计

`统计分析管理 -> 输血单统计` 仅读取 `LS_XK_BloodRequestApply`，不关联申请成分、交叉配血、出库或血袋表。统计时间按 `Apply_Time >= 开始日期 AND Apply_Time < DATEADD(day,1,结束日期)`，以去空格后的唯一 `ApplyFormNO` 为主键；空申请单号不进入正式总数并单列异常。页面默认纳入未审核、已审核和已完结，分别提供默认不勾选的“包含已驳回”和“包含已删除”，其中 `Delete_Bit=1` 或状态为已删除统一归为已删除。未知状态不进入正式申请单总数，单列为其他状态异常；同一申请单状态冲突时按 `已删除 > 已驳回 > 已完结 > 已审核 > 未审核 > 其他` 归类并显示冲突。院区在 C++ 内存中按 `Apply_Dept` 是否包含“滨水”派生为新院或老院，过滤后再计算总数和状态分布。明细支持状态配色、表头本地排序、UTF-8 BOM CSV 导出和未删除申请单跳转。

### 交叉配血记录

`LS_XK_BloodCrossMatch` 是交叉配血记录表，表内 `ApplyFormNO` 对应输血申请号，并有索引 `ApplyFormNO + Delete_Bit` 可用于按申请单关联查询。当前已确认字段含义：

| 含义 | 数据库字段 |
| --- | --- |
| 输血申请单号 | `ApplyFormNO` |
| 病人号 | `Patient_NO` |
| 患者类型 | `Patient_NOType` |
| 病人姓名 | `Patient_Name` |
| 审核状态 | `VerifyState`，常见值为 `已审核`、`未审核` |
| 配血时间 | `Match_Date` |

后续如果输血页面需要展示交叉配血状态或配血时间，可优先按 `ApplyFormNO` 关联 `LS_XK_BloodCrossMatch`，并过滤 `Delete_Bit=0`。

`输血查询` 页面当前将 `输血历史` tab 放在首位并默认展示，按当前选中申请的 `Patient_NO` 读取 `LS_XK_BloodCrossMatch`，并通过 `BloodInID` 联查出库、血袋库存和字典表来展示该病人的历史配血信息。字段顺序为：出库时间、出库人、血袋编号、产品码、血型、RH(D)、血液成分、血量、单位、配血方法、主侧结果、次侧结果、配血时间、配血者、血袋来源。其中出库时间和配血时间仍由 SQL 返回标准 `yyyy-mm-dd hh:mm:ss` 文本，列表填充时由 C++ 端格式化为 `yyyy/M/d H:mm`，避免数据库侧承担显示格式转换。

主要字段来源：
- `LS_XK_BloodCrossMatch.BloodInID = LS_XK_BloodOutInfo.BloodInID`：获取 `BloodOut_Date / BloodOut_Man`。
- `LS_XK_BloodCrossMatch.BloodInID = LS_XK_BloodInfo.ID`：获取 `BloodBagNO / CmpProductCode`，并继续关联血型、Rh、成分和来源字典。
- `LS_XK_BloodInfo.BloodTypeID = LS_XK_B_TypeInfo.ID`：获取 `Blood_Type`。
- `LS_XK_BloodInfo.RhD_ID = LS_XK_B_RhInfo.ID`：获取 `Blood_RH`。
- `LS_XK_BloodInfo.CompositionID = LS_XK_B_CompositionInfo.ID`：获取 `Blood_Composition / Norm / Unit`。
- `LS_XK_BloodInfo.SourceID = LS_XK_B_SourceInfo.ID`：获取 `Sources_Blood`。

### 查询检验结果报告列表

`输血结果查询 -> 查询检验结果` 弹窗的右侧报告列表使用专用轻量查询 `query_blood_lis_reports()`，不再复用通用 `query_reports()`。该列表只需要展示样本号、检验时间、组合项目、条码、检验者、审核者、科室代码、仪器代码，并在选择报告后按 `REP_NO` 查询明细结果，因此 SQL 只读取 `LS_AS_REPORT.REP_NO / OPER_NO / CHK_DATE / GROUP_NO / TXM_NO / OPER_CODE / REP_OPER / AGE / SEX / ROOM_CODE / MACH_CODE` 及少量人员、性别名称字段。

选择报告后的右侧详情列表复用 `query_results()` 读取 `LS_AS_REPENTRY` 明细。`参考范围` 列只做显示层拼接，按下限在前、上限在后输出为 `下限~上限`，避免把 `UPBOUND / DOWNBOUND` 的历史字段映射顺序暴露到界面。

按病人号查询时使用现场已确认同口径字段：`LS_XK_BloodRequestApply.Patient_NO = LS_AS_REPORT.REG_NO`。因此输血弹窗报告列表和右侧摘要都直接按 `LS_AS_REPORT.REG_NO` 过滤，不再绕行 `LS_AS_BARCODE.REG_NO + BARCODE/TXM_NO` 判断病人归属。该查询避开通用报告列表中的 `LS_AS_PATTYPE / LS_AS_SAMPLE / LS_AS_MACHINE` 等无关字典联查、同条码医嘱内容聚合和条码表相关子查询，减少数据库端行扩展、字符串聚合和逐行 `EXISTS` 成本。

按名字查询用于处理输血申请病人号不足或现场需要按姓名补查的场景，但姓名可能重名。当前会先用当前输血申请病人号按 `LS_AS_REPORT.REG_NO` 查询最近一条非空 `PAT_PHONE`，取到电话后，报告列表和摘要查询都会在 `NAME LIKE` 外额外下推 `LS_AS_REPORT.PAT_PHONE = 当前电话`，使列表和摘要使用同一身份约束；如果当前病人号未能取到电话，则保留原姓名查询。弹窗会在检验摘要区下方显示身份匹配可信度提示：病人号或姓名+电话匹配为绿色提示，仅姓名匹配为橙色提示。

按身份证查询用于处理同一病人多次住院号变化的场景。弹窗先使用当前输血申请病人号匹配 `ZY_INPATIENT.INPATIENT_NO`，读取该住院病人的 `SOCIAL_NO`，再查询同一 `SOCIAL_NO` 下所有非空 `INPATIENT_NO`，报告列表和摘要都用这些住院号下推 `LS_AS_REPORT.REG_NO IN (...)`。如果当前病人号在 `ZY_INPATIENT` 中未找到身份证号，则不发起宽泛查询并提示用户。身份证号仅用于数据库侧匹配，不在界面显示。

报告列表支持通过系统设置 `[LisSummary] BloodLisExcludeMachines` 排除不想展示的检验科室/仪器，过滤只作用于输血弹窗报告列表，不影响右侧血型/血常规摘要。格式为 `ROOM:;ROOM:MACH1,MACH2;ROOM:MACH`，默认 `3:;71:;8:8004`，表示排除 `ROOM_CODE=3`、`ROOM_CODE=71` 的全部仪器，并额外排除 `ROOM_CODE=8 AND MACH_CODE=8004`。配置为空或无有效片段时不追加排除条件；用户在系统设置中清空该项后会保留空值，不会被默认值覆盖。

报告列表还会复用 `[LisSummary] BloodTypeMachines / CbcMachines` 判断当前行是否属于血型仪器或血常规仪器。查询完成状态栏会显示当前列表命中的血型/血常规行数，便于核对配置是否匹配实际 `ROOM_CODE:MACH_CODE`；列表行本身保持系统默认绘制，不额外设置背景色。

### 查询检验结果摘要

`查询检验结果` 窗口右侧摘要按当前病人号或姓名查询最近一次血型鉴定、血红蛋白、血小板、不规则抗体筛查和直接抗人球蛋白试验。摘要查询使用 `LS_AS_REPORT` 联查 `LS_AS_REPENTRY`，血型、血常规、不规则抗体筛查和直接抗人球蛋白试验均按可配置 `ITEM_CODE` 匹配；血型分支、血常规分支分别下推可配置仪器范围。配置保存到 `ClientConfig.ini` 的 `[LisSummary]`：

摘要显示层由弹窗父窗口在 `WM_PAINT` 中一次性绘制四行文本，不再用多个 `STATIC` 控件拼接日期和值。血型、血常规、不规则和直抗均使用“日期在前、加粗结果在后”的同一显示样式。这样查询开始时的“正在读取最近检验摘要...”和查询完成后的摘要结果只刷新同一块摘要区域，减少耗时查询期间控件各自擦除、重排导致的文字短暂断裂。

| 配置项 | 默认值 | 说明 |
| --- | --- | --- |
| `BloodTypeMachines` | `11:11101;64:626` | 血型鉴定摘要仪器范围 |
| `CbcMachines` | `1:1002,1011,1012;61:613,615` | 血红蛋白/血小板摘要仪器范围 |
| `IrregularAntibodyCodes` | `11106;91966` | 不规则抗体筛查项目代码 |
| `DirectAntiglobulinCodes` | `11105;91965` | 直接抗人球蛋白试验项目代码 |
| `BloodLisExcludeMachines` | `3:;71:;8:8004` | 输血弹窗报告列表排除科室/仪器；清空则不排除 |

仪器范围使用成对表达：分号分组，冒号左侧为 `ROOM_CODE`，冒号右侧为该科室下允许的 `MACH_CODE`，多个仪器用逗号分隔。程序只接受数字片段，非法片段忽略；配置为空或无有效片段时不限制仪器。该过滤用于减少无关报告参与聚合。当前按现场确认口径执行强过滤：如果某一侧摘要在限定仪器范围内查不到，不再自动去掉仪器条件做二次兜底查询。

### HIV 统计中的已完结输血单申请号

`HIV 抗体检测统计` 下方明细列表中的“病人号”列直接显示 `LS_AS_REPORT.REG_NO`。“已完结输血单申请号”列用于辅助核对 HIV 明细是否存在已完结输血申请。当前改为直接使用输血申请主表 `LS_XK_BloodRequestApply`，不再绕行 `LS_XK_BloodCrossMatch` 交叉配血表；申请主表自身包含唯一病人号 `Patient_NO`，因此匹配逻辑可简化为按病人号匹配，不再依赖姓名。为避免对每条 HIV 明细逐行扫描输血申请表，当前先按统计月份一次性取出当月已完结输血申请，再在 C++ 内存中按病人号匹配到明细行。当前匹配规则：

```text
LS_XK_BloodRequestApply.Apply_Time >= 当前统计月第一天
LS_XK_BloodRequestApply.Apply_Time <  下月第一天
LS_AS_REPORT.REG_NO = LS_XK_BloodRequestApply.Patient_NO
LS_XK_BloodRequestApply.ApplyForm_Statue = '已完结'
LS_XK_BloodRequestApply.Delete_Bit = 0
```

匹配后显示 `LS_XK_BloodRequestApply.ApplyFormNO`。如果同一病人匹配到多个已完结申请单号，则在同一单元格中用分号拼接。该列只读展示，不修改输血表数据。

上方汇总表中的“受血（制品）前检测”行按下方明细“已完结输血单申请号”列是否非空统计：初筛检测数为该列非空的明细行数，初筛阳性数为这些明细行中 `阳性='是'` 的行数。

上方汇总表中的“术前检测”行按剩余量统计：初筛检测数为总初筛检测数减去“受血（制品）前检测 / 性病门诊 / 其他就诊检测 / 孕产期检查”的初筛检测数，初筛阳性数同理用总初筛阳性数减去这些来源的初筛阳性数；如果分类之间未来出现交叉导致结果为负，则按 0 显示。

HIV 明细查询的性能策略：

- 第一步只查 `LS_AS_REPORT`：按月份、审核状态、发送状态、姓名非空、三组 `MACH_CODE` 和可选 `DEPT_CODE IN (...)` 筛出候选 `REP_NO`。
- 第二步按候选 `REP_NO` 分批查询 `LS_AS_REPENTRY` 中三个目标 `ITEM_CODE`，每批最多 500 个 `REP_NO`，再在 C++ 中按报告仪器匹配对应 HIV 项目。
- 明细“方法学”列不额外查表，按 `MACH_CODE` 固定派生：`4005`、`914` 显示 `化学发光法`，`4008` 显示 `酶免法`。
- 三组候选报告按 `MACH_CODE` 拆成三段 `UNION ALL`，避免一个大 `OR` 条件影响 SQL Server 执行计划。
- 报告主表查询不直接 JOIN `LS_AS_REPENTRY`，避免在月度范围上直接联查大明细表。
- `LS_AS_MACHINE`、`LS_AS_PATTYPE`、`JC_DEPT_PROPERTY` 先作为小字典查询到 C++ 内存中，再对明细行做名称映射。
- `全部 / 新院 / 老院` 来源筛选先根据 `JC_DEPT_PROPERTY.NAME` 分出新院 `DEPT_ID` 集合。选择 `新院` 时下推 `r.DEPT_CODE IN (新院代码...)`；选择 `老院` 时下推为 `DEPT_CODE` 为空/`NULL` 或 `r.DEPT_CODE NOT IN (新院代码...)`，使字典缺失、字典名称为空和代码为空的报告都与 C++ 侧“科室名称不含滨水新城即老院”的兜底一致；主 SQL 仍避免使用 `LIKE '%滨水新城%'`。
- 下方明细在“方法学”前展示“样本来源”列，按当前汇总分类口径落单行分类：已匹配已完结输血申请号优先显示 `受血（制品）前检测`，其次按科室文字显示 `性病门诊`、`其他就诊检测`、`孕产期检查`，剩余显示 `术前检测`。
- 上方样本来源分类表采用 UI 专用显示顺序，将 `合计` 放在第一行并用浅蓝背景突出；DOCX 导出仍保留原模板占位符顺序，不跟随 UI 显示顺序改变。
- 上方样本来源分类表下方额外展示一个独立的“方法学”小汇总表，不改变原样本来源分类表列结构；当前按下方明细行的“方法学”统计 `化学发光法` 和 `酶免法` 的初筛检测数、初筛阳性数。
- 下方明细双击行会复用常规报告的 `RegularReportOpenTarget + WM_REGULAR_OPEN_REPORT` 机制，携带 `REP_NO`、`OPER_NO`、`MACH_CODE`、`MACH_NAME`、`ROOM_CODE` 和 `REP_TIME` 跳转到 `常规报告` 并定位目标报告。

HIV 统计表导出：

- `导出统计表` 按钮只使用当前页面已加载的 `HivStatSummary` 汇总数据，不额外访问数据库。
- HIV 统计表 DOCX 模版不随项目、安装包或更新包发布。用户在页面点击 `上传模版` 选择本地 DOCX 后，程序会校验 `{}` 占位符数量并复制到安装目录 `templates\HIVStatisticsTemplate.docx`；未检测到匹配模版时 `导出统计表` 按钮不可用。模板内使用从上到下、从左到右的 `{}` 占位符；客户端不依赖 Office COM 自动化，而是读取 DOCX 包并替换 `word/document.xml` 中的占位符生成新文件，从而保留模板原有版式、字体、合并单元格和页边距。
- 导出时选择目标文件夹，默认文件名为 `YYYY年M月HIV抗体检测统计表.docx`。
- 导出内容只填写统计汇总表；非合计行数字为 `0` 时导出为空，合计行保留 `0`；WB 检测数、复检数、报告疫情检测数等未接入字段暂留空。
- `导出明细表` 按钮将下方当前 listview 明细导出为 UTF-8 BOM CSV，默认文件名为 `YYYY年M月HIV检测明细表 - 全部/新院/老院.csv`；导出使用当前已加载并已排序的内存明细，包含“样本来源”列，不额外访问数据库。
- 当前占位符顺序为：统计年份、统计月份、院区文字、20 行样本来源分类的 `初筛检测数/初筛阳性数`、填报日期年/月/日。

## 已签收条码查询

工具菜单中的 `已签收条码查询` 以 `LS_AS_BARCODE` 为主表做只读检索，不自动执行查询，等待用户点击 `查询` 或 `刷新`。当前开放查询、刷新和导出能力，取消签收、取消医嘱签收、取消原因限制按钮保持禁用，不执行数据库修改。

查询执行时使用后台线程读取数据库。为减少慢查询期间的视觉闪烁，页面会保留上一轮列表直到新查询成功返回；成功后再暂停 ListView 重绘，清空旧行并一次性填充新结果，最后恢复绘制并统一刷新。

日期类型下拉框按 `申请日期 / 签收日期 / 上机日期` 展示，默认选中 `签收日期`；开始和结束控件使用日期时间选择器，默认当天 `00:00` 至 `23:59`，查询结束条件按 `< DATEADD(minute,1,结束时间)` 处理。条形码、姓名和病人号输入框按回车会直接触发同一查询路径。

第一行筛选顺序为日期范围、条形码、姓名、病人号、院区、专业组和上机状态。第二行放置取消签收状态、查询等操作按钮和状态图例，查询状态提示位于该行下方、结果列表上方，采用左对齐且左边缘与“查询”按钮左边缘对齐；窗口底部不再保留状态栏高度，ListView 向下占满剩余客户区。院区提供 `全部 / 老院 / 新院`，查询读取结果后按申请科室 `LS_AS_BARCODE.DEPT_NAME` 在 C++ 侧派生并过滤：包含“滨水”为新院，其余为老院。院区筛选后的结果再用于列表、排序和导出。

`专业组` 和 `上机状态` 使用原生 ComboBox 主题外观的下拉按钮，弹出层使用 `ListView + LVS_EX_CHECKBOXES` 实现多选。专业组仅加载 `LS_AS_ROOM.DELETE_BIT=0` 且 `Dept_Code IN (102,401)` 的记录；院区为全部时显示两院专业组，老院只显示 `Dept_Code=102`，新院只显示 `Dept_Code=401`。切换院区会立即刷新专业组，并清除新院区列表中不存在的已选专业组。第一项 `全部` 是批量开关：点击后勾选当前下拉中所有具体项，再次点击则取消所有具体项；没有勾选具体项或已勾选全部具体项时，查询都按“不限定该条件”处理。下拉弹窗会按当前显示器工作区自动向下或向上展开，避免靠近屏幕边缘时被截断；按钮支持鼠标点击、`F4` 和 `Alt+↓` 打开。需要查询未完成检验时，直接在主 `上机状态` 下拉中勾选 `已签收未上机 / 已上机未审核 / 已审核未发送`，该组合摘要显示为 `未完成检验`；同时可用主 `专业组` 下拉多选限定专业组。确认查询后，结果仍回填当前 ListView，并继续复用表头排序、右键复制、导出 CSV 和双击跳转常规报告。检验者、审核者仅作为列表显示列，不作为筛选条件。

ListView 单元格复制菜单统一使用公共预览规则：右键显示可点击的 `复制：实际内容` 菜单；空值显示为 `（空白）`，换行和制表符在菜单中转为空格，超过 48 个字符时仅缩短菜单预览，剪贴板仍写入完整原值。当前已用于已签收条码查询、门诊查询和免疫重复项目统计；不使用鼠标悬停自动弹出，避免干扰列表浏览和选择。

同一非空条形码对应多条医嘱时，结果列表将这些 `BarcodeQueryRow` 连续成组。每组第一行正常显示样本号、急诊、条形码、病人号、类型、姓名、性别和申请科室，后续行仅在 ListView 展示层将这 8 列留空，内存数据保持完整；空条形码不参与分组。CSV 继续导出每行完整字段。

结果列表不再保留首个空白占位列，从“样本号”开始显示业务列，“上机状态”位于“审核者”之后、“费用”之前。所有表头均支持本地升降序排序，只重排当前已加载的 `BarcodeQueryRow` 内存数据，不重新访问数据库；费用列按数值比较，其余列按文本比较。排序完成后按条形码重新归组：条码组顺序取该组在排序结果中首次出现的位置，组内医嘱顺序保留排序结果，因此每组第一行始终显示完整公共字段。排序前会记录当前选中行，重绘后再恢复选中并滚动到可见位置。页面不再提供独立排序下拉框；用户点过表头后，后续查询结果会继续按当前表头排序和归组展示。结果列表支持右键复制任意业务单元格，菜单文字为 `复制：实际内容`；菜单预览将换行和制表符转为空格，超过 48 个字符时显示省略号，但写入剪贴板的仍是完整原值。

结果列表双击行会复用常规报告的 `RegularReportOpenTarget + WM_REGULAR_OPEN_REPORT` 机制，携带同条码最近有效报告的 `REP_NO / OPER_NO / MACH_CODE / MACH_NAME / ROOM_CODE / CHK_DATE` 跳转到 `常规报告` 并定位目标报告；未匹配到有效报告、仪器或检验日期时不跳转并提示该条码为已签收未上机。

`导出Excel` 按钮在当前列表有数据后启用，导出为 UTF-8 BOM CSV 文件，默认文件名包含日期类型、查询日期范围和当前院区。导出内容使用当前内存列表顺序和所有可见业务列，“上机状态”位于“审核者”之后，不重新访问 LIS。

### 查询条件

| 界面输入 | 查询字段 / 规则 |
| --- | --- |
| 日期类型：申请日期 | `LS_AS_BARCODE.REQ_TIME` |
| 日期类型：签收日期 | `LS_AS_BARCODE.IN_DATE` |
| 日期类型：上机日期 | `LS_AS_BARCODE.IN_DATE` |
| 条形码 | `LS_AS_BARCODE.BARCODE LIKE` |
| 姓名 | `LS_AS_BARCODE.NAME LIKE` |
| 病人号 | `LS_AS_BARCODE.REG_NO LIKE` |
| 院区 | `全部 / 老院 / 新院`；按 `LS_AS_BARCODE.DEPT_NAME` 是否包含“滨水”在 C++ 侧派生并过滤 |
| 专业组 | 多选下拉来源 `LS_AS_ROOM` 的有效记录，仅加载 `Dept_Code IN (102,401)`；按院区显示两院、`102` 或 `401`，选择具体项时使用 `LS_AS_BARCODE.ROOM_CODE IN (...)` |
| 上机状态 | 多选下拉；选择具体项时由报告链路派生状态后按所选状态组合过滤，选择 `全部` 时不追加上机状态条件 |
| 未取消签收 | `LS_AS_BARCODE.CANCEL_DATE IS NULL` |
| 取消签收 | `LS_AS_BARCODE.CANCEL_DATE IS NOT NULL` |
| 已签收未上机 | 未匹配到有效 `LS_AS_REPORT.REP_NO`，`LS_AS_REPORT.CHK_FLAG<>'T'`，`LS_AS_REPORT.CONF<>'S'`，且 `LS_AS_BARCODE.OPER_STATE=0` |
| 已上机未审核 | 已匹配到有效 `LS_AS_REPORT.REP_NO` 或 `LS_AS_BARCODE.OPER_STATE>=1`，且 `LS_AS_REPORT.CHK_FLAG<>'T'`，`LS_AS_REPORT.CONF<>'S'` |
| 已审核未发送 | `LS_AS_REPORT.CHK_FLAG='T'` 且 `LS_AS_REPORT.CONF<>'S'` |
| 发送完成 | `LS_AS_REPORT.CONF='S'` |

上机状态不再直接裸用 `LS_AS_BARCODE.OPER_STATE`。查询会按条码号聚合 `LS_AS_REPORT`，优先以报告链路校正状态：`CONF='S'` 优先显示发送完成；否则 `CHK_FLAG='T'` 显示已审核未发送；否则已存在有效报告号或 `OPER_STATE>=1` 显示已上机未审核；最后 `OPER_STATE=0` 显示已签收未上机。这样可以规避条码表 `OPER_STATE` 更新滞后造成的状态不准。

`院区` 和 `专业组` 筛选控件依次位于第一行 `上机状态` 前。按钮区右侧状态图例使用横向小色块加文字展示 `已签收未上机 / 已上机未审核 / 已审核未发送 / 发送完成`，色块颜色与列表行背景色一致，便于和筛选结果直接对照。

### 列表字段

| 界面列 | 数据库字段 / 规则 |
| --- | --- |
| 样本号 | `LS_AS_REPORT.OPER_NO`，通过 `TXM_NO = BARCODE` 取最近一条非空值 |
| 急诊 | `LS_AS_BARCODE.JZ_FLAG` |
| 条形码 | `LS_AS_BARCODE.BARCODE` |
| 病人号 | `LS_AS_BARCODE.REG_NO` |
| 类型 | `LS_AS_BARCODE.TYPENAME` |
| 姓名 | `LS_AS_BARCODE.NAME` |
| 性别 | `LS_AS_BARCODE.SEX` |
| 申请科室 | 优先 `LS_AS_BARCODE.DEPT_NAME`；需要从代码补全时，根据 `TYPE / TYPENAME` 区分门诊/住院后，用 `DEPT_CODE` 对应 `JC_dept_mz_zy.mzksid / zyksid` 取得 `mzksmc / zyksmc` |
| 床号 | `LS_AS_BARCODE.BEDNO` |
| 签收人 | `LS_AS_BARCODE.OPER_CODE`，按签收人显示值/姓名处理，不等同于报告表人员代码 |
| 签收时间 | `LS_AS_BARCODE.IN_DATE` |
| 医嘱内容 | `LS_AS_BARCODE.ORDER_TEXT` |
| 标本 | `LS_AS_BARCODE.SAMP_NAME` |
| 检验者 | 最近有效报告 `LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，优先显示 `JC_EMPLOYEE_PROPERTY.NAME`，字典缺失时回退显示原值 |
| 审核者 | 最近有效报告 `LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，优先显示 `JC_EMPLOYEE_PROPERTY.NAME`，字典缺失时回退显示原值 |
| 费用 | `LS_AS_BARCODE.FY` |
| 申请医生 | `LS_AS_BARCODE.REQ_DRN` |
| 状态 | `LS_AS_BARCODE.ZT_FLAG` |
| 备注 | `LS_AS_BARCODE.NOTE` |
| 原因 | `LS_AS_BARCODE.REASON` |
| 送检 | `LS_AS_BARCODE.sjyq_qsr` |
| 送检时间 | 优先 `LS_AS_BARCODE.COLLECTION_TIME`，为空时回退 `SUB_DATE` |
| 申请时间 | `LS_AS_BARCODE.REQ_TIME` |
| 取消时间 | `LS_AS_BARCODE.CANCEL_DATE` |
| 取消人 | `LS_AS_BARCODE.CANCEL_OPER` |
| HZID | `LS_AS_BARCODE.HZID` |
| 上机状态 | 报告链路优先派生状态，显示 `已签收未上机 / 已上机未审核 / 已审核未发送 / 发送完成` |

列表不合并同一条形码的多条记录，保持 `LS_AS_BARCODE` 查询结果一行对应一行，避免因聚合造成现场查询变慢。

## 急诊样本统计

`统计分析管理 -> 急诊样本统计` 以急诊条码为主口径做只读统计，目标是让用户快速发现急诊条码是否已上机、是否审核、是否发送，以及签收后已经等待多久。

### 查询口径

第一版默认按签收时间查询，筛选控件精确到分钟，默认范围为当天 `00:00` 到 `23:59`：

| 界面输入 | 查询字段 / 规则 |
| --- | --- |
| 开始时间 | `LS_AS_BARCODE.IN_DATE >= 开始时间` |
| 结束时间 | `LS_AS_BARCODE.IN_DATE < 结束时间 + 1 分钟` |
| 院区 | SQL 不下推院区过滤；C++ 侧派生后过滤。`sign_dept=102` 为老院，`sign_dept=401` 为新院；`sign_dept` 为空/异常时，申请科室含 `滨水` 为新院，否则为老院 |
| 只看未完成 | 关联报告未发送，即 `LS_AS_REPORT.CONF <> 'S'` 或为空 |

主统计对象是唯一 `LS_AS_BARCODE.BARCODE`。同一条码对应多条医嘱时，先按条码聚合，再统计和展示：

| 条码级字段 | 聚合规则 |
| --- | --- |
| 急诊条码 | 任一有效行 `JZ_FLAG=1`，或关联报告存在 `assaypat_type='0'` |
| 签收时间 | 最早非空 `IN_DATE` |
| 申请时间 | 关联报告聚合后的 `LS_AS_REPORT.REP_DATE` |
| 当前状态 | 由报告链路优先校正后显示：取到 `LS_AS_REPORT.REP_NO` 至少视为已上机，`CHK_FLAG='T'` 至少视为审核完成并按 `CONF` 细分为审核完成未发送/已发送，`OPER_STATE=3` 且已审核时显示医生已查看 |
| 医嘱内容 / 标本 | SQL 返回条码行级数据；C++ 按 `BARCODE` 聚合，`ORDER_TEXT` 去重后用 `/` 拼接，标本取非空代表值 |

查询实现上，急诊统计主 SQL 返回 `LS_AS_BARCODE` 行级数据，不在 SQL 端按条码聚合，也不使用 `OUTER APPLY + FOR XML PATH` 拼接医嘱；C++ 侧按 `BARCODE` 聚合状态、时间和展示字段，并将同条码多条 `ORDER_TEXT` 去重后用 `/` 拼接。仪器名称不在主 SQL 中 `JOIN LS_AS_MACHINE`，而是先轻量预取 `MACH_CODE -> MACH_NAME` 字典到 C++ `std::map`，fetch 明细后内存映射，查不到时回退显示 `MACH_CODE`。院区筛选同样在 C++ 侧完成，避免主 SQL 追加 `sign_dept` 条件导致空值条码漏查，也避免把 `LIKE '%滨水%'` 放入主查询条件。

急诊统计的展示状态不再直接裸用 `LS_AS_BARCODE.OPER_STATE`，因为该字段可能异步更新。派生规则为：

| 当前状态 | 字段口径 |
| --- | --- |
| 未上机 | 未取到有效报告号，且聚合后 `OPER_STATE=0` |
| 已上机未审核 | 已取到 `LS_AS_REPORT.REP_NO`，或聚合后 `OPER_STATE>=1`，但 `CHK_FLAG<>'T'` |
| 审核完成未发送 | `LS_AS_REPORT.CHK_FLAG='T'` 且 `LS_AS_REPORT.CONF<>'S'`，且未满足医生已查看 |
| 审核完成已发送 | `LS_AS_REPORT.CHK_FLAG='T'` 且 `LS_AS_REPORT.CONF='S'`，且未满足医生已查看 |
| 医生已查看 | `LS_AS_REPORT.CHK_FLAG='T'` 且聚合后 `OPER_STATE=3` |

`签收-审核用时` 由客户端软件在 C++ 中计算：报告 `CHK_FLAG='T'` 时按 `LS_AS_REPORT.REP_TIME - LS_AS_BARCODE.IN_DATE` 显示固定分秒数；未审核条码按当前软件时间持续刷新 `当前时间 - LS_AS_BARCODE.IN_DATE`；未签收或时间缺失显示 `-`。

### 明细字段

明细列表展示院区、样本号、条码号、急诊来源、当前状态、签收-审核用时、签收人、病人号、类型、姓名、性别、年龄、申请科室、床号、医嘱内容、标本、报告号、仪器、申请时间、签收时间、上机时间、报告时间、审核、发送和末尾空白列。时间字段按现场口径对应：申请时间 `LS_AS_REPORT.REP_DATE`、签收时间 `LS_AS_BARCODE.IN_DATE`、上机时间 `LS_AS_REPORT.CREATE_TIME`、审核/报告时间 `LS_AS_REPORT.REP_TIME`，且放在 `仪器` 列之后。院区优先来自 `LS_AS_BARCODE.sign_dept`，`102` 映射为老院，`401` 映射为新院；当 `sign_dept` 缺失时按申请科室是否包含 `滨水` 兜底。审核列显示 `LS_AS_REPORT.CHK_FLAG`，发送列显示 `LS_AS_REPORT.CONF`，其中 `CONF='S'` 表示报告已发送。明细填充时会通过 `WM_SETREDRAW` 暂停 ListView 重绘，完成所有行列填充后再恢复绘制并统一刷新。明细双击会复用常规报告的 `RegularReportOpenTarget + WM_REGULAR_OPEN_REPORT` 机制，携带 `REP_NO`、`OPER_NO`、`MACH_CODE`、`MACH_NAME`、`ROOM_CODE` 和 `CHK_DATE` 跳转到 `常规报告` 并定位目标报告。

## 常规报告

自定义工具栏中的 `常规报告` 以三栏工作台形式复用检验结果查询的数据链路。页面打开时不自动查询；用户先选择左侧 `检验仪器`，再按左侧 `检验日期` 查询当天该仪器下的报告主记录。

`检验结果查询` 页面同样复用通用报告查询链路，但该页面报告列表不展示医嘱内容，因此会通过 `skip_order_text` 跳过 `LS_AS_BARCODE.ORDER_TEXT` 的 `FOR XML PATH` 聚合；`常规报告` 仍展示医嘱内容，继续保留聚合逻辑。主查询、检验结果查询、常规报告和已签收条码的批量 ListView 填充会在 C++ 端用 `WM_SETREDRAW` 暂停重绘，完成所有行列更新后统一刷新。

### 查询入口

| 界面输入 | 查询字段 / 规则 |
| --- | --- |
| 检验日期 | `LS_AS_REPORT.CHK_DATE >= 日期` 且 `< 日期 + 1 天` |
| 检验仪器 | 弹窗来源 `LS_AS_ROOM` / `LS_AS_MACHINE`，仅加载 `LS_AS_ROOM.Dept_Code IN (102,401)` 对应房间及其启用仪器，并按 `ROOM_CODE, MACH_CODE` 排序；列表显示仪器代码、仪器名称、项目代码、项目名称、样本和拼音码；项目代码来自同仪器 `LS_AS_GROUP.REP_STYLE='M'` 的首条 `GROUP_CODE`，多条时按 `orderby, GROUP_CODE` 取第一条；项目名称按 `LS_AS_GROUP.GROUP_CODE = LS_CODE_ITEM.ITEM_CODE` 显示 `ITEM_NAME`；样本按 `LS_AS_GROUP.SAMP_CODE = LS_AS_SAMPLE.SAMP_CODE` 显示 `SAMP_NAME`；科室和启用仪器字典在当前常规报告页面内缓存，数据库连接配置变化时自动重载；科室下拉提供 `全部`，无检索内容且用户未主动选择科室时默认展示全部仪器；输入英文或数字时跨科室本地匹配 `PY_CODE` 和 `MACH_CODE`，匹配结果默认选中第一行，检索框回车可直接确认，选中仪器后同步科室下拉框，确认后以 `LS_AS_REPORT.MACH_CODE` 过滤 |

系统设置页可配置常规报告底部 `1 / 2 / 3` 快捷检验仪器，保存到 `[RegularReport] QuickMachine*Code / QuickMachine*Name / QuickMachine*RoomCode`。中文仪器名会以 ASCII 安全编码写入 `ClientConfig.ini`，程序读取时自动还原，避免 Win32 profile API 按系统 ANSI 代码页保存后乱码。配置弹窗复用常规报告仪器弹窗的数据范围，只显示 `LS_AS_ROOM.Dept_Code IN (102,401)` 对应房间下的启用仪器，并同步展示主项目代码、项目名称和样本；如果常规报告页当前已有检验仪器，弹窗打开时会优先按当前 `ROOM_CODE` 选中科室，并按当前 `MACH_CODE` 选中仪器；弹窗失焦时只投递关闭消息，不在 `WM_ACTIVATE` 中同步销毁，避免点击主界面时影响主窗口重新激活。点击快捷按钮后只更新当前页的 `检验仪器` 条件，并按当前 `检验日期` 重新查询右侧报告列表。如果当前页面已经是该快捷仪器，则按保留状态刷新处理；切换到其他快捷仪器时仍按普通查询处理。底部快捷按钮按 `MACH_CODE + ROOM_CODE` 与当前页面检验仪器匹配，匹配项用 `[1] / [2] / [3]` 文本标记，不依赖仪器名称；打开页面、手动选择检验仪器、点击快捷按钮和系统设置保存后都会刷新该标记。

常规报告页面直接打开时会在窗口初始化完成后尝试静默应用快捷检验仪器 `1`：如果 `QuickMachine1Code` 已配置，则回填左侧 `检验仪器`，按当天 `检验日期` 查询右侧报告列表，并高亮底部 `[1]`；如果未配置则不提示、不查询。从 `检验结果查询` 双击报告行跳转到常规报告时，目标报告跳转消息会取消这次默认快捷仪器加载，避免默认查询覆盖目标报告定位。

### 右侧信息列表

右侧信息列表以 `LS_AS_REPORT` 为主表，默认按 `OPER_NO` 升序展示。当前优先保存并使用 `LS_AS_REPORT.ID` 作为行唯一标识；界面选中行后再用该行 `REP_NO` 查询项目明细。

| 界面列 | 数据库字段 / 规则 |
| --- | --- |
| 标签 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后取 `JZ_FLAG`；`1` 显示 `急`，`0` 不显示 |
| 样本号 | `LS_AS_REPORT.OPER_NO` |
| 姓名 | `LS_AS_REPORT.NAME` |
| 性别 | `LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE`，显示 `SEX_NAME` |
| 年龄 | `LS_AS_REPORT.AGE` |
| 医嘱内容 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后聚合 `ORDER_TEXT`，多行用 `/` 分隔 |
| 科室代码 | `LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE` 后优先取 `DEPT_NAME`；需要从代码补全时，根据 `LS_AS_BARCODE.TYPE / TYPENAME` 区分门诊/住院后，用 `DEPT_CODE` 对应 `JC_dept_mz_zy.mzksid / zyksid` 取得 `mzksmc / zyksmc` |
| 床号 | `LS_AS_REPORT.BED_CODE` |
| 打印 | `LS_AS_REPORT.ZYMZ_PRINT` |
| 病人类型 | `LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE`，显示 `TYPE_NAME` |
| 检验者 | `LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID` |
| 项目名称 | `LS_AS_REPORT.GROUP_NO` |
| 验单号 | `LS_AS_REPORT.REP_NO` |
| 审核 / 确认 | `CHK_FLAG` / `CONF` |
| 条形码 | `LS_AS_REPORT.TXM_NO` |
| 检验仪器 | 当前显示 `GROUP_NO`，后续可按实际字段再调整 |
| 标本 | `LS_AS_REPORT.SAMP_CODE = LS_AS_SAMPLE.SAMP_CODE`，显示 `SAMP_NAME` |
| 备注 | `LS_AS_REPORT.NOTE` |
| 开单日期 | `LS_AS_REPORT.REP_DATE` |
| 签收时间 | `LS_AS_BARCODE.IN_DATE` |
| 检验日期 | `LS_AS_REPORT.CHK_DATE` |
| 报告时间 | `LS_AS_REPORT.REP_TIME` |
| 费用 | `LS_AS_REPORT.FY` |
| 医生代号 | `LS_AS_REPORT.REQ_DR = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`，查不到姓名时显示为空 |
| 临床诊断 | `LS_AS_REPORT.DIAG_NAME` |
| 病人号 | `LS_AS_REPORT.REG_NO` |
| 上机时间 | `LS_AS_REPORT.CREATE_TIME` |
| 电话 | `LS_AS_REPORT.PAT_PHONE` |

行背景色规则：报告级危急状态按 `LS_AS_REPORT.assaypat_type = 9` 判断，未审核且未发送显示粉色，已审核且已发送显示黄色；非危急报告中 `CONF='S'` 优先显示深绿色，否则 `CHK_FLAG='T'` 显示蓝色，未审核保持默认背景色。若 `assaypat_type = 0` 或 `JZ_FLAG = 1`，该行文字显示为红色。

中间结果列表和右侧信息列表均使用 `NM_CUSTOMDRAW` 处理业务行色。选中行优先使用系统高亮色，并清理 `CDIS_SELECTED/CDIS_FOCUS` 后交回默认绘制，避免 ListView 失去焦点时被系统非活动选中态覆盖成浅色；收到 `NM_KILLFOCUS` 时会重绘当前选中行。

中间结果列表的 `结果` 列保持白色背景；其他列默认使用浅灰背景，用于弱化辅助字段并突出结果值。选中行高亮优先于列背景色。

中间结果列表的 `结果` 单元格支持界面内临时编辑。实现方式是单击 `结果` 子项时，在该单元格位置创建覆盖 `EDIT` 控件；回车时只更新 `st->resultRows[row].result` 和当前 ListView 文本，并自动跳到下一行 `结果` 单元格继续编辑；失焦或 Esc 取消。失焦取消时不强制把焦点设回 ListView，避免点击其他按钮时第一次点击被焦点切换消耗。该功能不调用任何数据库更新语句，切换报告、重新查询或关闭窗口时会取消未完成编辑。

右侧顶部摘要只统计当前已加载到列表中的内存数据，不额外访问数据库：第一行 `样本数` 统计 `REP_NO` 非空行，`上机数` 统计 `NAME` 非空行，`审核数` 统计 `CHK_FLAG='T'` 行，`发送数` 统计 `CONF='S'` 行；第二行 `危急报告数` 统计 `assaypat_type=9` 行，`急诊报告数` 统计 `assaypat_type=0` 或 `JZ_FLAG=1` 行，对应 `已审` 统计均要求 `CHK_FLAG='T'` 且 `CONF='S'`。

点击右侧信息列表表头时，只对内存中的 `st->reportRows` 做 `std::stable_sort` 并重绘列表，不重新访问数据库。排序前会记录当前选中行的 `LS_AS_REPORT.ID` 和已勾选行的 ID，排序后再恢复选中和勾选状态。

右侧信息列表上方的 `⇧` / `⇩` 按钮只在当前内存列表内跳转，分别选中第一行和最后一行，不访问数据库。页面底部 `上一个 / 下一个` 也只在当前内存列表内移动当前选中行，切换后复用右侧列表选中联动，刷新左侧信息和中间项目明细。右侧信息列表下方的 `今天 / 前一天 / 后一天` 会先更新左侧 `检验日期`，再按当前 `检验仪器` 重新发起报告主列表查询；其后的 `自动刷新` 默认不启用，勾选后按秒数输入启动窗口定时器，默认 10 秒，输入值按 5-3600 秒夹取，且若上一轮报告主列表查询尚未完成则跳过本次定时触发，避免堆积数据库查询。页面底部 `刷新(F5)` 会读取当前左侧 `检验仪器` 和 `检验日期`，重新发起报告主列表查询并刷新右侧列表；查询完成后若没有可恢复或可自动选中的报告行，则清空左侧详情、中间结果和图像状态，避免保留上一条报告内容。

`刷新(F5)` 和 `自动刷新` 使用保留状态刷新：查询发起时不清空右侧列表、中间明细和左侧信息；查询完成后以 `LS_AS_REPORT.ID` 恢复选中行、勾选行和滚动位置。若刷新前后行 ID 顺序一致，则只比较并更新变化的单元格；若行集合或顺序发生变化，则重建右侧列表后再恢复状态。原选中行仍存在时会重新查询该行中间明细，用数据库查询结果覆盖界面内临时编辑值；原选中行消失时清空左侧信息和中间明细。

日期控件会记录当前列表对应的查询日期。右侧已有选中行时，如果 `检验日期` 控件重复触发同一天查询，或点击 `今天` 按钮，也按保留状态刷新处理；`前一天 / 后一天` 仍按明确换日期的普通查询处理。

左侧 `样本号` 输入框按回车时，只在当前已加载的 `st->reportRows` 内按 `OPER_NO` 定位；匹配到后选中右侧对应行，并复用现有选中行联动逻辑刷新左侧信息和中间项目明细。

右侧信息列表第一列启用勾选框。右键某一行会弹出报告操作菜单：

- `打印条码`：打印当前右键行。
- `打印勾选条码`：按当前列表顺序打印所有勾选行；如果中途失败，会停止后续打印并提示已发送数量和失败记录。

打印会把对应行字段填入外部 `LabelPrint` 项目的 `MedicalLabelData`，再通过共享条码打印 helper 发送 RAW 打印任务。打印机名读取 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`，默认值为 `Xprinter XP-360B #2`，并以宽字符形式传给打印链路，避免中文打印机名经过 ANSI 转换后失效。非 Zebra 打印机继续交给 LabelPrint 读取 Windows 打印机元数据并自动选择 XP-360B TSPL、Godex EZPL 等路径；无法识别时按 XP-360B 兼容路径兜底。检测到 Zebra/ZD888t 时，本项目复用 LabelPrint Zebra 测试打印的默认布局，并读取 `[RegularReport] ZebraChineseFont` 选择 `E:SIMSUN.TTF` 或 `E:CSONG.TTF` 输出中文，默认 `E:SIMSUN.TTF`；仍清空 fallback，避免双字体叠印导致文字显示不全；Zebra 路径下 `组合项目` 以条码水平区域为基准居中。打印数据中的样本号、条码号、姓名、标本、开单日期、科室代码、病人号来自右侧报告行；条码上的组合项目取自右侧报告行的 `检验仪器` 列内容，不再为了打印条码额外查询中间项目明细；开单日期按 `yyyy/M/d` 格式输出。

如果保存的打印机名因为 Windows 重命名、换电脑或驱动重装而失效，右键打印会提示失败原因和当前打印机名。用户需要到 `系统设置` 页重新选择常规报告条码打印机并保存。

构建时主项目优先通过 `find_package(LabelPrint 1.2 CONFIG QUIET)` 查找 `LabelPrint::labelprint`。正式打包建议使用 `scripts/build_main.ps1 -LabelPrintPackagePath` 指向 LabelPrint release zip 解压目录，使构建可复现且不依赖本机相邻源码目录。如果未找到已安装或已解压的 LabelPrint CMake package，或版本低于统一打印入口所需版本，则回退到 CMake 变量 `LIS_LABELPRINT_DIR` 指定的源码目录，默认路径为 `../../020 LabelPrint/LabelPrint`，并以 `add_subdirectory` 接入。两种方式都找不到时，常规报告仍可使用查询功能，但右键 `打印条码` 会提示打印功能未启用。

条码模板当前使用以下字段：

- 样本号
- 组合项目
- 条码号
- 姓名
- 标本
- 开单日期
- 科室代码
- 病人号

### 左侧默认值与回填优先级

常规报告左侧区域按业务形态预留为后续“编号样本 / 写入报告主记录”的准备区，`组合项目`、`标本`、`检验单号`、`样本号` 等字段在真实编号流程中会成为写入 `LS_AS_REPORT` 等表的数据来源之一。当前项目仍保持只读查询和界面展示，不执行编号写库，也不向 LIS 业务表插入或更新记录。

在不写库的当前阶段，左侧字段来源按以下规则记录：

- 用户选择左侧 `检验仪器` 后，`组合项目` 和 `标本` 的默认值优先来自仪器选择弹窗当前选中仪器的主项目和样本信息：主项目取 `LS_AS_GROUP.REP_STYLE='M'` 的首条 `GROUP_CODE` 及其 `LS_CODE_ITEM.ITEM_NAME`，样本取同一主项目的 `SAMP_CODE` 及其 `LS_AS_SAMPLE.SAMP_NAME`。
- 用户选中右侧已有报告行后，左侧 `标本信息 / 病人信息 / 验单信息` 仍以右侧数据库记录回填为准，用于查看既有报告。此时 `组合项目`、`标本` 等展示值来自当前报告行及其关联字典。
- 如果没有选中右侧报告行，也没有完成编号写库，左侧来自仪器选择器的 `组合项目` 和 `标本` 只作为界面默认展示和后续编号流程的候选值，不代表数据库中已存在报告记录。
- 后续如果接入编号写库，需要在代码状态中区分“仪器选择器默认值”和“右侧报告行回填值”，避免查看已有报告时误把默认值写回数据库。

### 左侧和中间联动

右侧选中某一行后：

- 左侧 `标本信息 / 病人信息 / 验单信息` 从当前选中行回填。
- 左侧年龄显示会解析 `LS_AS_REPORT` 查询结果中的年龄文本，识别末尾 `岁 / 月 / 天 / 小时 / 分`，输入框只显示数字部分，单位下拉框选中对应单位；无法识别单位时保留原文本并默认选中 `岁`。
- `检验者 / 审核 / 申请日期 / 签收时间 / 上机时间 / 报告时间` 是只读展示控件，由程序写入，用户不能手动修改。
- `检验日期` 仍是查询条件；程序回填选中行检验日期时会屏蔽自动查询，避免选中行触发重复查询。
- 左侧可输入控件维护独立 `leftTabControls` 顺序，不依赖 Win32 默认创建顺序；Tab 按页面视觉从上到下跳转，Shift+Tab 反向跳转。
- 左侧区域宽度使用 21% 比例布局，但当前限制为 360 逻辑像素；内容区始终预留垂直滚动条宽度，避免有无滚动条时自绘分组和控件宽度跳变。
- 中间检验结果列表通过当前行 `REP_NO` 查询 `LS_AS_REPENTRY`，并复用 `LS_AS_ITEM` 字典、参考区间和 `NORMAL` 偏差显示规则。
- 中间列表的 `组合项目` 列不再直接使用右侧报告行项目名，而是按当前明细行 `LS_AS_REPENTRY.GROUP_CODE` 关联 `LS_AS_LABMATCH.GROUP_CODE`，再取 `LS_AS_LABMATCH.GROUP_NAME`。取名时优先使用 `DELETE_BIT=0 且 USE_FLAG=0` 的非空名称；若缺失，则取同一 `GROUP_CODE` 下任意非空名称作为兜底。
- 中间列表查询排序加入 `GROUP_CODE / ITEM_CODE / ID`，让相同组合项目尽量连续展示；界面展示时连续相同的组合项目名只显示第一行，其余行显示为空。
- 中间 `图象` 页签打开时，才通过当前行 `REP_NO` 查询 `LS_AS_ITEMPICTURE.PICTURE`。查询只取 `DELETE_BIT=0` 且图片非空的第一张记录，按 `PIC_NO, ID` 排序；若无图片则保持空白，有图片时用 GDI+ 在左上角固定大图层内按比例绘制，外层视口提供横向/纵向滚动条，不随拖条宽度变化而缩放。
- 页面底部 `图形(T)` 会按当前选中报告行的 `REP_NO` 打开独立结果图窗口，查询来源仍复用 `LS_AS_ITEMPICTURE.PICTURE`。若中间 `图象` 页签已经加载了同一报告图像，则直接克隆已加载图像；否则弹窗先显示加载状态，再后台查询并在窗口客户区绘制图片，不再额外套滚动容器。弹窗打开后，右侧信息列表选择变化会同步更新标题和图片。独立窗口使用项目图标，采用离屏位图双缓冲绘制，并在缩放时跳过背景擦除，以减少 GDI+ 图片重绘残影；用户调整后的窗口尺寸保存到 `[RegularReport] PicturePopupWidth / PicturePopupHeight`，下次打开沿用。

## 当前未实现的截图功能

- 历史库 / 当前库切换。
- 取消发送。
- 导出、预览、打印。
- 科室、医生、病人类型、报告状态下拉字典。
- 行颜色规则。
- 勾选多条记录后的批量操作。
- 报告状态中文转换，例如 `已审核`、`已发送`。

## 后续建议

优先补齐：

1. 将 `CONF`、`CHK_FLAG`、`TYPE` 转换成现场软件一致的中文状态。
2. 按截图增加行颜色规则和选中行效果。
3. 导出当前报告列表为 CSV 或 Excel。
4. 若要做到完全复刻，再继续反查原系统里项目名称、医生、科室等字典表。

## 大量输血统计

`统计分析管理 -> 大量输血统计` 提供“实际输血量”和“申请量对照”两种只读口径，正式默认“实际输血量”和“配血时间”。实际输血量以 `LS_XK_BloodCrossMatch.VerifyState='已审核'` 作为事实，并按唯一 `BloodInID` 计量；用户可主动切换申请量对照。

申请量对照分支从页面开始日期 `00:00:00` 起读取，不向前回溯；结束条件读取到页面结束日期次日 `00:00:00 + 24小时`，用于补齐最后一天起始事件的完整窗口。只有事件第一张有效申请时间位于页面日期范围内的事件可进入结果，结束日期以后读取的申请只能补充已有事件，不能新建结果事件。

主查询保留未审核、已审核、已完结和已驳回申请，排除 `Delete_Bit=1` 或状态为已删除的记录。C++ 按去空格后的 `Patient_NO` 分组，以页面范围内第一张有效申请为事件起点 `T0`，窗口使用左闭右开区间：

```text
T0 <= Apply_Time < T0 + 24小时
```

每张有效申请最多进入一个事件；当前窗口结束后，下一张有效申请建立新的独立事件。未审核、已审核和已完结申请参与事件和申请量统计；已驳回申请不建立事件、不贡献申请量，落入已有事件窗口时随事件成分展示，否则在独立已驳回核查视图展示；已删除申请不展示。

申请量只读取子表 `ApplyComposition / ApplyNum / ApplyUnit`，按唯一子表 `ID` 去重。子表没有申请成分时不读取主表同名冗余字段回退，而是将事件标记为总量不完整。规则版本 `v2` 按申请成分名称和单位折算：

| 申请成分识别 | 原单位 | 毫升折算 |
| --- | --- | ---: |
| 任意制品 | `ML` | `ApplyNum × 1` |
| 名称包含“冷沉淀” | `U` | `ApplyNum × 20` |
| 其他制品 | `U` | `ApplyNum × 200` |
| 任意制品 | `治疗量` | `ApplyNum × 250` |

页面提供默认不勾选的“包括血小板和冷沉淀”。不勾选时，`ApplyComposition` 包含“血小板”或“冷沉淀”的行保留在成分明细和 CSV 中，但不计入事件总量、计量制品项，也不因该行本身将事件标记为总量不完整；勾选后正常参与统计。CSV 同时记录本次开关状态和规则版本。

查询参数携带阈值毫升数和比较方式，默认条件为 `>=1600ml`；阈值必须大于 `0` 且页面限制最多两位小数，比较方式可选 `>=` 或 `>`。已知折算量满足本次条件时事件命中大量输血；即使另有异常成分，只要已知量已经满足条件，事件仍命中并标记总量不完整。已知量未满足条件且存在无法折算或子表缺失时，事件进入折算异常结果，不能直接判定未命中。查询状态及两级 CSV 均记录实际统计条件。

院区不下推 SQL，按事件首张有效申请的 `Apply_Dept` 在 C++ 内存派生：包含“滨水”为新院，其他为老院。事件列表、成分列表、汇总和 CSV 均基于院区过滤后的内存结果。事件和逐成分 CSV 不重新查询 LIS，双击事件或成分通过 `WM_BLOOD_OPEN_REQUEST` 定位对应输血申请。

实际输血量查询链路为：

```text
LS_XK_BloodCrossMatch cm
    LEFT JOIN LS_XK_BloodRequestApply a ON a.ApplyFormNO=cm.ApplyFormNO
    LEFT JOIN LS_XK_BloodInfo bi ON bi.ID=cm.BloodInID
    LEFT JOIN LS_XK_B_CompositionInfo comp ON comp.ID=bi.CompositionID
    LEFT JOIN (
        SELECT BloodInID, MIN(BloodOut_Date), COUNT_BIG(*)
        FROM LS_XK_BloodOutInfo
        GROUP BY BloodInID
    ) bo ON bo.BloodInID=cm.BloodInID
```

出库表只做一次按 `BloodInID` 的预聚合，避免对每条交叉配血记录执行相关子查询；申请表采用 `a.ApplyFormNO=cm.ApplyFormNO` 直接等值连接，避免在连接列两侧执行 `LTRIM/RTRIM` 而妨碍索引使用。现场只读核查已确认申请单号直接关联无缺失，去空格只用于返回后的显示和 C++ 分组。

实际口径可选 `Match_Date / BloodOut_Date / Apply_Time / Check_Date` 作为事件时间，默认 `Match_Date`。同一患者按所选时间升序，以第一袋为起点建立左闭右开的24小时窗口；所选时间为空时不回退，进入时间缺失核查。`BloodOutInfo` 多行时只取最早出库时间并标记核查，不能放大血袋数。事件的“实际输血制品构成”只按 `CompositionInfo.Blood_Composition + Norm + Unit` 折算后的实际血袋毫升数汇总，不读取申请子表成分；申请量对照分支才按申请成分生成“申请制品构成”。查询状态同时显示 SQL 返回原始记录数和总耗时。

`VerifyState='未审核'`、未知状态、逻辑删除的交叉配血记录不进入正式统计而进入核查；申请单已删除、已驳回、缺失或病人号不一致不覆盖已审核的实际输血事实，血袋仍计量并标记申请单异常。实际容量读取 `BloodInfo.CompositionID -> CompositionInfo.Norm + Unit`，折算仍使用 `ML×1 / 普通U×200 / 冷沉淀U×20 / 治疗量×250`。事件、血袋明细和 CSV 均记录统计口径、事件时间口径、阈值、开关和异常状态。
