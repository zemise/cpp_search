/*
  已签收条码查询：SQL Server 性能诊断脚本

  使用方法：
  1. 将下面两个日期替换为出现慢查询时界面中的实际起止时间。
  2. 默认对应：日期类型=签收日期（上机日期当前也使用 b.IN_DATE）、院区=全部、
     专业组=全部、上机状态=全部、取消签收=未勾选。
  3. 在 SSMS 中按 Ctrl+M 启用“包括实际的执行计划”，再执行整个脚本。
  4. 请保留“消息”页中的 SQL Server 执行时间和各表 logical reads，并保存 .sqlplan。

  注意：当前程序的“院区”是在 SQL 取数后按 b.DEPT_NAME 由 C++ 过滤，
  因此本脚本不增加院区 WHERE 条件，以复现当前数据库实际承担的工作量。
*/

SET NOCOUNT ON;
SET STATISTICS IO ON;
SET STATISTICS TIME ON;

SELECT
    ISNULL(LTRIM(RTRIM(rd.OPER_NO)), '') AS sample_no,
    ISNULL(CONVERT(varchar(10), b.JZ_FLAG), '') AS emergency,
    ISNULL(LTRIM(RTRIM(b.BARCODE)), '') AS barcode,
    ISNULL(LTRIM(RTRIM(b.REG_NO)), '') AS reg_no,
    ISNULL(LTRIM(RTRIM(b.TYPENAME)), '') AS type_name,
    ISNULL(LTRIM(RTRIM(b.NAME)), '') AS patient_name,
    ISNULL(LTRIM(RTRIM(b.SEX)), '') AS sex,
    ISNULL(LTRIM(RTRIM(b.DEPT_NAME)), '') AS dept_name,
    ISNULL(LTRIM(RTRIM(b.BEDNO)), '') AS bed_no,
    ISNULL(LTRIM(RTRIM(b.OPER_CODE)), '') AS receiver,
    ISNULL(CONVERT(varchar(19), b.IN_DATE, 120), '') AS receive_time,
    ISNULL(LTRIM(RTRIM(b.ORDER_TEXT)), '') AS order_text,
    ISNULL(LTRIM(RTRIM(b.SAMP_NAME)), '') AS sample_name,
    ISNULL(LTRIM(RTRIM(rd.TESTER_NAME)), '') AS tester_name,
    ISNULL(LTRIM(RTRIM(rd.REVIEWER_NAME)), '') AS reviewer_name,
    ISNULL(CONVERT(varchar(19), rd.REP_TIME, 120), '') AS review_time,
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(32), b.FY))), '') AS fee,
    ISNULL(LTRIM(RTRIM(b.REQ_DRN)), '') AS request_doctor,
    ISNULL(CONVERT(varchar(10), b.ZT_FLAG), '') AS status,
    ISNULL(LTRIM(RTRIM(b.NOTE)), '') AS note,
    ISNULL(LTRIM(RTRIM(b.REASON)), '') AS reason,
    ISNULL(LTRIM(RTRIM(b.sjyq_qsr)), '') AS submitter,
    ISNULL(NULLIF(LTRIM(RTRIM(b.COLLECTION_TIME)), ''),
           ISNULL(CONVERT(varchar(19), b.SUB_DATE, 120), '')) AS submit_time,
    ISNULL(CONVERT(varchar(19), b.REQ_TIME, 120), '') AS request_time,
    ISNULL(CONVERT(varchar(19), b.CANCEL_DATE, 120), '') AS cancel_time,
    ISNULL(LTRIM(RTRIM(b.CANCEL_OPER)), '') AS cancel_operator,
    ISNULL(CONVERT(varchar(30), b.HZID), '') AS hzid,
    CASE
        WHEN ISNULL(rs.REPORT_SENT, 0) = 1 THEN N'发送完成'
        WHEN ISNULL(rs.REPORT_REVIEWED, 0) = 1 THEN N'已审核未发送'
        WHEN ISNULL(rs.HAS_REPORT, 0) = 1 OR ISNULL(b.OPER_STATE, 0) >= 1 THEN N'已上机未审核'
        WHEN ISNULL(b.OPER_STATE, 0) = 0 THEN N'已签收未上机'
        ELSE N''
    END AS machine_status,
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(30), rd.REP_NO))), '') AS report_no,
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(20), rd.MACH_CODE))), '') AS machine_code,
    ISNULL(NULLIF(LTRIM(RTRIM(rd.MACH_NAME)), ''),
           ISNULL(LTRIM(RTRIM(CONVERT(varchar(20), rd.MACH_CODE))), '')) AS machine_name,
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(20), rd.ROOM_CODE))), '') AS room_code,
    ISNULL(CONVERT(varchar(19), rd.CHK_DATE, 120), '') AS inspect_date
FROM LS_AS_BARCODE b WITH (NOLOCK)
OUTER APPLY (
    SELECT
        MAX(CASE
                WHEN NULLIF(LTRIM(RTRIM(CONVERT(varchar(30), r.REP_NO))), '') IS NOT NULL
                THEN 1 ELSE 0
            END) AS HAS_REPORT,
        MAX(CASE WHEN LTRIM(RTRIM(ISNULL(r.CHK_FLAG, ''))) = 'T' THEN 1 ELSE 0 END)
            AS REPORT_REVIEWED,
        MAX(CASE WHEN LTRIM(RTRIM(ISNULL(r.CONF, ''))) = 'S' THEN 1 ELSE 0 END)
            AS REPORT_SENT
    FROM LS_AS_REPORT r WITH (NOLOCK)
    WHERE ISNULL(r.DELETE_BIT, 0) = 0
      AND r.TXM_NO = b.BARCODE
) rs
OUTER APPLY (
    SELECT TOP (1)
        r.REP_NO,
        r.OPER_NO,
        r.CHK_DATE,
        r.REP_TIME,
        r.MACH_CODE,
        r.ROOM_CODE,
        mach.MACH_NAME,
        r.OPER_CODE,
        r.REP_OPER,
        ISNULL(NULLIF(LTRIM(RTRIM(emp_oper.NAME)), ''),
               LTRIM(RTRIM(CONVERT(varchar(50), r.OPER_CODE)))) AS TESTER_NAME,
        ISNULL(NULLIF(LTRIM(RTRIM(emp_rep.NAME)), ''),
               LTRIM(RTRIM(CONVERT(varchar(50), r.REP_OPER)))) AS REVIEWER_NAME
    FROM LS_AS_REPORT r WITH (NOLOCK)
    LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK)
      ON r.MACH_CODE = mach.MACH_CODE
     AND r.ROOM_CODE = mach.ROOM_CODE
     AND mach.DELETE_BIT = 0
    LEFT JOIN JC_EMPLOYEE_PROPERTY emp_oper WITH (NOLOCK)
      ON LTRIM(RTRIM(CONVERT(varchar(50), r.OPER_CODE))) =
         LTRIM(RTRIM(CONVERT(varchar(50), emp_oper.EMPLOYEE_ID)))
    LEFT JOIN JC_EMPLOYEE_PROPERTY emp_rep WITH (NOLOCK)
      ON LTRIM(RTRIM(CONVERT(varchar(50), r.REP_OPER))) =
         LTRIM(RTRIM(CONVERT(varchar(50), emp_rep.EMPLOYEE_ID)))
    WHERE ISNULL(r.DELETE_BIT, 0) = 0
      AND r.TXM_NO = b.BARCODE
      AND NULLIF(LTRIM(RTRIM(CONVERT(varchar(30), r.REP_NO))), '') IS NOT NULL
    ORDER BY r.CHK_DATE DESC, r.REP_TIME DESC, r.REP_NO DESC
) rd
WHERE b.CANCEL_DATE IS NULL
  AND b.IN_DATE >= '2026-09-06 00:00:00'       -- 改为实际开始时间
  AND b.IN_DATE < DATEADD(minute, 1, '2026-09-06 23:59:00') -- 改为实际结束时间

  /* 以下筛选条件仅在界面选择了对应值时取消注释并修改：
  AND b.BARCODE LIKE '%条码片段%'
  AND b.NAME LIKE '%姓名片段%'
  AND b.REG_NO LIKE '%病人号片段%'
  AND CONVERT(varchar(20), b.ROOM_CODE) IN ('10201', '10202')

  -- 示例：仅“发送完成”
  AND ISNULL(rs.REPORT_SENT, 0) = 1
  */
ORDER BY b.IN_DATE ASC, b.ID ASC;

/*
  若界面日期类型是“申请日期”，请只将上面 WHERE 中的两处 b.IN_DATE
  改为 b.REQ_TIME；程序的 ORDER BY 始终固定为 b.IN_DATE, b.ID。

  若勾选“取消签收”，将 b.CANCEL_DATE IS NULL 改为 IS NOT NULL。
*/

SET STATISTICS TIME OFF;
SET STATISTICS IO OFF;

/* 相关表的现有索引明细；此查询只读系统目录，不读取业务数据。 */
SELECT
    OBJECT_NAME(i.object_id) AS table_name,
    i.name AS index_name,
    i.type_desc,
    i.is_unique,
    i.is_disabled,
    ic.key_ordinal,
    ic.is_included_column,
    c.name AS column_name
FROM sys.indexes i
INNER JOIN sys.index_columns ic
  ON ic.object_id = i.object_id
 AND ic.index_id = i.index_id
INNER JOIN sys.columns c
  ON c.object_id = ic.object_id
 AND c.column_id = ic.column_id
WHERE i.object_id IN (
    OBJECT_ID(N'dbo.LS_AS_BARCODE'),
    OBJECT_ID(N'dbo.LS_AS_REPORT'),
    OBJECT_ID(N'dbo.LS_AS_MACHINE'),
    OBJECT_ID(N'dbo.JC_EMPLOYEE_PROPERTY')
)
ORDER BY table_name, index_name, ic.is_included_column, ic.key_ordinal, ic.index_column_id;
