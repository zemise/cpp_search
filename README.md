# lis-workbench

`lis-workbench`（LIS 工作台）是面向 LIS 检验结果、输血申请和相关检验摘要查询的 Windows 工作台。

当前版本：`v2026.07.04`

项目已经整理为可长期演进的结构。
详见 [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) 和 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

命名说明：

- 对外项目名和仓库名使用 `lis-workbench`。
- 用户可见程序名使用 `LIS 工作台`。
- 配置文件使用 `ClientConfig.ini`；升级时如果只存在旧 `result_search.ini`，程序会自动复制迁移。
- 主程序输出文件名使用 `lis_workbench.exe`；自动更新替换由独立 `Updater.exe` 执行。
- 为兼容既有部署，独立检验查询工具 `result_search.exe` 和 `search_*` 内部模块名暂时保留。

### 架构概要

```
┌─────────────────────┐
│  main.cpp (Win32)   │  ← 入口层，Qt 迁移后由 src_qt/main.cpp 替代
│  search_ui_*        │
├─────────────────────┤
│  search_input_*     │  ← 边界层：控件读写、事件分发、列表呈现
│  search_settings_*  │
│  trend_window.*     │
├─────────────────────┤
│  search_controller  │  ← 控制层：桥接查询请求与数据层
├─────────────────────┤
│  search_core        │  ← 核心层：ODBC 查询、数据映射
│  search_app         │      Qt 迁移后原样复用
│  app_settings       │      (ODBC → Qt SQL, INI → QSettings)
│  search_text        │
│  search_view_state  │
│  trend_core         │
└─────────────────────┘
```

- `build/`、`out/`、`ClientConfig.ini` 视为本地产物，不纳入版本管理

当前目标：

- 通过 SQL Server ODBC 连接 LIS 数据库。
- 从 `LS_AS_REPORT` 查询报告主记录。
- 通过 `REP_NO` 联查 `LS_AS_REPENTRY` 中的项目结果。
- 支持按姓名、诊疗卡号、病人号、条码号、样本号、仪器、组合项目、日期范围等条件过滤。
- `结果查询` 第一张报告列表支持点击任意列名进行本地升降序排序，只重排当前已加载内存数据，不重新查询报告列表数据库；排序后保留当前选中报告并刷新右侧项目明细。
- `检验结果查询` 第一张报告列表支持双击报告行跳转到 `常规报告` 页面；程序会携带报告号、检验日期、检验仪器和科室代码，常规报告页查询后按 `REP_NO` 精确选中对应报告并加载左侧详情和中间项目明细。
- 在主程序中接入输血结果查询模块，从 `LS_XK_BloodRequestApply` 查询输血申请，并通过 `LS_XK_BloodRequestApplySon.ApplyFormNO` 聚合申请成分。

## 当前实现范围

已实现：

- 原生 Win32 查询界面。
- 左侧查询条件区。
- `检验科室 / 病人类型 / 报告状态` 下拉筛选。
- `检验科室` 下拉来源于 `LS_AS_ROOM.ROOM_NAME`，查询时回写对应 `ROOM_CODE` 过滤。
- `病人类型` 下拉来源于 `LS_AS_PATTYPE`，显示格式为 `TYPE-TYPE_NAME`，查询时回写对应 `TYPE` 过滤。
- `检验仪器` 选择来源于 `LS_AS_ROOM / LS_AS_MACHINE`，仅显示 `LS_AS_ROOM.Dept_Code IN (102,401)` 对应房间下 `RUL='启用'` 且 `DELETE_BIT=0` 的仪器；弹窗加载 `ROOM_CODE / MACH_CODE / MACH_NAME / PY_CODE`，并从 `LS_AS_GROUP.REP_STYLE='M'` 的首条主项目补充 `GROUP_CODE`、`LS_CODE_ITEM.ITEM_NAME` 和 `LS_AS_SAMPLE.SAMP_NAME`，按 `ROOM_CODE, MACH_CODE` 排序后在当前常规报告页面内缓存，数据库连接配置变化时自动重载；科室下拉提供 `全部`，无检索内容且用户未主动选择科室时默认展示全部仪器；输入英文或数字时跨科室本地匹配 `PY_CODE` 和 `MACH_CODE`，匹配结果自动选中第一行，选中仪器后自动同步科室下拉框，检索框回车可直接确认并关闭弹窗，查询时回写对应 `MACH_CODE` 过滤。
- 主列表补充 `打印`、`自助机` 两列，分别对应 `ZYMZ_PRINT`、`ZZJ_PRINT`。
- 主列表补充 `检验者`、`审核者` 两列：
  - `检验者`：`LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`
  - `审核者`：`LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID`
- 主列表支持按审核/打印状态着色：
  - 已打印整行白底
  - 已审核整行浅青底
  - 未审核整行橙底
- 中间报告列表。
- 右侧项目明细列表。
- 点击报告行后自动加载对应项目结果。
- 右侧项目明细支持按 `NORMAL` 着色。
- 底部 `趋势图` 按钮可按上一次成功查询条件打开趋势窗口：
  - 上一次查询必须使用病人姓名或病人号，任意一个即可。
  - 如果查询成功后又清空输入框，趋势图仍使用上一次成功查询条件。
  - 按上一次患者/仪器/日期/项目条件查询历史项目点。
  - 趋势数据采用后台加载，窗口会先打开并显示加载状态，避免长日期范围查询时卡住主界面。
  - 右侧平铺显示项目列表，点击项目行切换图表。
  - 项目列表每行带勾选框。
  - 中间显示折线图，横轴按有效结果点顺序等距排列，日期和时间分两行显示。
  - 下方显示趋势明细表，便于和主界面核对。
  - 右侧底部 `导出勾选项目` 可将勾选项目的趋势明细导出为 CSV。
  - 右侧底部 `导出勾选图片` 可选择一个文件夹，并将勾选项目分别导出为多张 PNG。
  - 导出默认文件名为 `病人姓名-病人号-查询日期.csv`，病人姓名或病人号为空时自动跳过对应部分。
- 数据库设置页面，支持服务器、初始数据库、用户名、密码配置。
- 设置页面采用原生 Win32 分组卡片布局，支持字号配置；字号选择限制为 `9 / 11 / 12 / 13` 四档，保存后持久化到 `ClientConfig.ini`，并立即应用到菜单栏及子菜单、主界面、输血模块和 LIS 检验信息弹窗；底部状态栏保持系统默认字体。
- 系统设置支持配置 LIS 摘要项目代码和输血摘要仪器过滤，`ABO 代码`、`RhD 代码`、`Hb 代码`、`PLT 代码`、`不规则代码`、`直抗代码` 均以分号分隔保存到 `ClientConfig.ini` 的 `[LisSummary]`，默认不规则抗体筛查代码为 `11106;91966`，直接抗人球蛋白试验代码为 `11105;91965`；`血型仪器`、`血常规仪器` 使用 `ROOM:MACH1,MACH2;ROOM:MACH` 成对格式，默认分别为 `11:11101;64:626` 和 `1:1002,1011,1012;61:613,615`，清空则不限制仪器；`输血排除仪器` 保存为 `BloodLisExcludeMachines`，支持 `ROOM:;ROOM:MACH1,MACH2`，默认 `3:;71:;8:8004`，清空则不排除。
- 系统设置支持选择常规报告条码打印机，保存到 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`；条码打印机区域的 `Zebra 字体` 下拉框仅在识别到 Zebra/ZD888t 时生效，保存到 `[RegularReport] ZebraChineseFont`，可在 `E:SIMSUN.TTF` 与 `E:CSONG.TTF` 间选择。
- 系统设置支持配置常规报告底部 `1 / 2 / 3` 快捷检验仪器，选择器复用常规报告仪器弹窗的数据范围，仅显示 `LS_AS_ROOM.Dept_Code IN (102,401)` 对应房间下的启用仪器，并展示主项目代码、项目名称和样本，保存到 `ClientConfig.ini` 的 `[RegularReport] QuickMachine*`。
- 直接打开 `常规报告` 页面时，如果已配置快捷检验仪器 `1`，页面会自动应用该仪器并按当天检验日期加载右侧报告列表；从 `检验结果查询` 双击跳转目标报告时会跳过该默认加载。
- 系统设置支持配置自动更新源，默认选择共享文件夹，并根据更新源只显示共享目录或 HTTP 地址其中一项；配置保存到 `ClientConfig.ini` 的 `[Update]`，菜单栏 `系统 -> 检查更新` 会在后台按共享文件夹或 HTTP manifest 拉取更新包并完成 size / SHA-256 校验，发现新版本后可确认安装并重启程序。
- 自动更新 HTTP 地址默认使用 GitHub latest manifest：`https://github.com/zemise/lis-workbench/releases/latest/download/manifest.json`，后续发布新版本不需要修改客户端配置。
- 系统设置支持启用自动检查更新；开启后主程序启动会延迟检查 manifest，每天最多一次，只提示新版本，用户确认后再下载并安装。设置页中的复选框保持纯原生 `BUTTON + BS_AUTOCHECKBOX`，通过白色宿主窗口统一背景，不使用 owner-draw。
- `Updater.exe` 每次运行都会自动写入安装目录 `log\updater.log`，便于现场更新失败后带回分析。
- 数据库配置持久化保存到程序同目录 `ClientConfig.ini`；中文打印机名、快捷检验仪器名等模块配置会以 ASCII 安全编码保存，程序读取时自动还原，避免受系统 ANSI 代码页影响后乱码。
- `设置`、`查询` 和 `退出` 按钮。
- 主程序中的 `检验结果查询`、`输血结果查询`、`系统设置` 均为单实例 MDI 窗口，重复点击菜单会激活已打开窗口。
- 主程序自定义工具栏提供 `标本签收中心`、`常规报告`、`输血查询`、`结果查询`、`质控分析`、`门诊查询`、`常用电话` 快捷入口，分别复用对应菜单命令；活动 MDI 页面会高亮对应入口，右侧 `关闭当前` 为文字型次要操作按钮，用于关闭当前 MDI 子窗口。
- 工具菜单中的 `已签收条码查询` 已接入 `LS_AS_BARCODE` 只读查询：
  - 默认不自动查询，日期默认当天申请日期，支持切换签收日期、上机日期。
  - 支持按条形码、姓名、病人号、上机状态、专业组、取消签收状态筛选；取消签收状态按 `CANCEL_DATE` 是否为空判断，上机状态直接对应 `LS_AS_BARCODE.OPER_STATE`，列表中 `0/1/2` 简化显示为 `未上机 / 已上机 / 审核完成`，下拉中的 `已审核未发送` 暂时不关联实际状态。
  - 下方列表展示样本号、急诊、条形码、病人号、类型、姓名、性别、申请科室、床号、签收人、签收时间、医嘱内容、标本、费用、申请医生、状态、备注、原因、送检、送检时间、申请时间、取消时间、取消人、HZID、上机状态。
  - 当前仅实现查询和刷新；取消签收、取消医嘱签收、取消原因限制、导出 Excel 保持禁用，不执行数据库修改。
- 工具菜单新增 `门诊查询`，位于 `常用电话` 前；页面顶部采用两行筛选布局，并按当前系统字体测量标签宽度，按收费时间默认当天起止、院区、门诊号、姓名和身份证筛选门诊收费明细，院区后提供默认不勾选的 `包含非检验科`，勾选后不再按院区限定 `ZXKS`；门诊号、姓名和身份证输入框均支持回车直接查询；门诊号筛选直接对应 `YJ_MZSQ.BLH`，按末尾匹配查询，便于输入短尾号定位完整门诊号；输入门诊号时若前 8 位可推断 `YYYYMMDD`，则按该日期查，否则只查第二个收费时间控件所在日期；院区 `全部` 仅汇总 `ZXKS=102/401` 的老院和新院数据；主表读取 `YJ_MZSQ.BLH / FPH / SQNR / SQKS / DJ / SL / DW / JE / SFRQ / TXM / BBMC / TXMDYSJ / ZXKS`，并通过 `YJ_MZSQ.BRXXID = YY_BRXX.BRXXID` 读取 `SFZH / BRXM / XB / CSRQ`；身份证筛选对应卡号列字段 `YY_BRXX.SFZH`；列表在项目名称后显示 `申请科室`，对应 `YJ_MZSQ.SQKS` 并按 `JC_DEPT_PROPERTY.DEPT_ID -> NAME` 转换为科室名称，匹配不到时回退显示原代码，支持右键复制当前单元格；有条码号的记录按 `TXM` 聚合同条码多行 `SQNR`，项目名称用 `/` 去重拼接，无条码号的记录仍逐行显示并用黄色行背景提示；院区按 `ZXKS` 的 `102 / 401` 映射老院/新院，`FPH` 为空时显示 `0`，卡号显示 `SFZH`，`XB` 按 `1/2` 显示男女，`CSRQ` 转换为年龄，`TXM` 为空时条码号显示 `未生成`，只做只读查询。
- 工具菜单新增 `常用电话`，采用 RTF + RichEdit 内嵌只读查看器加载程序目录下 `documents` 目录中的 RTF 文件；页面按 MDI 最大化打开，RichEdit 查看区放在浅边框白色文档卡片内并从顶部占满窗口主体，右上角独立白色控制卡片显示两行操作：上行为搜索框，下行为文档下拉、上传和刷新等低频操作；正常宽度下文档卡片会让出右上角控制卡片宽度，避免两者重合；可上传 RTF、下拉选择多个常用电话文件，并将当前选择持久化到 `ClientConfig.ini` 的 `[PhoneDirectory] SelectedFile`，下次打开保持一致；控件字体跟随系统设置字号，搜索框作为主输入焦点，输入变化时立即自动搜索并保留当前输入内容，`Enter` / `F3` 可继续查找下一个；内容按页面宽度居中显示，搜索跳转按命中行号定位并立即完整刷新，避免 RichEdit 自动滚动到远离命中的位置；滚动条拖动中节流刷新、滚动结束立即完整刷新，以兼顾 Windows 7 实机残影修复和滑块拖动流畅度；支持刷新重载。
- 工具菜单中的 `标本签收中心` 已替换原 `工具3` 占位页，当前按 `temp/模版.png` 完成界面骨架，并接入第一版只读条码查询；有条码时按单个条码精确读取数据库并回填页面，左侧条码为空时可按签收日期和/或申请日期查询已签收条码列表，不执行签收、拒签、导出等数据库写入业务操作。下方列表补充签收时间、送检时间、年龄、签收人、标本类型和检验室；`此条码已存在...` 使用独立红色提示标签，普通查询状态显示在左下按钮区，且未上机条码会追加 `未上机检验!`。`补打条码` 复用常规报告页的 LabelPrint 打印入口，样本号固定为 `补`，组合项目固定取当前选中行的 `检验室` 内容；`管理` 按钮会打开或激活 `已签收条码查询` 页面。
- 统计分析管理菜单新增 `HIV 抗体检测统计` 第一版页面，按 `LS_AS_REPORT.REP_TIME` 月份只读统计三组已确认 HIV 初筛候选项目，只纳入已审核、已发送且姓名非空的报告，按 `REP_NO + MACH_CODE + ITEM_CODE` 去重填充合计行的初筛检测数和初筛阳性数；HIV 明细查询先按月份、审核、发送和仪器从 `LS_AS_REPORT` 筛出候选 `REP_NO`，再按候选 `REP_NO` 分批查询 `LS_AS_REPENTRY` 的目标项目；仪器、病人类型、科室名称字典转为 C++ 内存映射，避免主查询使用大 `OR`、多字典 JOIN 和 `LIKE '%滨水新城%'`；顶部 `新院 / 老院` 筛选会先由 `JC_DEPT_PROPERTY` 分出新院 `DEPT_ID` 集合，选择 `新院` 时下推 `r.DEPT_CODE IN (新院代码...)`，选择 `老院` 时下推 `DEPT_CODE` 为空/`NULL` 或 `r.DEPT_CODE NOT IN (新院代码...)`；`性病门诊` 行暂按下方明细 `科室` 包含 `皮肤科门诊` 的文字口径填充，`其他就诊检测` 行暂按 `科室` 包含 `体检`、`儿童保健`、`健康管理`、`GCP` 或科室为空/`0` 的文字口径填充，`孕产期检查` 行暂按 `科室` 包含 `产科门诊` 或 `早孕关爱门诊` 的文字口径填充，后续再考虑将这三类改为 `DEPT_ID` 精确口径；`受血（制品）前检测` 行按下方明细 `已完结输血单申请号` 非空统计；`术前检测` 行按总数扣除受血、性病门诊、其他就诊和孕产期检查后的剩余量统计；阳性按结果文本判断，`待确认` 优先，其次为 `阳性` 或 `+`，不做结果值与上下限的数值比较；上方样本来源分类表将 `合计` 置顶并用浅蓝背景突出，原 DOCX 统计表导出模板顺序不变；样本来源分类表下方增加独立方法学汇总表，按下方明细行统计 `化学发光法` 和 `酶免法` 的初筛检测数、初筛阳性数；顶部可按 `全部 / 新院 / 老院` 筛选检验科来源；下方明细列表展示仪器、样本来源、方法学、检验科、项目、病人号、姓名、科室、结果和报告时间，其中 `样本来源` 按上方汇总分类口径落单行分类，`方法学` 按 `MACH_CODE` 派生为 `4005/914 -> 化学发光法`、`4008 -> 酶免法`，`病人号` 来自 `LS_AS_REPORT.REG_NO`，`科室` 按 `LS_AS_REPORT.DEPT_CODE -> JC_DEPT_PROPERTY.DEPT_ID -> NAME` 显示，`检验科` 按科室名称是否包含 `滨水新城` 显示 `新院/老院`，`已完结输血单申请号` 先按当前统计月份的 `LS_XK_BloodRequestApply.Apply_Time` 预取当月 `ApplyForm_Statue='已完结'` 申请记录，再按当前明细 `REG_NO` 匹配 `LS_XK_BloodRequestApply.Patient_NO` 的 `ApplyFormNO`；阳性行用红色背景提示，支持点击任意表头对当前已加载明细做本地排序，双击明细行可跳转到 `常规报告` 页面并按目标 `REP_NO` 定位，不额外查询统计数据；`导出统计表` 按钮会基于安装目录 `templates/HIVStatisticsTemplate.docx` 的 `{}` 占位符生成 Word `.docx` 统计表，模版不随项目发布，需通过旁边的 `上传模版` 指定并复制到安装目录；未检测到匹配模版时 `导出统计表` 不可用；`导出明细表` 会将下方当前明细列表按当前排序导出为 UTF-8 BOM CSV，不额外查询数据库；统计表只导出当前汇总数据，非合计行数字为 `0` 时导出为空，合计行保留 `0`。WB 检测数、复检和本年度累计暂未接入。
- 统计分析管理菜单新增 `急诊样本统计` 第一版页面，按签收时间 `LS_AS_BARCODE.IN_DATE` 只读统计唯一急诊条码 `BARCODE`，签收时间筛选精确到分钟，默认当天 `00:00` 到 `23:59`，结束条件按 `< 结束时间 + 1 分钟` 处理；以 `LS_AS_BARCODE.JZ_FLAG=1` 为主急诊口径，并用关联 `LS_AS_REPORT.assaypat_type='0'` 补充识别报告侧急诊；顶部支持院区筛选 `全部 / 老院 / 新院`，院区在 C++ 侧派生过滤：优先按 `LS_AS_BARCODE.sign_dept` 映射 `102=老院`、`401=新院`，字段为空或异常时按申请科室是否包含 `滨水` 兜底；同一条码多条医嘱会聚合为一条记录，当前条码流程状态采用报告链路优先校正：能取到 `LS_AS_REPORT.REP_NO` 至少视为已上机，`LS_AS_REPORT.CHK_FLAG='T'` 至少视为审核完成并按 `CONF` 细分为审核完成未发送/已发送，`CHK_FLAG='T'` 且 `OPER_STATE=3` 显示医生已查看；审核列显示 `LS_AS_REPORT.CHK_FLAG`，发送列显示 `LS_AS_REPORT.CONF`，未完成按报告未发送 `CONF<>'S'` 或为空判断；查询主 SQL 返回条码行级数据，C++ 按 `BARCODE` 聚合并将同条码多条 `ORDER_TEXT` 去重后用 `/` 拼接，仪器名称通过预取 `LS_AS_MACHINE` 字典后在 C++ 内存映射；页面汇总区采用横向表格布局，表头显示急诊条码总数、未完成、未上机、已上机未审核、审核完成、医生已查看和报告已发送等指标，数据行显示对应数量，默认列宽较宽便于查看，下方明细首列为院区，第二列为样本号，并展示当前状态、`签收-审核用时`、签收人、病人信息、医嘱、标本、报告号、仪器和辅助报告状态；明细时间列按 `申请时间=LS_AS_REPORT.REP_DATE`、`签收时间=LS_AS_BARCODE.IN_DATE`、`上机时间=LS_AS_REPORT.CREATE_TIME`、`报告时间=LS_AS_REPORT.REP_TIME` 显示，并放在 `仪器` 列之后；`签收-审核用时` 由软件计算，不由数据库计算，报告已审核后按 `LS_AS_REPORT.REP_TIME - LS_AS_BARCODE.IN_DATE` 固定显示，未审核行按当前软件时间持续刷新，时间缺失显示 `-`；默认勾选 `只看未完成`，明细支持表头本地排序，填充时暂停 ListView 重绘并统一刷新，双击明细行可跳转到 `常规报告` 并按 `REP_NO` 定位目标报告，不写入 LIS 业务表。
- 统计分析管理菜单新增 `质控分析` 页面：用户先在系统设置中维护某台检验仪器的固定质控样本号和项目级质控名称/水平；质控品设置区采用左侧配置列表、右侧双列表单，读取日期使用日历选择器且 `读取项目` 放在同一行，批号开始日期和结束日期也使用日历选择器，其中结束日期允许为空；可按 `仪器 + 样本号 + 指定日期` 只读读取 LIS 项目清单并保存为本机质控项目，重复读取会复用已有样本配置并更新本机项目清单；若右侧质控名称、水平、批号、日期、靶值、SD 或备注非空，会批量覆盖到本次读取到的项目，之后仍可选中单个项目行单独调整。质控批号按样本号维护开始日期、可空结束日期，以及该批号下各项目独立靶值和 SD；质控分析页从右侧操作侧栏选择日期、仪器、水平和状态后，`查询` 只读取本机 SQLite 中已同步的质控结果镜像并套用当前批号/靶值配置重新分析，`导入质控` 会弹出日期范围选择，默认当天；确认后按所选日期范围和已配置质控样本号只读拉取 LIS 明细，并覆盖更新 `qc_result_cache / qc_query_cache_meta`，但不会改动主页面日期筛选或自动刷新当前卡片。页面在内存中按结果日期匹配当时生效的本机批号和项目靶值，完成分组、均值/SD/CV%、Z 值和 Westgard 规则判断，已覆盖 `1-2s / 1-3s / 2-2s / R-4s / 4-1s / 10x`；主区域采用卡片优先看板，摘要条按项目统计失控、警告、在控、无数据和未判定数量，卡片标题按 `（项目英文名）项目名` 显示，正文显示批号、靶值和操作人，未维护靶值时显示“均值”；卡片标题栏单击打开 L-J 图，正文单击只选择卡片；右侧侧栏提供 `L-J图 / 明细 / 导出` 和项目列表，底部明细默认收起并可按需展开；L-J 图支持多水平纵向堆叠、滚动、点位 tooltip、点击图点定位明细和明细高亮图点，并在维护靶值后以靶值作为中心线；状态栏、明细来源列和 CSV 导出会标识 `LIS导入` 或 `本机缓存`。质控样本、项目、批号、项目靶值和本机质控结果镜像保存在 SQLite，质控结果事实来源以 LIS 为准且不写 LIS。
- 工具菜单中的 `常规报告` 已替换原 `工具2` 占位页，按 `temp/模版2.png` 基本完成三栏报告工作台界面：
  - 构建时优先通过 `find_package(LabelPrint 1.2 CONFIG QUIET)` 链接外部 `LabelPrint` package；正式打包建议用 `scripts/build_main.ps1 -LabelPrintPackagePath` 指向 LabelPrint release zip 解压目录。找不到符合版本的 package 时，再按 `LIS_LABELPRINT_DIR` 回退到源码 `add_subdirectory`，默认路径为 `../../020 LabelPrint/LabelPrint`。两者都找不到时仍可构建，但条码打印不可用。
  - 通过左侧 `检验日期` 和 `检验仪器` 查询 `LS_AS_REPORT`，右侧信息列表按样本号升序展示报告主记录。
  - 右侧选中行会回填左侧标本、病人和验单信息，并通过 `REP_NO` 查询中间检验结果列表。
  - 左侧年龄回填时会把 `21 岁 / 6 月 / 4 天 / 7 小时 / 8 分` 这类值拆成数字输入框和单位下拉框；年龄单位下拉固定提供 `岁 / 月 / 天 / 小时 / 分`。
  - 左侧 `检验者 / 审核 / 申请日期 / 签收时间 / 上机时间 / 报告时间` 为只读展示控件，不允许用户手动修改；`急诊` 标签显示为红色，并在前方用勾选框展示当前报告是否为 `assaypat_type=0` 或 `JZ_FLAG=1` 的急诊相关报告。
  - 左侧可输入控件使用独立 Tab 顺序，按页面视觉从上到下跳转焦点，Shift+Tab 反向跳转。
  - 右侧信息列表新增 `标签` 列，按 `LS_AS_BARCODE.JZ_FLAG` 显示急诊标记：`1` 显示 `急`，`0` 不显示；行文字红色由 `LS_AS_REPORT.assaypat_type=0` 或 `JZ_FLAG=1` 触发；报告级危急状态按 `LS_AS_REPORT.assaypat_type=9` 判断，行背景按危急、`CONF`、`CHK_FLAG` 着色。
  - 中间和右侧 ListView 失去焦点时仍保持选中行高亮；自绘时清理 `CDIS_SELECTED/CDIS_FOCUS` 并重绘当前选中行，避免系统非活动选中态覆盖业务行色。
  - 右侧顶部 `样本数 / 上机数 / 审核数 / 发送数` 按当前列表内存数据动态统计，分别对应 `REP_NO` 非空、`NAME` 非空、`CHK_FLAG='T'`、`CONF='S'`。
  - 右侧信息列表支持点击表头进行本地内存排序；排序不会重新查询数据库，并会保留当前选中行和勾选状态。
  - 右侧信息列表上方的上下箭头用于在当前列表内快速选中第一行和最后一行；底部 `上一个 / 下一个` 用于切换当前选中报告的相邻行。底部 `刷新(F5)` 会按当前 `检验仪器` 和 `检验日期` 刷新右侧信息列表，但不会在查询开始前清空现有列表。
  - `刷新(F5)` 和 `自动刷新` 查询完成后按 `LS_AS_REPORT.ID` 恢复当前选中行、勾选行和滚动位置；如果刷新前后行顺序未变，只更新内容变化的单元格，减少闪烁和不必要的界面重建；如果查询后没有可选中报告，则同步清空左侧详情、中间结果和图像状态。
  - 右侧已有选中行时，重复选择当前 `检验日期` 或点击 `今天` 按钮，也会复用保留状态刷新，避免因为日期控件触发查询而清空当前页面信息。
  - 右侧信息列表下方提供 `今天 / 前一天 / 后一天` 快捷按钮，用于切换左侧 `检验日期` 并按当前检验仪器刷新列表；其后 `自动刷新` 默认未启用，勾选后按秒数输入定时刷新，默认 10 秒，最小 5 秒，且不会在上一次查询未完成时叠加发起。
  - 页面底部 `1 / 2 / 3` 按钮会读取系统设置中的快捷检验仪器，快速切换左侧 `检验仪器` 并按当前 `检验日期` 刷新右侧列表；如果当前页面已经是该快捷仪器，则复用保留状态刷新，并把对应按钮文字标记为 `[1] / [2] / [3]`。页面直接打开时会优先自动应用快捷仪器 `1` 并加载当天报告；手动打开检验仪器弹窗时，科室默认 `全部` 并显示全部启用仪器；弹窗顶部 `检索内容` 可输入拼音码或仪器代码快速过滤，匹配结果默认选中第一行，回车即可确认并关闭弹窗，匹配选中后会反向同步科室下拉框；弹窗失焦时采用延迟关闭，避免点击主界面时打断主窗口激活。
  - 左侧 `样本号` 输入框支持输入样本号后按回车，在当前右侧信息列表中定位并选中对应行，同时触发左侧信息和中间结果列表联动更新。
  - 右侧信息列表第一列支持勾选；行右键菜单提供 `打印条码`、`打印勾选条码` 和 `趋势图`。条码打印会把对应行样本号、组合项目、条码号、姓名、标本、开单日期、科室代码、病人号填入 LabelPrint 的 `MedicalLabelData`，再交给共享条码打印 helper 发送到条码打印机；其中条码上的组合项目取自该右侧报告行的 `检验仪器` 列内容，开单日期按 `2026/5/21` 这种仅日期格式打印。右键 `趋势图` 和底部 `趋势图` 按钮复用检验结果查询页趋势图窗口，按当前报告的病人号/姓名、当前检验仪器和科室生成查询条件，窗口顶部提供开始/结束日期选择器和刷新按钮，默认结束日期为当前报告检验日期，开始日期为前 14 天；窗口按父窗口所在显示器工作区居中打开。
  - 条码打印机名读取 `ClientConfig.ini` 的 `[RegularReport] BarcodePrinterName`，默认 `Xprinter XP-360B #2`；打印机型号和 TSPL/ZPL 后端由 LabelPrint 根据 Windows 打印机元数据自动选择，识别不出时按 XP-360B 兼容路径兜底。检测到 Zebra/ZD888t 时，本项目复用 LabelPrint Zebra 测试打印的默认布局，并按 `[RegularReport] ZebraChineseFont` 选择 `E:SIMSUN.TTF` 或 `E:CSONG.TTF` 输出中文，默认 `E:SIMSUN.TTF`；仍清空 fallback，避免两种字体叠印导致文字显示不全；Zebra 路径下标签 `组合项目` 按条码水平区域居中显示。如果打印机名失效，需要在系统设置页重新选择条码打印机。
  - 中间结果列表复用检验结果查询的项目明细查询和 `NORMAL` 偏差展示逻辑；其中 `组合项目` 按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，优先取未删除启用记录，缺失时取同组任意非空名称兜底；连续相同组合项目只在第一行显示；`结果` 列保持白底，其他列使用浅灰底以突出结果值。
  - 中间结果列表优先采用 `LS_AS_REPENTRY.NORMAL_WJ` 的 LIS 判定结果：`9` 表示该项目结果为危急值，`0` 表示该项目存在危急值规则，`NULL` 表示不参与危急值规则；同时保留客户端按 `LS_AS_DEF_ITEMSCOPE` 的 `DNBOUND1 / UPBOUND1` 计算危急值的能力，用于当前展示兜底和后续仪器输入结果后的实时判断。命中危急值时，该行除 `结果` 列外以 `#FFFF39` 黄色背景提示。
  - 右侧信息列表的报告级危急状态按 `LS_AS_REPORT.assaypat_type=9` 判断；未审核且未发送显示 `#FAC0CB`，已审核且已发送显示 `#FFFF39`。
  - 右侧顶部第二行统计当前列表中的 `危急报告数`、`危急报告已审`、`急诊报告数` 和 `急诊报告已审`；危急报告按 `assaypat_type=9` 统计，急诊报告按 `assaypat_type=0` 或 `JZ_FLAG=1` 合并统计，已审均要求已审核且已发送。
  - 中间结果列表的 `结果` 单元格支持界面内临时编辑：单击结果单元格开始编辑，回车提交到当前内存行并跳到下一行继续编辑，失焦或 Esc 取消且不抢回焦点；当前不执行数据库写入。
  - 中间 `图象` 页签打开时，才按当前选中报告的 `REP_NO` 查询 `LS_AS_ITEMPICTURE.PICTURE`；无图像时保持空白，有图像时在左上角固定大图层内按比例绘制，并通过外层滚动视口查看超出窗口的部分。
  - 底部 `图形(T)` 按钮会按当前选中报告打开独立结果图窗口，复用同一张 `LS_AS_ITEMPICTURE.PICTURE` 图片；若中间 `图象` 页签已经加载同一报告，则直接复用已加载图像，否则在弹窗内后台加载后显示。弹窗打开后会跟随右侧信息列表当前选中报告自动刷新，图片采用离屏双缓冲绘制，减少窗口缩放时的图片残影；窗口使用项目图标，关闭或完成拖拽缩放时保存尺寸，下次打开沿用。
  - 中间结果区与右侧信息区之间的拖条可调整宽度，位置保存到 `ClientConfig.ini` 的 `[RegularReport] SplitterX`。
  - 左侧滚动表单不再使用真实 `GROUPBOX` 子窗口，改由内容容器自绘分组边框和标题，并给内部控件启用兄弟裁剪以减少拖动残影；左侧区域宽度当前限制为 360 逻辑像素，内容区始终预留垂直滚动条宽度，避免有无滚动条时自绘分组和控件宽度跳变。
  - 右侧顶部摘要文字改由父面板自绘并按宽度自动换行，避免拖条调整宽度时透明 `STATIC` 控件残影。
- 主程序 `输血结果查询` 模块：
  - 默认按最近 7 天申请日期自动查询。
  - 支持按病人编号、病人姓名、申请单号、申请状态、申请日期筛选。
  - 输血申请主表为 `LS_XK_BloodRequestApply`：`ApplyFormNO` 为输血单申请号，`Apply_Time` 为输血申请时间，`Plan_Date` 为输血计划时间，`ApplyForm_Statue` 为审核状态，`Patient_NO` 为病人号，`Patient_NOType` 为病人类型，`Patient_Name` 为病人姓名。
  - `申请状态` 直接对应 `LS_XK_BloodRequestApply.ApplyForm_Statue` 的中文值，支持“未审核 / 已审核 / 已完结”过滤。
  - 下方列表按 `ApplyForm_Statue` 着色，并显示申请 ABO/RHD、申请成分、病人号、申请单号、审核人、审核时间等字段。
  - 交叉配血记录表为 `LS_XK_BloodCrossMatch`，可通过 `ApplyFormNO` 关联输血申请；已确认 `Patient_NO` 为病人号、`Patient_NOType` 为患者类型、`Patient_Name` 为病人姓名、`VerifyState` 为配血审核状态、`Match_Date` 为配血时间。
  - `输血历史` tab 放在首位并默认展示，会按当前选中申请的 `Patient_NO` 读取 `LS_XK_BloodCrossMatch`，通过 `BloodInID` 联查 `LS_XK_BloodOutInfo / LS_XK_BloodInfo` 及血型、Rh、成分、来源字典表，只读展示出库时间、出库人、血袋编号、产品码、血型、RH(D)、血液成分、血量、单位、配血方法、主侧/次侧结果、配血时间、配血者和血袋来源；出库时间和配血时间由 C++ 端格式化为 `yyyy/M/d H:mm`。
  - `查询检验结果` 窗口可按当前病人号、姓名或身份证查询 LIS 结果；已确认输血申请 `Patient_NO` 与 `LS_AS_REPORT.REG_NO` 同口径，按病人号查询时报告列表和摘要均直接过滤 `LS_AS_REPORT.REG_NO`；按名字查询时会先用当前病人号获取 `LS_AS_REPORT.PAT_PHONE`，取到电话后追加姓名+电话约束以减少重名误匹配；按身份证查询时会从住院病人信息表 `ZY_INPATIENT` 用当前病人号 `INPATIENT_NO` 找到 `SOCIAL_NO`，再查询同一身份证下的所有住院号并过滤 `LS_AS_REPORT.REG_NO`；检验摘要区下方会显示身份匹配可信度提示；摘要区根据可配置项目代码显示最近一次血型鉴定、血红蛋白/血小板、不规则抗体筛查和直接抗人球蛋白试验摘要，新项目显示为“不规则”和“直抗”两行。
  - `查询检验结果` 窗口右侧报告列表的 `样本号` 与检验结果查询页一致，来自 `LS_AS_REPORT.OPER_NO`，并展示 `LS_AS_REPORT.ROOM_CODE / MACH_CODE` 便于配置排除规则；查询完成状态栏会显示命中系统设置中 `血型仪器`、`血常规仪器` 的报告数量。
  - `查询检验结果` 窗口的报告列表使用专用轻量 SQL，仅读取弹窗展示和选择所需字段，并按 `BloodLisExcludeMachines` 下推排除不想展示的科室/仪器；组合项目列表和摘要信息分别走独立后台查询，组合项目列表不等待摘要查询完成；右侧详情列表的 `参考范围` 按下限在前、上限在后显示为 `下限~上限`。
- GitHub Actions 会在 `windows-2022` runner 上使用 VS2022 和 LabelPrint `v1.2.10` Win7 兼容 release 包生成 `LISWorkbench-Setup-<version>-win7-win11.exe` 安装包。该包按 Windows 7 兼容目标构建，目标覆盖 Windows 7 到 Windows 11；实际运行验证仍需在对应系统或虚拟机中完成。

暂未实现：

- 历史库 / 当前库切换。
- 导出、预览、打印、取消发送。
- 医嘱项目、临床科室、临床医生等字典下拉。
- 权限、审核、发送状态业务动作。

## 查询表关系

核心关系：

```text
LS_AS_REPORT.REP_NO = LS_AS_REPENTRY.REP_NO
LS_AS_REPENTRY.ITEM_CODE = LS_AS_ITEM.ITEM_CODE
LS_AS_REPORT.OPER_CODE = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.REP_OPER = JC_EMPLOYEE_PROPERTY.EMPLOYEE_ID
LS_AS_REPORT.TXM_NO = LS_AS_BARCODE.BARCODE
LS_AS_REPORT.TYPE = LS_AS_PATTYPE.TYPE
LS_AS_REPORT.SEX = LS_AS_SEX.SEX_CODE
LS_AS_REPORT.ROOM_CODE = LS_AS_ROOM.ROOM_CODE
LS_AS_REPORT.MACH_CODE = LS_AS_MACHINE.MACH_CODE
```

主表 `LS_AS_REPORT` 用于按病人和报告维度定位报告：

- `NAME`
- `REG_NO`
- `TXM_NO`
- `OPER_NO`
- `BED_CODE`
- `ROOM_CODE`
- `MACH_CODE`
- `GROUP_CODE`
- `CHK_DATE`

明细表 `LS_AS_REPENTRY` 用于查询具体项目结果：

- `ITEM_CODE`
- `ITEM_NAME`
- `RESULT`
- `DOWNBOUND`
- `UPBOUND`
- `ITEM_UNIT`
- `ITEM_ENG`
- `NORMAL`

字典和辅助表：

- `LS_AS_ITEM`：项目名称、单位、英文名。
- `JC_EMPLOYEE_PROPERTY`：人员字典，当前用于“检验者”和“审核者”列；`审核者` 使用 `LS_AS_REPORT.REP_OPER` 关联。
- `JC_DEPT_PROPERTY`：科室字典，`DEPT_ID -> NAME`，当前用于 HIV 统计明细和常规报告相关查询中的科室名称显示。
- `LS_AS_BARCODE`：条码/病人号关联，用于“病人号”筛选。
- `LS_AS_PATTYPE`：病人类型字典。
- `JC_dept_mz_zy`：临床科室字典。结合 `TYPE / TYPENAME` 判断门诊或住院后，用 `DEPT_CODE` 分别匹配门诊科室 ID `mzksid` 或住院科室 ID `zyksid`，得到对应 `DEPT_NAME`（`mzksmc / zyksmc`）。
- `LS_AS_SEX`：性别字典。
- `LS_AS_ROOM`：检验科室字典。
- `LS_AS_MACHINE`：检验仪器字典。

更完整的字段对应和表关系见 [QUERY_DESIGN.md](QUERY_DESIGN.md)。

## NORMAL 码值说明

当前项目基于实际运行数据，右侧项目明细颜色按 `LS_AS_REPENTRY.NORMAL` 解释为：

- `1`：整行红色
- `5`：整行蓝色
- `3`：不处理，保持默认颜色
- `NULL` / 空：不处理，保持默认颜色

说明：

- 这是一条基于现场实测得到的显示规则。
- 如果后续现场验证结论变化，同步修改 `main.cpp` 中的 `result_row_color()`。

## Windows 交叉编译 (macOS)

macOS 可通过 MinGW-w64 toolchain 交叉编译 Windows 版本，使用项目根目录的 `Makefile`：

```bash
# 安装 toolchain
brew install mingw-w64

# Maven 风格构建目标
make              # 帮助（默认）
make compile      # 交叉编译 lis_workbench.exe + Updater.exe
make clean compile  # 清除后编译
make package      # 编译 + NSIS 安装包 + 更新 zip + manifest.json
make verify       # 效验二进制和版本一致性
make install      # 完整发布流程 → out/windows/dist/
```

要求环境：`cmake`、`make`、`x86_64-w64-mingw32-g++`、`makensis`（Homebrew），LabelPrint 本地源码目录需存在。

编译产物：`build/windows-x64/lis_workbench.exe` 等。`make package` 生成的 NSIS 安装包和更新包结构与 Windows `lis.ps1` 产物路径一致，适用于同一个 CI 流水线的 GitHub Release 发布。

## 项目文件

- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)
- [CHANGELOG.md](CHANGELOG.md)
- [packaging/README_windows_installer.md](packaging/README_windows_installer.md)
- [QUERY_DESIGN.md](QUERY_DESIGN.md)
- [WIN32_NATIVE_UI_DESIGN.md](WIN32_NATIVE_UI_DESIGN.md)
- [AUTO_UPDATE_DESIGN.md](AUTO_UPDATE_DESIGN.md)
- [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)
- [TREND_CHART_PLAN.md](TREND_CHART_PLAN.md)

## Windows 安装包（Windows / macOS 通用）

主程序安装包使用 NSIS 生成，详见 `packaging/README_windows_installer.md`。自动更新设计详见 `AUTO_UPDATE_DESIGN.md`；当前已接入 `Updater.exe` 构建、安装包打包、文件夹/HTTP 更新源、统一检查拉取流程、系统设置页的更新源配置和菜单栏 `系统 -> 检查更新` 入口。发现新版本后，主程序会在用户确认后启动 `Updater.exe`，由更新器解压 zip、备份、替换、失败回滚并重启主程序，同时自动写入 `log\updater.log`。GitHub Actions 会同时生成安装包、更新 zip 和 `manifest.json` artifact；推送与版本号一致的 `v*` 标签时会自动发布到 GitHub Release。

VS 原生构建的 `lis_workbench.exe` 默认静态链接 MSVC runtime，Release 安装包通常不需要再携带 `MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`。

常用构建命令已封装：

**Windows** — 根目录 `lis.ps1`：

```powershell
.\lis.ps1 build
.\lis.ps1 run
.\lis.ps1 clean
.\lis.ps1 package -LabelPrintSource github
.\lis.ps1 rebuild-package -LabelPrintSource github -LabelPrintVersion v1.2.10
```

**macOS** — 根目录 `Makefile`（MinGW 交叉编译）：

```bash
make compile
make package
make install
```

`build` / `run` 默认优先使用本地 LabelPrint 源码目录，避免旧 CMake 缓存或外部 package 把 `/MD` 运行库的 `labelprint.lib` 链接进默认 `/MT` 主程序；`package` / `rebuild-package` 默认从 GitHub 下载 LabelPrint release 包，并同时生成 NSIS 安装包、自动更新 zip 和 `manifest.json`。可通过 `-LabelPrintSource github|local|package` 显式选择来源；已有解压包仍可用 `-LabelPrintPackagePath` 指定。`lis.ps1 package` 会优先复用现有 CMake 缓存生成器，没有缓存时再解析实际 Visual Studio 生成器，并选择匹配的 LabelPrint release 包。

项目按 Windows 7 兼容目标编译，CMake 会为 Win32 目标统一设置 `WINVER/_WIN32_WINNT=0x0601`。代码中不能直接导入 Windows 8/10 才有的 API；需要使用时应通过 `GetProcAddress` 动态探测并提供 Win7 回退，避免在 Win7 上出现 `CreateFile2`、`GetDpiForWindow` 等入口点缺失错误。

面向 Windows 7 打包时需要安装 VS 2022 Build Tools，并优先使用 LabelPrint 的 `windows-x64-vs2022-win7` release 包，例如 `.\lis.ps1 rebuild-package -LabelPrintSource github -LabelPrintVersion v1.2.10`。不要把 VS 2026 的 CRT DLL 打入安装目录，否则可能出现 `GetSystemTimePreciseAsFileTime` 等 Win8+ 入口点缺失。只面向 Windows 10/11 时可以显式使用 `-Generator "Visual Studio 18 2026"` 和对应的 LabelPrint VS2026 release 包。

## 使用说明

1. 在 Windows 上运行主程序 `lis_workbench.exe`；独立检验查询工具仍可运行 `result_search.exe`。
2. 点击底部 `设置`。
3. 填写服务器、初始数据库、用户名、密码。
4. 可点击 `测试连接` 验证数据库连接。
5. 点击 `保存` 后，配置会写入程序同目录 `ClientConfig.ini`，设置页面保持打开并在按钮附近显示保存状态。LIS 摘要项目代码可在系统设置里按分号维护，默认值来自当前现场排查结果。
6. 输入姓名、病人号、条码号、日期范围，或通过 `检验科室 / 病人类型 / 报告状态` 下拉筛选。
7. 点击 `查询`。
8. 在中间报告列表选择一行，右侧显示该报告的项目结果。
9. 可点击第一张报告列表的任意列名进行本地排序，不会重新查询报告列表数据库。
10. 可双击第一张报告列表中的报告行跳转到 `常规报告` 页面，并自动定位同一条报告。
11. 如果本次查询使用了病人姓名或病人号，可点击 `趋势图` 查看该查询条件下的项目趋势。
12. 在趋势图窗口右侧点击项目行切换图表，勾选项目后可导出对应趋势明细 CSV。

程序内部会自动生成连接串，格式示例：

```text
Data Source=172.18.3.8\MSSQLSERVER1;Initial Catalog=trasen;User ID=sa;Password=your_password
```

程序会生成 SQL Server 连接串，并在底层自动尝试：

- `ODBC Driver 18 for SQL Server`
- `ODBC Driver 17 for SQL Server`
- `SQL Server`

说明：

- `初始数据库` 直接作为实际查询连接里的 `Initial Catalog`。
- 前端不再手填 ODBC 驱动，驱动选择由底层自动尝试；本次运行内会优先复用上次成功的 driver candidate，失败时再回退完整候选列表。
- 数据库连接层启用 ODBC Driver Manager 连接池并使用严格连接匹配，现有短连接查询模式可复用底层连接。
- ODBC 登录阶段默认 5 秒超时，仅限制建连阶段，不限制 SQL 查询执行时长。
- 该字段会保存到 `ClientConfig.ini`。
