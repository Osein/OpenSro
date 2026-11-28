#include "ShardManager/AQScheduleJobs.h"

#include <strsafe.h>
#include <time.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"
#include "ShardManager/ShardAdminFwd.h"
#include "ShardManager/ShardSchedule.h"

using ServerFramework::CClassTrigger;
using ServerFramework::CMassiveMsg;
using NewSchedule::SCHEDULE_DATA;

namespace
{

// The six WORDs year .. second of a date of a raw row (a TIMESTAMP_STRUCT in the place of the
// SYSTEMTIME).
inline const WORD* RowDate(const SYSTEMTIME& st)
{
    return (const WORD*)&st;
}

// The "Schedule Prepare" head of the answers: the text and BYTE 1.
void WriteSchedulePrepare(CMassiveMsg* pMsg)
{
    std::string strText;
    strText.assign("Schedule Prepare", 0x10);
    BYTE byOK = 1;
    AQ_WriteString(pMsg, strText);
    pMsg->Write(&byOK, 1);
}

// 0x4726d0: {BYTE 1, row} per row.
int WriteFlaggedRow(CMassiveMsg* pMsg, DWORD dwQuery)
{
    AQ_DBQuery* pQuery = (AQ_DBQuery*)(DWORD_PTR)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        pQuery->ResetRowBuffer();
        return 1;
    }
    BYTE byRow = 1;
    pMsg->Write(&byRow, 1);
    pMsg->Write(pQuery->m_pBuffer, (int)(DWORD_PTR)pQuery->m_pUserData);
    return 1;
}

// 0x472ce0: the row bytes alone.
int WriteBareRow(CMassiveMsg* pMsg, DWORD dwQuery)
{
    AQ_DBQuery* pQuery = (AQ_DBQuery*)(DWORD_PTR)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        pQuery->ResetRowBuffer();
        return 1;
    }
    pMsg->Write(pQuery->m_pBuffer, (int)(DWORD_PTR)pQuery->m_pUserData);
    return 1;
}

} // namespace

// -------------------------------------------------------------------------------------------
// AQ_ScheduleInit

bool AQ_ScheduleInit::Execute()
{
    BeginResponse(AQ_OPCODE_SCHEDULE_INIT);
    WriteSchedulePrepare(m_pResponse);
    DWORD dwNow = (DWORD)time(NULL);
    m_pResponse->Write(&dwNow, 4);

    NewSchedule::SCHEDULE_DEFINE_DATA row;
    CClassTrigger<AQ_ScheduleInit> trigger(this);
    trigger.m_pfnTrigger2 = &AQ_ScheduleInit::OnQueryRow;
    AQ_DBQuery query;
    query.Setup(&trigger, 4);
    query.SetRow(&row, sizeof(row));
    if (query.ExecuteFormat("SELECT * FROM _RefScheduleDefine") < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

bool AQ_ScheduleInit::OnComplete()
{
    CShard_ScheduleManager::DumpSchedules();
    return AQ_Base::OnComplete();
}

int AQ_ScheduleInit::OnQueryRow(DWORD dwQuery, DWORD)
{
    return WriteFlaggedRow(m_pResponse, dwQuery);
}

// -------------------------------------------------------------------------------------------
// AQ_ScheduleDefine

bool AQ_ScheduleDefine::ReadRequestDefine(CMsg* pMsg, DWORD dwRelayID, DWORD dwParam)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_dwParam = dwParam;
    *pMsg >> m_dwDefineIdx;
    return true;
}

bool AQ_ScheduleDefine::Execute()
{
    BeginResponse(AQ_OPCODE_SCHEDULE_DEFINE);
    WriteSchedulePrepare(m_pResponse);

    SCHEDULE_DATA row;
    CClassTrigger<AQ_ScheduleDefine> trigger(this);
    trigger.m_pfnTrigger2 = &AQ_ScheduleDefine::OnQueryRow;
    AQ_DBQuery query;
    query.Setup(&trigger, 4);
    query.SetRow(&row, sizeof(row));
    if (query.ExecuteFormat("SELECT * FROM _Schedule WHERE ScheduleDefineIdx = %d",
                            m_dwDefineIdx) < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

int AQ_ScheduleDefine::OnQueryRow(DWORD dwQuery, DWORD)
{
    return WriteFlaggedRow(m_pResponse, dwQuery);
}

// -------------------------------------------------------------------------------------------
// AQ_ScheduleChange

AQ_ScheduleChange::AQ_ScheduleChange()
{
    memset(&m_Row, 0, sizeof(m_Row));
}

bool AQ_ScheduleChange::ReadRequestChange(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    *pMsg >> m_byChangeType;
    *pMsg >> m_dwDefineIdx;
    *pMsg >> m_dwScheduleIdx;
    pMsg->ReadBytes(&m_Row, (WORD)sizeof(m_Row));
    return true;
}

bool AQ_ScheduleChange::Execute()
{
    BeginResponse(AQ_OPCODE_SCHEDULE_CHANGE);
    WriteSchedulePrepare(m_pResponse);

    // _InsertSchedule answers the new schedule index.
    DWORD dwNewIdx = 0;
    CClassTrigger<AQ_ScheduleChange> trigger(this);
    trigger.m_pfnTrigger2 = &AQ_ScheduleChange::OnQueryRow;
    AQ_DBQuery query;
    query.Setup(&trigger, 4);
    query.SetRow(&dwNewIdx, 4);

    if (m_byChangeType == AQ_SCHEDULE_INSERT)
    {
        const WORD* pBegin = RowDate(m_Row.stBegin);
        const WORD* pEnd = RowDate(m_Row.stEnd);
        if (query.ExecuteFormat(
                "EXEC _InsertSchedule %d, '%d-%d-%d %d:%d:%d.000', '%d-%d-%d %d:%d:%d.000', %d, "
                "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, '%s', '%s';",
                m_dwDefineIdx,
                (short)pBegin[0], pBegin[1], pBegin[2], pBegin[3], pBegin[4], pBegin[5],
                (short)pEnd[0], pEnd[1], pEnd[2], pEnd[3], pEnd[4], pEnd[5],
                m_Row.nType, m_Row.nInterval, m_Row.byDayOfWeek, m_Row.byDay, m_Row.byWeek,
                m_Row.byMonth, m_Row.byHour, m_Row.byMinute, m_Row.bySecond, m_Row.nDuration,
                m_Row.nRepeatInterval, m_Row.nRepeatLength, m_Row.szName, m_Row.szParam) < 0)
        {
            m_byResult = AQ_RESULT_NO_ROW;
            return false;
        }
        m_dwScheduleIdx = dwNewIdx;
        m_Row.dwScheduleID = dwNewIdx;
    }
    else if (m_byChangeType == AQ_SCHEDULE_DELETE)
    {
        if (query.ExecuteFormat("DELETE FROM _Schedule WHERE ScheduleIdx = %d",
                                m_dwScheduleIdx) < 0)
        {
            m_byResult = AQ_RESULT_NO_ROW;
            return false;
        }
    }

    CMassiveMsg* pMsg = m_pResponse;
    pMsg->Write(&m_byChangeType, 1);
    m_pResponse->Write(&m_dwDefineIdx, 4);
    m_pResponse->Write(&m_dwScheduleIdx, 4);
    m_pResponse->Write(&m_Row, sizeof(m_Row));
    return true;
}

bool AQ_ScheduleChange::OnComplete()
{
    SCHEDULE_DATA data;
    if (m_byChangeType == AQ_SCHEDULE_INSERT)
    {
        g_ShardScheduleManager.AddSchedule(CopyScheduleRow(&m_Row, &data));
        const WORD* pBegin = RowDate(m_Row.stBegin);
        const WORD* pEnd = RowDate(m_Row.stEnd);
        LogPrintf(0x2000001,
                  "Scheduleing : Insert - %d, '%d-%d-%d %d:%d:%d.000', '%d-%d-%d %d:%d:%d.000', "
                  "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, '%s';",
                  m_dwDefineIdx,
                  (short)pBegin[0], pBegin[1], pBegin[2], pBegin[3], pBegin[4], pBegin[5],
                  (short)pEnd[0], pEnd[1], pEnd[2], pEnd[3], pEnd[4], pEnd[5],
                  m_Row.nType, m_Row.nInterval, m_Row.byDayOfWeek, m_Row.byDay, m_Row.byWeek,
                  m_Row.byMonth, m_Row.byHour, m_Row.byMinute, m_Row.bySecond, m_Row.nDuration,
                  m_Row.nRepeatInterval, m_Row.nRepeatLength, m_Row.szParam);
    }
    else if (m_byChangeType == AQ_SCHEDULE_DELETE)
    {
        m_Row.dwDefineID = m_dwDefineIdx;
        m_Row.dwScheduleID = m_dwScheduleIdx;
        g_ShardScheduleManager.RemoveSchedule(CopyScheduleRow(&m_Row, &data));
        LogPrintf(0x2000001, "Scheduleing : Delete - %d, %d", m_Row.dwDefineID,
                  m_Row.dwScheduleID);
    }
    return AQ_Base::OnComplete();
}

int AQ_ScheduleChange::OnQueryRow(DWORD dwQuery, DWORD)
{
    return WriteBareRow(m_pResponse, dwQuery);
}

// -------------------------------------------------------------------------------------------
// AQ_ScheduleLog

bool AQ_ScheduleLog::ReadRequestRange(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    *pMsg >> m_strFrom;
    *pMsg >> m_strTo;
    return true;
}

bool AQ_ScheduleLog::Execute()
{
    BeginResponse(AQ_OPCODE_SCHEDULE_LOG);
    std::string strText;
    strText.assign("Schedule Log", 0xC);
    BYTE byOK = 1;
    AQ_WriteString(m_pResponse, strText);
    m_pResponse->Write(&byOK, 1);

    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_TABLE_LOG_SCHEDULE);
    if (!pTable)
    {
        BSAssertFail();
        m_byResult = AQ_RESULT_FAILED;
        return false;
    }

    char szQuery[0x400];
    memset(szQuery, 0, sizeof(szQuery));
    StringCchPrintfA(szQuery, 0x400,
                     "SELECT * FROM _LogSchedule WITH (NOLOCK) WHERE OccureTime >= '%s' AND "
                     "OccureTime <= '%s'",
                     m_strFrom.c_str(), m_strTo.c_str());
    std::string strQuery(szQuery, strlen(szQuery));
    // BUG(SM-313): the selected records are never returned to their table.
    CDBRecordList listRows;
    if (!pTable->SelectCustom(strQuery, 3, &listRows, 1))
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    WriteLogRows(m_pResponse, &listRows);
    return true;
}

void AQ_ScheduleLog::WriteLogRows(CMassiveMsg* pMsg, CDBRecordList* pList)
{
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CInstanceLogSchedule* pRow = dynamic_cast<CInstanceLogSchedule*>(*it);
        if (!pRow)
        {
            BSAssertFail();
            return;
        }
        BYTE byRow = 1;
        pMsg->Write(&byRow, 1);
        pMsg->Write(&pRow->ID, 4);
        AQ_WriteString(pMsg, pRow->ServerType);
        pMsg->Write(&pRow->ServerBodyID, 4);
        AQ_WriteString(pMsg, pRow->ScheduleDefine);
        pMsg->Write(&pRow->ScheduleIdx, 4);
        AQ_WriteString(pMsg, pRow->Type);
        pMsg->Write(&pRow->OccureTime, 16);
    }
    BYTE byEnd = 0;
    pMsg->Write(&byEnd, 1);
}

// -------------------------------------------------------------------------------------------
// AQ_ScheduleInsertLog

bool AQ_ScheduleInsertLog::Execute()
{
    if (!g_pShardDB)
        BSAssertFail();
    int nResult = CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetLogSession(), 6,
                                                       m_strQuery.c_str(), 0, NULL);
    // SQL_SUCCESS or SQL_SUCCESS_WITH_INFO
    if ((nResult & ~1) == 0)
        return true;
    LogPrintf(0x2000001, "AQ_ScheduleInsertLog Query Failed!! Query : %s", m_strQuery.c_str());
    BSAssertFail();
    return false;
}

BOOL DB_Insert_LogSchedule(AQ_ScheduleInsertLog* pJob, std::string strModule, int nServerID,
                           std::string strDefine, DWORD dwScheduleID, std::string strState,
                           CDateTime dt)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CTask* pOwner = MainProcess_GetJobOwnerTask();
    if (!pOwner)
        BSAssertFail();
    pJob->Init(pOwner);

    char szQuery[0x400];
    memset(szQuery, 0, sizeof(szQuery));
    StringCchPrintfA(szQuery, 0x400,
                     "INSERT INTO _LogSchedule VALUES( '%s', %d, '%s', %d, '%s', "
                     "'%02d-%02d-%02d %02d:%02d:%02d')",
                     strModule.c_str(), nServerID, strDefine.c_str(), dwScheduleID,
                     strState.c_str(), dt.GetYear(), dt.GetMonth(), dt.GetDay(), dt.GetHour(),
                     dt.GetMinute(), dt.GetSecond());
    pJob->m_strQuery.assign(szQuery, strlen(szQuery));
    return TRUE;
}

// -------------------------------------------------------------------------------------------
// AQ_StandardShardScheduleInfo

bool AQ_StandardShardScheduleInfo::Execute()
{
    BeginResponse(AQ_OPCODE_STANDARD_SCHEDULE_INFO);
    WriteSchedulePrepare(m_pResponse);

    AQ_STANDARD_SCHEDULE_ROW row;
    CClassTrigger<AQ_StandardShardScheduleInfo> trigger(this);
    trigger.m_pfnTrigger2 = &AQ_StandardShardScheduleInfo::OnQueryRow;
    AQ_DBQuery query;
    query.Setup(&trigger, 4);
    query.SetRow(&row, sizeof(row));
    if (query.ExecuteFormat(
            "SELECT R.ScheduleName,\tI.ScheduleIdx,I.DateStart,I.DateEnd,I.MainInterval_Type,"
            "I.MainInterval_TypeDate,I.SubInterval_DayOfWeek,I.SubInterval_Days,"
            "I.SubInterval_Weeks,I.SubInterval_Months,I.SubInterval_StartTimeHour,"
            "I.SubInterval_StartTimeMinute,I.SubInterval_StartTimeSecond,"
            "I.SubInterval_DurationSecond,I.SubInterval_RepititionTerm,I.SubInterval_MaintainTime,"
            "I.Param,I.Description FROM _RefScheduleDefine AS R, _Schedule AS I WHERE "
            "I.ScheduleDefineIdx = R.ScheduleDefineIdx ORDER BY R.ScheduleName DESC, "
            "I.ScheduleIdx ASC") < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

int AQ_StandardShardScheduleInfo::OnQueryRow(DWORD dwQuery, DWORD)
{
    return WriteFlaggedRow(m_pResponse, dwQuery);
}

// -------------------------------------------------------------------------------------------
// AQ_ScheduleChangeStandardShard

SCHEDULE_DATA* AQ_CopyStandardScheduleRow(SCHEDULE_DATA* pDst, DWORD dwScheduleIdx,
                                          DWORD dwDefineIdx, const AQ_STANDARD_SCHEDULE_ROW* pSrc)
{
    // The original copies into an uninitialised local: weekday and milliseconds of the dates
    // keep stack contents there and are 0 here.
    const SCHEDULE_DATA& src = pSrc->Data;
    const WORD* pBegin = RowDate(src.stBegin);
    const WORD* pEnd = RowDate(src.stEnd);
    pDst->dwScheduleID = dwScheduleIdx;
    pDst->dwDefineID = dwDefineIdx;
    pDst->stBegin.wYear = pBegin[0];
    pDst->stBegin.wMonth = pBegin[1];
    pDst->stBegin.wDay = pBegin[2];
    pDst->stBegin.wHour = pBegin[3];
    pDst->stBegin.wMinute = pBegin[4];
    pDst->stBegin.wSecond = pBegin[5];
    pDst->stEnd.wYear = pEnd[0];
    pDst->stEnd.wMonth = pEnd[1];
    pDst->stEnd.wDay = pEnd[2];
    pDst->stEnd.wHour = pEnd[3];
    pDst->stEnd.wMinute = pEnd[4];
    pDst->stEnd.wSecond = pEnd[5];
    pDst->nType = src.nType;
    pDst->nInterval = src.nInterval;
    pDst->byDayOfWeek = src.byDayOfWeek;
    pDst->byDay = src.byDay;
    pDst->byWeek = src.byWeek;
    pDst->byMonth = src.byMonth;
    pDst->byHour = src.byHour;
    pDst->byMinute = src.byMinute;
    pDst->bySecond = src.bySecond;
    pDst->nDuration = src.nDuration;
    pDst->nRepeatInterval = src.nRepeatInterval;
    pDst->nRepeatLength = src.nRepeatLength;
    strcpy_s(pDst->szName, 0x100, src.szName);
    // BUG(SM-314): the parameter text is copied with a 256-byte limit.
    strcpy_s(pDst->szParam, 0x100, src.szParam);
    return pDst;
}

bool AQ_ScheduleChangeStandardShard::Execute()
{
    BeginResponse(AQ_OPCODE_SCHEDULE_CHANGE_STANDARD);
    WriteSchedulePrepare(m_pResponse);

    DWORD dwUnused = 0;
    AQ_DBQuery query;
    query.Setup(NULL, 4);
    query.SetRow(&dwUnused, 4);
    if (query.ExecuteFormat("DELETE FROM _Schedule") < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }

    for (std::list<AQ_STANDARD_SCHEDULE_ROW>::iterator it = m_listSchedule.begin();
         it != m_listSchedule.end(); ++it)
    {
        // _InsertSchedule_AllShard answers {schedule index, define index}.
        DWORD adwNew[2] = { 0, 0 };
        CClassTrigger<AQ_ScheduleChangeStandardShard> trigger(this);
        trigger.m_pfnTrigger2 = &AQ_ScheduleChangeStandardShard::OnQueryRow;
        query.Setup(&trigger, 4);
        query.SetRow(adwNew, 8);

        AQ_STANDARD_SCHEDULE_ROW row;
        memcpy(&row, &*it, sizeof(row));
        LogPrintf(0x1000000, " %s, %d", row.szDefineName, row.Data.dwDefineID);
        const WORD* pBegin = RowDate(row.Data.stBegin);
        const WORD* pEnd = RowDate(row.Data.stEnd);
        if (query.ExecuteFormat(
                "EXEC _InsertSchedule_AllShard '%s', '%d-%d-%d %d:%d:%d.000', "
                "'%d-%d-%d %d:%d:%d.000', %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "
                "'%s', '%s';",
                row.szDefineName,
                (short)pBegin[0], pBegin[1], pBegin[2], pBegin[3], pBegin[4], pBegin[5],
                (short)pEnd[0], pEnd[1], pEnd[2], pEnd[3], pEnd[4], pEnd[5],
                row.Data.nType, row.Data.nInterval, row.Data.byDayOfWeek, row.Data.byDay,
                row.Data.byWeek, row.Data.byMonth, row.Data.byHour, row.Data.byMinute,
                row.Data.bySecond, row.Data.nDuration, row.Data.nRepeatInterval,
                row.Data.nRepeatLength, row.Data.szName, row.Data.szParam) >= 0)
        {
            SCHEDULE_DATA data;
            memset(&data, 0, sizeof(data));
            g_ShardScheduleManager.AddSchedule(
                AQ_CopyStandardScheduleRow(&data, adwNew[0], adwNew[1], &row));
        }
        else
        {
            m_byResult = AQ_RESULT_NO_ROW;
        }
    }
    return true;
}

bool AQ_ScheduleChangeStandardShard::ReadRequestList(CMassiveMsg* pMsg, DWORD dwRelayID,
                                                     DWORD dwSessionID)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CTask* pOwner = MainProcess_GetJobOwnerTask();
    if (!pOwner)
        BSAssertFail();
    Init(pOwner);
    m_dwSessionID = dwSessionID;
    m_dwRelayID = dwRelayID;
    m_dw50 = dwSessionID;

    BYTE byMore = 1;
    pMsg->Read(&byMore, 1);
    if (byMore != 1)
        return true;
    do
    {
        AQ_STANDARD_SCHEDULE_ROW row;
        pMsg->Read(&row, sizeof(row));
        m_listSchedule.push_back(row);
        pMsg->Read(&byMore, 1);
    }
    while (byMore == 1);
    return true;
}

bool AQ_ScheduleChangeStandardShard::OnComplete()
{
    if (!m_pResponse)
    {
        BSAssertFail();
        return true;
    }
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->SendRelayReplyMassive(m_dwSessionID, m_dwRelayID, m_pResponse);
    m_pResponse = NULL;
    LogPrintf(0x1000000, "Scheduleing : All Shard Scheduleing is Successed !!");
    return true;
}

bool AQ_ScheduleChangeStandardShard::OnFail()
{
    LogPrintf(0x1000000, "Scheduleing : All Shard Scheduleing is Failed !!");
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->SendRelayReplyMassive(m_dwSessionID, m_dwRelayID, m_pResponse);
    m_pResponse = NULL;
    return true;
}

int AQ_ScheduleChangeStandardShard::OnQueryRow(DWORD dwQuery, DWORD)
{
    return WriteBareRow(m_pResponse, dwQuery);
}
