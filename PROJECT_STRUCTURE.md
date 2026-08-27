# lis-workbench 项目结构

## 目录约定

- `src/`
  - Win32 界面 + 跨平台核心层源码
- `src_qt/`（新建，待实现）
  - Qt 5.15 界面层，与 Win32 共存
- `cmake/`
  - `toolchains/` — Windows 交叉编译工具链
- `scripts/`
  - `lis.ps1` — Windows 常用构建/运行/打包统一入口
  - `build_main.ps1` — Win32 主程序和 `Updater.exe` 构建脚本
  - `create_update_package.ps1` — 生成自动更新 zip 和 `manifest.json`
  - `build_windows_package.sh` — Windows 便携包/安装包构建脚本
- `packaging/`
  - `LISWorkbench.nsi` — NSIS 安装包脚本
  - `README_windows_installer.md` — 安装包构建说明
- `resource/`
  - `app.ico` — 应用图标（16+32px）
  - `app.rc` — Windows 资源脚本
  - `app.manifest` — DPI 感知 + Common Controls 清单
  - `resource.h` — 资源 ID 定义
- `build/` — 编译中间产物，不入版本管理
- `out/` — 打包输出，不入版本管理
- `WIN32_NATIVE_UI_DESIGN.md` — 原生 Win32 界面设计约定，记录复选框背景、宿主窗口、字体和布局取舍
- `IMMUNE_DUPLICATE_ITEM_STATISTICS_DESIGN.md` — `统计分析管理 -> 免疫重复项目统计` 设计文档，记录 Win32 页面、`YJ_ZYSQ` 住院申请识别、同一 `INPATIENT_ID` 重复医嘱匹配和分阶段查询口径
- `MASSIVE_TRANSFUSION_STATISTICS_DESIGN.md` — `统计分析管理 -> 大量输血统计` 设计与实现文档，记录24小时事件划分、单位折算、可调统计阈值、状态范围、页面/导出和现场验收口径

## 命名约定

- 对外项目名：`lis-workbench`
- 用户可见程序名：`LIS 工作台`
- CMake project 名：`lis_workbench`
- 配置文件：`ClientConfig.ini`；旧 `result_search.ini` 仅作为升级迁移来源。
- 主程序输出文件：`lis_workbench.exe`
- 自动更新器输出文件：`Updater.exe`
- 兼容保留：独立检验查询工具 `result_search.exe`、`search_core` 和 `search_*` 源文件名暂不重命名，避免影响既有构建脚本和代码引用。

## 文件层级 — 按迁移状态

### 核心层（新 UI 可直接复用）

| 文件 | 职责 | Qt 复用方式 |
|------|------|-----------|
| `search_core.*` | ODBC 数据库查询、driver candidate 缓存、连接池和登录超时 | 切换到 Qt SQL 后端，接口不变 |
| `search_app.*` | `QueryInput` 结构、筛选器组装、状态文案映射 | 原样复用 |
| `search_controller.*` | 测试连接、加载字典、执行查询 | 原样复用（ODBC 切换后） |
| `search_text.*` | `trim`、UTF-8 ↔ 宽字符转换 | Qt 下替换为 `QString`，过渡期保持兼容 |
| `app_settings.*` | `ClientConfig.ini` 读写、连接串生成、LIS 摘要项目代码和输血摘要仪器过滤默认配置 | 切换到 `QSettings`，接口保留 |
| `search_view_state.*` | `ViewState` 聚合运行时状态 | 原样复用 |
| `version.h` | 版本号与标题 | 原样复用 |
| `search_ui_columns.h` | 列号常量 | Qt 中列语义由 model 管理，参考此文件 |
| `trend_core.*` | 趋势数据查询，复用 ODBC driver 缓存、连接池和登录超时策略 | 原样复用（ODBC 切换后） |
| `update_config.h` | 自动更新配置键、更新源取值和 GitHub latest manifest 默认地址 | Win32 主程序和设置页共用 |
| `update_manifest.*` | 自动更新 manifest 解析、版本比较、SHA-256 校验 | 可作为后续 Qt/Win32 共享更新核心 |
| `update_source.*` | 自动更新源抽象、文件夹更新源、HTTP 更新源和统一检查拉取流程 | 主程序菜单 `系统 -> 检查更新` 入口已复用 |

### 边界层（接口保留，实现替换）

| 文件 | Win32 实现 | Qt 替换 |
|------|-----------|---------|
| `search_input_view_model.*` | HWND 控件读写、下拉填充 | QLineEdit/QComboBox/QDateEdit 读写 |
| `search_ui_events.*` | Win32 消息分发 → 回调 | Qt signal/slot |
| `search_ui_presenter.*` | ListView 列定义与行填充 | QTableView + QStandardItemModel |
| `search_ui_layout.*` | Win32 布局、splitter、字体/DPI缩放、公共控件文本宽度测量，以及 ListView 单元格复制菜单预览 | QLayout + QSplitter |
| `search_splitter.*` | Win32 通用拖条控件，向父窗口发送拖动/释放消息 | QSplitter |
| `search_settings_dialog.*` | Win32 模式对话框 | QDialog |
| `trend_window.*` | GDI+ 图表 + ListView | QwtPlot + QTableView |
| `trend_chart_renderer.*` | GDI+ 离屏位图 | QPainter + QCustomPlot |

### 入口层（Win32 独有，Qt 新建等价物）

| 文件 | 职责 |
|------|------|
| `main.cpp` | Win32 入口、消息循环、窗口过程、全局状态（独立查询工具） |
| `main_frame.cpp` | 主程序入口、g_modules[] 菜单注册表、自动菜单/分发、主工具栏快捷入口、工具栏专用入口分发、MDI 活动页与工具栏 active/关闭状态同步，以及 `系统 -> 检查更新` |
| `main_app.h` | 主程序全局上下文 |
| `module_registry.h` | ModuleContext + ModuleDef 统一模块接口；MDI 子窗口按标题激活的单实例 helper |
| `menu_toolbar.cpp/h` | 原生 Win32 自定义工具栏组件；GDI 双缓冲绘制浅色 command bar，支持 hover/pressed/active/disabled、右侧文字型关闭按钮和拉伸占位 |
| `barcode_label_printing.cpp/h` | Win32 主程序条码标签打印共享 helper，统一读取 `[RegularReport] BarcodePrinterName`，封装 LabelPrint 打印调用；常规报告和标本签收中心共用；检测到 Zebra/ZD888t 时复用 LabelPrint Zebra 测试打印的默认布局，并读取 `[RegularReport] ZebraChineseFont` 在 `E:SIMSUN.TTF` 和 `E:CSONG.TTF` 间选择单一中文字体，避免 fallback 叠印，且 Zebra 路径下 `组合项目` 按条码水平区域居中显示 |
| `query_module.cpp/h` | 检验结果查询单实例 MDI 子窗口；第一张报告列表支持点击任意列名本地排序，双击可跳转到常规报告并按 `REP_NO` 精确定位对应报告；报告列表不展示医嘱内容，查询时通过 `skip_order_text` 跳过 `ORDER_TEXT` 聚合，列表批量填充时由共享 presenter 暂停重绘后统一刷新 |
| `barcode_module.cpp/h` | 已签收条码查询单实例 MDI 子窗口，按 `LS_AS_BARCODE` 只读检索；日期类型默认 `签收日期`，起止控件支持小时分钟并默认当天 `00:00` 至 `23:59`；第一行依次显示日期范围、条形码、姓名、病人号、院区、专业组和上机状态，文本框支持回车查询；院区按申请科室是否包含“滨水”派生，专业组仅加载 `LS_AS_ROOM.Dept_Code IN (102,401)` 的有效记录并随院区联动；查询状态位于按钮行下方、列表上方，ListView 占满剩余客户区；结果列表不保留空白占位列，“上机状态”位于“审核者”之后，同一非空条形码的多条医嘱连续成组，仅首行显示样本号至申请科室等公共字段，排序后重新归组且底层数据和 CSV 保持完整；结果支持表头本地排序、右键复制、双击跳转常规报告和导出带院区文件名的 UTF-8 BOM CSV |
| `regular_report_module.cpp/h` | 常规报告单实例 MDI 子窗口，按 `temp/模版2.png` 基本完成报告工作台；按检验日期和检验仪器查询 `LS_AS_REPORT`，检验仪器弹窗从 `LS_AS_ROOM / LS_AS_MACHINE` 加载 `Dept_Code IN (102,401)` 对应房间和启用仪器，按 `ROOM_CODE, MACH_CODE` 排序，显示仪器代码、仪器名称、项目代码、项目名称、样本、拼音码，其中项目和样本来自同仪器 `LS_AS_GROUP.REP_STYLE='M'` 首条主项目，并关联 `LS_CODE_ITEM` 与 `LS_AS_SAMPLE` 显示中文名；科室和仪器字典缓存在当前页面内，数据库连接配置变化时自动重载；科室下拉提供 `全部`，无检索内容且用户未主动选择科室时展示全部仪器，顶部输入拼音码或仪器代码时跨科室本地过滤，匹配结果默认选中第一行，检索框回车可直接确认，选中匹配仪器后反向同步科室下拉框；右侧选中行回填左侧信息并联动中间项目明细；中间组合项目按 `LS_AS_REPENTRY.GROUP_CODE -> LS_AS_LABMATCH.GROUP_NAME` 显示，连续相同项只显示首行，图象页按 `REP_NO` 按需查询 `LS_AS_ITEMPICTURE.PICTURE` 并用滚动视口显示大图；底部 `图形(T)` 打开独立结果图窗口，复用同一图片查询逻辑，跟随右侧选中行刷新，双缓冲绘制并按 `[RegularReport] PicturePopupWidth/Height` 保存尺寸；右侧列表支持本地排序、首末行跳转、今天/前一天/后一天快捷切换检验日期、样本号回车定位、顶部动态统计、保留状态刷新、勾选批量打印；中间和右侧列表批量更新时暂停重绘后统一刷新，自绘 ListView 会在失焦后保持选中行高亮；底部 `1/2/3` 快捷检验仪器读取 `[RegularReport] QuickMachine*`，页面直接打开时会静默应用快捷仪器 `1` 并加载当天报告，检验结果查询跳转目标报告时跳过该默认加载；右键菜单通过共享条码打印 helper 对接 `LabelPrint` 执行 `打印条码`，条码组合项目来自中间明细 `ResultRow.group_name` 去重拼接，非 Zebra 打印机继续走 LabelPrint 自动识别路径，Zebra/ZD888t 走项目内专用 ZPL 路径；右键 `趋势图` 和底部 `趋势图` 按钮复用检验结果查询页趋势图窗口，基于当前报告行病人号/姓名和当前检验仪器生成条件，趋势图窗口顶部可用两个日期选择器调整范围，默认结束日期为当前报告检验日期、开始日期为前 14 天，并按父窗口所在显示器居中打开；构建时优先 `find_package(LabelPrint 1.2)`，找不到再回退 `LIS_LABELPRINT_DIR` 源码接入；中间/右侧拖条位置按 `[RegularReport] SplitterX` 持久化；左侧长表单用自绘分组框替代真实 `GROUPBOX`，右侧顶部摘要由父面板自绘并自动换行，配合 `WS_CLIPSIBLINGS` 降低拖动/滚动残影 |
| `specimen_sign_module.cpp/h` | 标本签收中心单实例 MDI 子窗口，替换原 `工具3`；当前按 `temp/模版.png` 完成界面骨架，日期筛选默认当天起止时间并在跨日后自动切换；条码输入框和查询按钮已接入第一版只读查询，有条码时精确查询 `LS_AS_BARCODE / LS_AS_REPORT` 并可选补查 `V_lis_mzinfo_txm / YJ_MZSQ / YJ_ZYSQ` 回填页面，左侧条码为空时按签收日期 `IN_DATE` 和/或申请日期 `REQ_TIME` 查询已签收条码列表；下方列表补充签收时间、送检时间、年龄、签收人、标本类型和检验室，检验室/仪器分别通过 `LS_AS_ROOM`、`LS_AS_MACHINE` 转名称；`补打条码` 复用常规报告共用的 LabelPrint helper，样本号固定 `补`，组合项目取当前选中行 `检验室`；暂不执行签收、拒签、导出等数据库写入业务操作 |
| `outpatient_query_module.cpp/h` | 门诊查询单实例 MDI 子窗口，位于工具菜单 `常用电话` 前；顶部采用两行筛选布局，并按当前系统字体测量标签宽度，提供收费时间起止、院区、默认不勾选的 `包含非检验科`、门诊号、姓名、身份证和查询按钮，勾选 `包含非检验科` 后不再按院区限定 `ZXKS`，门诊号、姓名和身份证输入框均加宽并支持回车直接查询；门诊号筛选直接对应 `YJ_MZSQ.BLH`，按末尾匹配查询，便于输入短尾号定位完整门诊号，输入门诊号时若前 8 位可推断 `YYYYMMDD`，则按该日期查，否则只查第二个收费时间控件所在日期；下方 ListView 展示门诊收费明细，支持右键复制当前单元格；查询层固定读取 `YJ_MZSQ.BLH / FPH / SQNR / SQKS / DJ / SL / DW / JE / SFRQ / TXM / BBMC / TXMDYSJ / ZXKS`，并通过 `YJ_MZSQ.BRXXID = YY_BRXX.BRXXID` 读取 `SFZH / BRXM / XB / CSRQ`，身份证筛选对应 `YY_BRXX.SFZH`；项目名称后显示 `申请科室`，对应 `YJ_MZSQ.SQKS` 并按 `JC_DEPT_PROPERTY.DEPT_ID -> NAME` 转换为科室名称，匹配不到时回退显示原代码；有条码号的记录按 `TXM` 聚合同条码多行 `SQNR`，项目名称用 `/` 去重拼接，无条码号的记录仍逐行显示并用黄色行背景提示；`FPH` 为空时显示 `0`，卡号显示 `SFZH`，`XB` 按 `1/2` 显示男女，`CSRQ` 转换为年龄，`TXM` 为空时条码号显示 `未生成`，院区按 `ZXKS=102/401` 映射老院/新院，`全部` 默认仅汇总这两个院区，只做只读查询 |
| `phone_directory_module.cpp/h` | 常用电话单实例 MDI 子窗口，替换原 `工具5`；采用 Windows 7 兼容的 `Msftedit.dll` RichEdit 只读查看器加载程序目录下 `documents` 目录中的 RTF 文件；RichEdit 查看区放在浅边框白色文档卡片内并从顶部占满窗口主体，右上角独立白色控制卡片显示两行操作，上行为搜索框，下行为文档下拉、上传和刷新等低频操作，正常宽度下文档卡片会让出右上角控制卡片宽度，避免两者重合；支持上传 RTF、下拉选择多个文件并持久化当前选择；控件字体跟随系统设置字号；搜索框作为主输入焦点，输入变化时立即自动搜索并保留当前输入内容，`Enter` / `F3` 继续查找下一个；内容按页面宽度居中显示，搜索跳转按命中行号定位并立即完整刷新，避免 RichEdit 自动滚动到远离命中的位置；滚动条拖动中节流刷新、滚动结束立即完整刷新，以兼顾 Windows 7 实机残影修复和滑块拖动流畅度；不依赖 Edge/WebView2、Office/WPS 自动化或外部 PDF 阅读器 |
| `hiv_statistics_module.cpp/h` | HIV 抗体检测统计单实例 MDI 子窗口，替换 `统计分析管理 -> 统计分析1`；第一版按年份/月度只读查询 `LS_AS_REPORT.REP_TIME` 范围内的三组 HIV 初筛候选项目，只纳入已审核、已发送且姓名非空的报告；查询先按月份、审核、发送、仪器和可选 `DEPT_CODE IN (...)` 从 `LS_AS_REPORT` 筛出候选 `REP_NO`，再按候选 `REP_NO` 分批查询 `LS_AS_REPENTRY` 的目标 `ITEM_CODE`，并在 C++ 中按报告仪器匹配目标项目；仪器、病人类型和科室名称通过 `LS_AS_MACHINE`、`LS_AS_PATTYPE`、`JC_DEPT_PROPERTY` 小字典缓存后在 C++ 内存映射，方法学按 `MACH_CODE` 派生为 `4005/914 -> 化学发光法`、`4008 -> 酶免法`；顶部 `新院 / 老院` 筛选会先从 `JC_DEPT_PROPERTY` 分出新院 `DEPT_ID` 集合，选择 `新院` 时下推 `r.DEPT_CODE IN (...)`，选择 `老院` 时下推为空科室或 `r.DEPT_CODE NOT IN (新院代码...)`；按 `REP_NO + MACH_CODE + ITEM_CODE` 去重统计合计行的初筛检测数和初筛阳性数，并展示明细核对列表；`性病门诊`、`其他就诊检测`、`孕产期检查` 行暂按明细科室名称文字规则统计，后续再考虑将这三类改为 `DEPT_ID` 精确口径；`受血（制品）前检测` 行按明细 `已完结输血单申请号` 非空统计；`术前检测` 行按总数扣除受血、性病门诊、其他就诊和孕产期检查后的剩余量统计；病人号显示 `LS_AS_REPORT.REG_NO`，已完结输血单申请号按统计月份预取 `LS_XK_BloodRequestApply.Apply_Time` 当月 `ApplyForm_Statue='已完结'` 记录后，再按 `REG_NO` 匹配 `Patient_NO` 的 `ApplyFormNO`，阳性行以红色背景提示；上方合计行置顶并用浅蓝背景突出，样本来源分类表下方增加方法学汇总；明细表展示样本来源列，支持点击任意表头做本地内存排序，双击明细行可跳转到常规报告并定位目标报告，不额外查询统计数据；`导出统计表` 基于安装目录 `templates/HIVStatisticsTemplate.docx` 的 `{}` 占位符生成 `.docx`，模版不随项目发布，需通过页面 `上传模版` 指定并复制到安装目录；未检测到匹配模版时导出按钮不可用；统计表只填写当前汇总结果，不额外查询数据库；明细表可按当前排序导出 UTF-8 BOM CSV；非合计行数字为 `0` 时导出为空，合计行保留 `0`；WB 检测数、复检数和本年度累计暂未接入 |
| `emergency_statistics_module.cpp/h` | 急诊样本统计单实例 MDI 子窗口，替换 `统计分析管理 -> 统计分析2`；第一版按签收时间 `LS_AS_BARCODE.IN_DATE` 统计唯一急诊条码 `BARCODE`，签收时间筛选精确到分钟，默认当天 `00:00` 到 `23:59`，结束条件按 `< 结束时间 + 1 分钟` 处理；以 `JZ_FLAG=1` 为主急诊口径，并用关联 `LS_AS_REPORT.assaypat_type='0'` 补充报告侧急诊；顶部支持院区筛选 `全部 / 老院 / 新院`，院区在 C++ 侧派生过滤，优先按 `LS_AS_BARCODE.sign_dept` 映射 `102=老院`、`401=新院`，字段为空或异常时按申请科室是否包含 `滨水` 兜底；同一条码多条医嘱聚合为一条记录，查询层返回 `LS_AS_BARCODE` 行级数据，C++ 按 `BARCODE` 聚合状态、时间和展示字段，并将同条码多条 `ORDER_TEXT` 去重后用 `/` 拼接；预取 `LS_AS_MACHINE` 字典到 C++ 内存映射仪器名称；条码流程状态由报告链路优先校正，取到 `REP_NO` 至少视为已上机，`CHK_FLAG='T'` 至少视为审核完成并按 `CONF` 细分为审核完成未发送/已发送，`CHK_FLAG='T'` 且 `OPER_STATE=3` 显示医生已查看；审核列显示 `CHK_FLAG`，发送列显示 `CONF`，未完成按报告未发送 `CONF<>'S'` 或为空判断；页面汇总区采用横向表格布局，表头显示急诊条码总数、未完成、未上机、已上机未审核、审核完成、医生已查看和报告已发送等指标，数据行显示对应数量，默认列宽较宽便于查看，下方明细首列为院区、第二列为样本号，并展示 `签收-审核用时`、病人信息、医嘱、标本和辅助报告状态，明细时间列对应 `REP_DATE / IN_DATE / CREATE_TIME / REP_TIME` 并放在 `仪器` 列后，`签收-审核用时` 由软件计算，报告已审核后固定显示报告时间减签收时间，未审核行按当前软件时间持续刷新；明细填充使用 `WM_SETREDRAW` 暂停重绘后统一刷新，支持点击表头本地排序，双击明细行复用常规报告跳转消息并按 `REP_NO` 定位目标报告，不写 LIS 业务表 |
| `backup_blood_statistics_module.cpp/h` | 备血统计单实例 MDI 子窗口，替换 `统计分析管理 -> 统计分析4`；按 `LS_XK_BloodRequestApply.Apply_Time` 日期范围只读查询，以 `TranProperty='备血'`、`UseBloodNote` 包含“备血”或 `Apply_Purpose` 包含“备血”识别备血，不使用 `UrgencyLevel`；申请状态下拉为全部、未审核、已审核、已完结和已驳回，独立“包含已删除”开关控制删除数据；备血类型下拉为全部、申请类型、用血备注、输血目的和多项命中；按唯一 `ApplyFormNO` 去重并显示备血口径汇总及状态分布；院区下拉提供全部、老院、新院，C++ 按 `Apply_Dept` 是否包含“滨水”派生后过滤，再计算汇总、状态分布、明细和空申请单号异常，不在 SQL 中增加院区模糊条件；顶部采用两行筛选布局并动态测量标签宽度；明细首列和带院区后缀的 CSV 显示院区，整行以未审核橙色、已审核蓝色、已完结绿色、已驳回红色、已删除灰色区分，并参照“已签收条码查询”提供色块图例；支持备血类型内存筛选和表头本地排序；双击未删除明细会按申请单号和申请日期打开或激活“输血结果查询”、选中对应行并刷新详情，保持右侧页签不变，已删除明细不跳转；不写 LIS 业务表 |
| `massive_transfusion_statistics_module.cpp/h` | 大量输血统计单实例 MDI 子窗口；正式默认实际输血量和配血时间，并保留申请量对照。实际口径只读关联 `BloodCrossMatch -> BloodInfo -> CompositionInfo`，并将 `BloodOutInfo` 按 `BloodInID` 一次性预聚合，以 `VerifyState='已审核'` 和唯一 `BloodInID` 统计实际血袋；申请号直接等值连接申请表。支持四种事件时间、24小时事件、跨科室标记、异常核查和逐袋 CSV；实际口径的制品构成只按实际血袋汇总，申请单异常不覆盖实际输血事实。页面使用分组筛选、六项关键指标卡片、精简默认列、动态明细页签和事件上下文，隐藏技术字段仍完整导出。规则 `v3` 按 `ML×1 / 普通U×200 / 冷沉淀U×20 / 治疗量×250` 折算，保留制品开关和可调阈值。申请量及实际量纯聚合入口分别为 `build_massive_transfusion_statistics` 和 `build_actual_massive_transfusion_statistics`，均由独立测试覆盖。 |
| `immune_duplicate_statistics_module.cpp/h` | 免疫重复项目统计单实例 MDI 子窗口，替换 `统计分析管理 -> 统计分析3`；顶部采用两行筛选布局，签收时间范围独占第一行，查询、导出和状态文本位于第二行，标签宽度按当前字体与 DPI 动态测量；阶段 1 完成 Win32 页面骨架，阶段 2 已接入 `query_immune_duplicate_statistics` 后台只读查询，按 `YJ_ZYSQ.JSSJ / SQNR / INPATIENT_ID` 和 `ZXKS IN (102,401)` 查询基准及重复住院申请，通过 `ZY_INPATIENT.INPATIENT_NO` 展示病人号、`SQKS -> JC_DEPT_PROPERTY` 展示申请科室，按 `YJSQID` 去重后填充汇总和明细；不写业务表，CSV、基于条码表 `BEDNO` 的床号回填和 LIS 报告跳转尚未接入 |
| `quality_control_module.cpp/h` | 质控分析单实例 MDI 子窗口，替换 `统计分析管理 -> 质控分析`；用户在系统设置中维护某台检验仪器的固定质控样本号和项目级质控名称/水平，质控品设置页由 `quality_control_settings_dialog.*` 提供左侧列表和右侧双列表单，可用日历选择器维护读取日期、批号开始日期和可空结束日期，`读取项目` 放在读取日期同一行；按 `仪器 + 样本号 + 指定日期` 只读读取 LIS 项目清单保存为本机质控项目，重复读取会复用已有样本配置并更新本机项目清单，且会把右侧非空的质控名称、水平、批号、日期、靶值、SD 和备注批量覆盖到本次读取项目，之后仍可选中单个项目行单独调整；批号按样本号维护，靶值和 SD 按 `批号 + 项目` 独立维护；页面右侧操作侧栏选择日期、仪器、水平和状态后，`查询` 只读取本机 SQLite 质控结果镜像并套用当前配置分析，`导入质控` 会弹出日期范围选择，默认当天；确认后按所选日期范围和已配置质控样本号只读拉取 `LS_AS_REPORT + LS_AS_REPENTRY` 明细，并覆盖更新 `qc_result_cache / qc_query_cache_meta`，但不改动主页面日期筛选，也不自动刷新当前卡片；质控结果事实来源保持 LIS，SQLite 保存本机 `qc_sample_config / qc_sample_item / qc_lot / qc_lot_item_target` 和本机质控结果镜像；页面在内存中按结果日期匹配当时生效批号和项目靶值，再按仪器、样本号、项目和水平分组，计算均值、SD、CV%、Z 值和 Westgard 规则，已覆盖 `1-2s / 1-3s / 2-2s / R-4s / 4-1s / 10x`；主区域为卡片优先看板，摘要条按项目统计失控、警告、在控、无数据和未判定数量，卡片标题按 `（项目英文名）项目名` 显示，正文显示批号、靶值和操作人，未维护靶值时显示“均值”；右侧操作侧栏提供 `L-J图 / 明细 / 导出` 和项目列表，底部明细默认收起并可按需展开；卡片标题栏单击打开 L-J 图，正文单击只选择卡片；L-J 图由 `quality_control_chart_renderer.*` 使用 GDI 绘制，支持多水平纵向堆叠、滚动、tooltip、图点和明细联动，维护靶值后中心线优先使用靶值；状态筛选和 CSV 导出均基于当前内存结果，不重新查询 LIS，并显示 `LIS导入 / 本机缓存` 来源 |
| `blood_module.cpp/h` | 输血结果查询单实例 MDI 子窗口，按 `LS_XK_BloodRequestApply` 只读检索，已确认申请主表 `ApplyFormNO / Apply_Time / Plan_Date / ApplyForm_Statue / Patient_NO / Patient_NOType / Patient_Name` 等字段含义，`ApplyForm_Statue` 状态值为 `已审核 / 未审核 / 已完结`；通过 `LS_XK_BloodRequestApplySon.ApplyFormNO` 聚合申请成分；`输血历史` tab 放在首位并默认展示，按当前选中申请的 `Patient_NO` 读取 `LS_XK_BloodCrossMatch`，并通过 `BloodInID` 联查 `LS_XK_BloodOutInfo / LS_XK_BloodInfo` 及血型、Rh、成分、来源字典表，只读展示出库时间、出库人、血袋编号、产品码、血型、RH(D)、血液成分、血量、单位、配血方法、主侧/次侧结果、配血时间、配血者和血袋来源，出库时间和配血时间由 C++ 端格式化为 `yyyy/M/d H:mm`；LIS 结果弹窗列表与摘要分离后台查询，已确认输血申请 `Patient_NO` 与 `LS_AS_REPORT.REG_NO` 同口径，按病人号查询时报告列表和摘要均直接过滤 `LS_AS_REPORT.REG_NO`；按名字查询时先用当前病人号取 `LS_AS_REPORT.PAT_PHONE`，取到电话后对报告列表和摘要追加 `PAT_PHONE` 约束以减少重名误匹配；按身份证查询时用当前病人号匹配 `ZY_INPATIENT.INPATIENT_NO`，读取 `SOCIAL_NO` 后查询同一身份证下所有住院号，并对报告列表和摘要下推 `LS_AS_REPORT.REG_NO IN (...)`；报告列表使用专用轻量 SQL，仅读取 `REP_NO / OPER_NO / CHK_DATE / GROUP_NO / TXM_NO / 检验者 / 审核者 / 年龄 / 性别 / ROOM_CODE / MACH_CODE` 等弹窗所需字段，避开通用报告列表的医嘱聚合和无关字典联查；报告列表新增科室代码和仪器代码列，并按 `[LisSummary] BloodLisExcludeMachines` 的 `ROOM:;ROOM:MACH1,MACH2` 规则下推排除不想展示的科室/仪器；最近血型鉴定和血红蛋白/血小板摘要读取 `[LisSummary] BloodTypeMachines / CbcMachines` 的 `ROOM:MACH1,MACH2;ROOM:MACH` 成对配置，下推为 `LS_AS_REPORT.ROOM_CODE + MACH_CODE` 条件；不规则抗体筛查和直接抗人球蛋白试验摘要按 `[LisSummary] IrregularAntibodyCodes / DirectAntiglobulinCodes` 项目代码分别取最新非空结果，并显示为“不规则”“直抗”两行；弹窗取消顶部条件区，改为三列工作区：左侧采用 `患者信息 / 摘要信息` 两个无边框小节和下方查询范围区纵向排列，中间为组合项目列表，右侧为详情信息列表，详情列表 `参考范围` 按 `下限~上限` 展示；并将绿色身份匹配提示放在 `摘要信息` 标题右侧；患者字段标签统一为三字宽度，摘要日期显示为 `yyyy/M/d`，摘要无结果时显示为 `血型：无`、`直抗：无` 等短格式，`RhD阴性` 及不规则/直抗 `阳性` 结果以红色提示，查询范围使用 7-365 横向滑块；弹窗按当前显示器工作区限制最大尺寸并居中，默认查询最近 14 天 LIS 结果 |
| `settings_module.cpp/h` | 系统设置单实例 MDI 子窗口，维护数据库、字号、LIS 摘要项目代码、输血摘要仪器过滤、常规报告条码打印机、Zebra 中文字体、底部快捷检验仪器和自动更新源配置；页面采用原生 Win32 分组卡片布局，字号下拉限制为 `9 / 11 / 12 / 13`，自动检查更新复选框使用纯原生 `BUTTON + BS_AUTOCHECKBOX` 并通过白色宿主窗口统一背景，保存后页面不关闭且在按钮附近显示状态 |
| `search_ui_context.h` | Win32 句柄集合、字体上下文 |
| `updater_main.cpp` | 独立 `Updater.exe` 入口，支持 zip 包/已展开目录，负责等待主程序退出、解压、备份、替换、失败回滚、重启，并自动写入 `log\updater.log` |

## 代码分层约定

- 核心层禁止包含 `<windows.h>`、`HWND`、`HDC` 等 Win32 类型。
- 核心层只使用 C++17 标准库和 `<sqlext.h>`（ODBC 过渡期）。
- `QueryInput`（定义在 `search_app.h`）是界面层与核心层的唯一数据契约。
- 边界层可包含 Win32 类型，但接口上暴露的数据结构（如 `ViewState`）保持干净。
- 入口层自由使用任意 Win32 API。

## 当前成熟度

- Win32 版：主界面、设置窗口、趋势窗口均已完成功能。DPI 感知、图标、防闪烁到位。
- 核心库：`search_core` 静态库已提取，Win32 和 Qt 共享。
- 主程序壳：`lis_workbench.exe` 已接入检验结果查询、输血结果查询、已签收条码查询、质控分析和系统设置等 MDI 子窗口。
- Qt 编译链路已通：CI 双绿 + Windows 实机构建脚本（`scripts/build_qt.ps1`）。
- Qt 四大组件已实现：设置对话框、主窗口（查询+双列表）、趋势窗口（QwtPlot 折线图）。
- 详见 [QT_MIGRATION_GUIDE.md](QT_MIGRATION_GUIDE.md)。

## 演进方向

- ~~提取核心层为 `search_core` 静态库~~ ✅
- ~~搭建 Qt 5.15 编译链路~~ ✅
- ~~主程序阶段 1：MDI 窗口壳~~ ✅
- ~~主程序阶段 2.3：系统设置接入~~ ✅
- ~~主程序阶段 2.1：检验结果查询接入~~ ✅
- ~~主程序阶段 2.2：输血结果查询接入~~ ✅
- ~~工具模块：已签收条码查询接入~~ ✅
- ~~工具模块：常规报告界面接入~~ ✅
- ~~统计模块：质控分析接入~~ ✅
- ~~模块系统改造（5.1~5.7）~~ ✅
- **当前**：自动更新基础设施、主程序模块细节打磨与现场验证
- **中期**：Qt 版本功能对齐 Win32 版
- **长期**：Qt 版本稳定后，Win32 入口降级为可选回退构建
