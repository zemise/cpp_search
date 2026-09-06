/*
  已签收条码查询第二轮 A/B 测试：主查询移除人员字典关联

  与第一轮 SQL 相比，本脚本仅做两件事：
  1. 最近报告 OUTER APPLY 只返回 OPER_CODE / REP_OPER，不关联人员表。
  2. 测试结果中的 tester_name / reviewer_name 暂时显示人员代码。

  正式实现时可沿用项目现有统计模块的做法：单独读取并缓存人员字典，
  再在 C++ 内存中将代码映射为姓名。这样既保持原显示口径，也不让人员表
  参与大结果集主查询。

  报告状态、最近报告排序、机器名称、输出列数量、日期条件及排序均保持不变。
  本脚本只读，不创建或修改任何索引。

  请把日期改成与第一轮完全相同的范围，并在 SSMS 中按 Ctrl+M 启用实际执行计划。
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
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(50), rd.OPER_CODE))), '') AS tester_name,
    ISNULL(LTRIM(RTRIM(CONVERT(varchar(50), rd.REP_OPER))), '') AS reviewer_name,
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
        r.REP_OPER
    FROM LS_AS_REPORT r WITH (NOLOCK)
    LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK)
      ON r.MACH_CODE = mach.MACH_CODE
     AND r.ROOM_CODE = mach.ROOM_CODE
     AND mach.DELETE_BIT = 0
    WHERE ISNULL(r.DELETE_BIT, 0) = 0
      AND r.TXM_NO = b.BARCODE
      AND NULLIF(LTRIM(RTRIM(CONVERT(varchar(30), r.REP_NO))), '') IS NOT NULL
    ORDER BY r.CHK_DATE DESC, r.REP_TIME DESC, r.REP_NO DESC
) rd
WHERE b.CANCEL_DATE IS NULL
  AND b.IN_DATE >= '2026-09-06 00:00:00' -- 改成第一轮实际开始时间
  AND b.IN_DATE < DATEADD(minute, 1, '2026-09-06 23:59:00') -- 改成第一轮实际结束时间

  /* 第一轮使用了附加筛选时，在这里加入完全相同的条件：
  AND b.BARCODE LIKE '%条码片段%'
  AND b.NAME LIKE '%姓名片段%'
  AND b.REG_NO LIKE '%病人号片段%'
  AND CONVERT(varchar(20), b.ROOM_CODE) IN ('10201', '10202')

  -- 示例：仅“发送完成”
  AND ISNULL(rs.REPORT_SENT, 0) = 1
  */
ORDER BY b.IN_DATE ASC, b.ID ASC;

SET STATISTICS TIME OFF;
SET STATISTICS IO OFF;

/* 核对人员代码字段类型，为后续字典缓存实现提供依据；请一并返回这一结果。 */
SELECT
    OBJECT_NAME(c.object_id) AS table_name,
    c.name AS column_name,
    TYPE_NAME(c.user_type_id) AS data_type,
    c.max_length,
    c.is_nullable,
    c.collation_name
FROM sys.columns c
WHERE (OBJECT_NAME(c.object_id) = 'JC_EMPLOYEE_PROPERTY'
       AND c.name = 'EMPLOYEE_ID')
   OR (OBJECT_NAME(c.object_id) = 'LS_AS_REPORT'
       AND c.name IN ('OPER_CODE', 'REP_OPER'))
ORDER BY table_name, column_name;
