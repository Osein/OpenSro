#include "ShardManager/AQServerEvent.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/AQLogSelector.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

AQ_ServerEvent::AQ_ServerEvent()
{
    m_dwServerEventID = 0;
    m_dw54 = 0;
    m_nEventType = 0;
}

void AQ_ServerEvent::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_strQuery.clear();
}

bool AQ_ServerEvent::Execute()
{
    switch (m_nEventType)
    {
    case 1:
        return ExecUpdate();
    case 2:
        return ExecInsertReward();
    case 3:
    case 4:
        return ExecDeleteOrUpdateReward();
    default:
        // BUG(SM-312): the second %d has no argument.
        LogPrintf(0x2000001, "Not supported DoWork AQ_ServerEventType[%d]!!!. ServerEventID[%d]",
                  m_dwServerEventID);
        BSAssertFail();
        return false;
    }
}

bool AQ_ServerEvent::OnComplete()
{
    return true;
}

bool AQ_ServerEvent::OnFail()
{
    return true;
}

namespace
{

enum
{
    SERVER_EVENT_RESULT_FAILED  = -1001,
    SERVER_EVENT_RESULT_ROW     = -1000,    // no such row (update, delete) or already there
};

// The {?=CALL ...} statement of a server event job on connection 0 of the shard database; the
// return value of the procedure in *pnValue.
int ExecServerEventCall(const std::string& strQuery, __int64* pnValue)
{
    *pnValue = 0;
    if (!g_pShardDB)
        BSAssertFail();
    return CDatabase::ExecDirectWithReturnValue(ShardDB_GetSession(g_pShardDB), 0,
                                                strQuery.c_str(),
                                                DBEXEC_RETURN_INT | DBEXEC_RETURN_BIGINT,
                                                pnValue);
}

} // namespace

bool AQ_ServerEvent::ExecUpdate()
{
    __int64 nValue;
    if (ExecServerEventCall(m_strQuery, &nValue) & ~1)
        return false;
    int nResult = (int)nValue;
    if (nResult >= 0)
        return true;
    if (nResult == SERVER_EVENT_RESULT_FAILED)
    {
        LogPrintf(0x2000001, "Failed to UpdateServerEvent[%d]!! Query : %s", m_dwServerEventID,
                  m_strQuery.c_str());
        return false;
    }
    if (nResult == SERVER_EVENT_RESULT_ROW)
    {
        LogPrintf(0x2000001, "AQ_Type[%d]. Not exists ServerEvent[%d]!! Query : %s",
                  m_nEventType, m_dwServerEventID, m_strQuery.c_str());
        return false;
    }
    return true;
}

bool AQ_ServerEvent::ExecInsertReward()
{
    __int64 nValue;
    if (ExecServerEventCall(m_strQuery, &nValue) & ~1)
        return false;
    int nResult = (int)nValue;
    if (nResult >= 0)
        return true;
    if (nResult == SERVER_EVENT_RESULT_FAILED)
    {
        LogPrintf(0x2000001,
                  "Failed to InsertServerEventReward[%d] ServerEventID[%d] RewardID[%d]!! "
                  "Query : %s",
                  m_dw54, m_dwServerEventID, m_dw54, m_strQuery.c_str());
        return false;
    }
    if (nResult == SERVER_EVENT_RESULT_ROW)
    {
        LogPrintf(0x2000001,
                  "AQ_Type[%d]. exists ServerEventReward[%d] and RewardID[%d]!! Query : %s",
                  m_nEventType, m_dwServerEventID, m_dw54, m_strQuery.c_str());
        return false;
    }
    return true;
}

bool AQ_ServerEvent::ExecDeleteOrUpdateReward()
{
    __int64 nValue;
    if (ExecServerEventCall(m_strQuery, &nValue) & ~1)
        return false;
    int nResult = (int)nValue;
    if (nResult >= 0)
        return true;
    if (nResult == SERVER_EVENT_RESULT_FAILED)
    {
        LogPrintf(0x2000001,
                  "Failed to DeleteServerEventReward[%d] ServerEventID[%d] RewardID[%d]!! "
                  "Query : %s",
                  m_dw54, m_dwServerEventID, m_dw54, m_strQuery.c_str());
        return false;
    }
    if (nResult == SERVER_EVENT_RESULT_ROW)
    {
        LogPrintf(0x2000001,
                  "AQ_Type[%d]. Not exists ServerEventReward[%d] and RewardID[%d]!! Query : %s",
                  m_nEventType, m_dwServerEventID, m_dw54, m_strQuery.c_str());
        return false;
    }
    return true;
}

// -------------------------------------------------------------------------------------------
// AQ_ServerEventLog

AQ_ServerEventLog::AQ_ServerEventLog()
{
    m_dwAfterID = 0;
    m_by54 = 0;
    m_dwServerEventID = 0;
    m_byLogType = 0;
    m_nMode = -1;
}

void AQ_ServerEventLog::ReleaseId()
{
    AQ_Base::ReleaseId();
    // BUG(SM-315): m_strDesc is not cleared; the next request appends to it.
    m_strQuery.clear();
}

bool AQ_ServerEventLog::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_strQuery.clear();
    try
    {
        *pMsg >> m_dwAfterID;
        *pMsg >> m_dwServerEventID;
        *pMsg >> m_byLogType;
        *pMsg >> m_strFrom;
        *pMsg >> m_strTo;
    }
    catch (...)
    {
        LogPrintf(0x2000001, "ServerEvent Log Request Message is invalid");
        m_byResult = AQ_RESULT_INJECTION;
        return false;
    }

    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00,
            "SELECT TOP %d ID, EventTime, ServerEventID, LogType, strDesc FROM _LogServerEvent "
            "WITH (NOLOCK) \t\t\t\t\t\t\t\t\t\t\t\t   WHERE ID > %d AND ServerEventID = %d AND "
            "( EventTime >= '%s' AND EventTime <= '%s' )",
            50, m_dwAfterID, m_dwServerEventID, m_strFrom.c_str(), m_strTo.c_str()) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    m_strQuery.assign(szQuery, strlen(szQuery));
    std::string strFilter;
    BuildLogTypeFilter(&strFilter);
    if (strFilter.size())
        m_strQuery.append(strFilter, 0, std::string::npos);
    m_nMode = AQ_SERVER_EVENT_LOG_SELECT;
    BuildDescription();
    return true;
}

void AQ_ServerEventLog::BuildLogTypeFilter(std::string* pstrFilter)
{
    char szFilter[0x200];
    memset(szFilter, 0, sizeof(szFilter));
    HRESULT hr;
    if (m_byLogType == 0)
        hr = StringCchPrintfA(szFilter, 0x200, "AND LogType IN (%d,%d) ORDER BY EVENTTIME", 1, 2);
    else if (m_byLogType == 1)
        hr = StringCchPrintfA(szFilter, 0x200, "AND LogType = %d ORDER BY EVENTTIME", 3);
    else if (m_byLogType == 2)
        hr = StringCchPrintfA(szFilter, 0x200, "AND LogType = %d ORDER BY EVENTTIME", 4);
    else
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND ( LogType >= %d AND LogType < = %d ) ORDER BY EVENTTIME", 1, 4);
    if (hr != S_OK)
        BSAssertFail();
    pstrFilter->assign(szFilter, strlen(szFilter));
}

void AQ_ServerEventLog::BuildDescription()
{
    m_strDesc.append(" [ServerEvent]", 0xE);
    switch (m_byLogType)
    {
    case 0:     m_strDesc.append(" (Start and End)", 0x10); break;
    case 1:     m_strDesc.append(" (Completion)", 0xD); break;
    case 2:     m_strDesc.append(" (Reward)", 9); break;
    case 0xFF:  m_strDesc.append(" (All)", 6); break;
    default:    m_strDesc.append(" (INVALID!!!)", 0xD); break;
    }
}

bool AQ_ServerEventLog::Execute()
{
    if (m_nMode == AQ_SERVER_EVENT_LOG_SELECT)
    {
        BeginResponse(AQ_OPCODE_SERVER_EVENT_LOG);
        AQ_WriteString(m_pResponse, m_strDesc);
        if (!g_pShardDB)
            BSAssertFail();
        CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_LOG_SERVER_EVENT);
        if (!pTable)
        {
            BSAssertFail();
            m_byResult = AQ_RESULT_FAILED;
            return false;
        }
        // BUG(SM-313): the selected records are never returned to their table.
        CDBRecordList listRows;
        if (!pTable->SelectCustom(m_strQuery, 3, &listRows, 1))
        {
            m_byResult = AQ_RESULT_NO_ROW;
            return false;
        }
        CMassiveMsg* pMsg = m_pResponse;
        BYTE byCount = (BYTE)listRows.size();
        BYTE byOK = 1;
        pMsg->Write(&byOK, 1);
        pMsg->Write(&m_byLogType, 1);
        pMsg->Write(&byCount, 1);
        WriteLogRows(&listRows, m_pResponse);
        return true;
    }
    if (m_nMode == AQ_SERVER_EVENT_LOG_INSERT)
    {
        if (!g_pShardDB)
            BSAssertFail();
        // The mode value is also the connection index.
        int nResult = CDatabase::ExecDirectWithReturnValue(ShardDB_GetLogSession(g_pShardDB),
                                                           m_nMode, m_strQuery.c_str(), 0, NULL);
        if ((nResult & ~1) == 0)
            return true;
        LogPrintf(0x2000001, "AQ_ServerEventLog Query Failed!! Query : %s", m_strQuery.c_str());
        BSAssertFail();
        return false;
    }
    LogPrintf(0x2000001, "Failed to AQ_ServerEventLog Work!!");
    BSAssertFail();
    return false;
}

bool AQ_ServerEventLog::OnComplete()
{
    if (m_nMode == AQ_SERVER_EVENT_LOG_SELECT)
        return AQ_Base::OnComplete();
    return true;
}

bool AQ_ServerEventLog::OnFail()
{
    if (m_nMode == AQ_SERVER_EVENT_LOG_SELECT)
        return AQ_Base::OnFail();
    return true;
}

void AQ_ServerEventLog::WriteLogRows(CDBRecordList* pList, CMassiveMsg* pMsg)
{
    CInstanceLogServerEvent* pLast = NULL;
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        pLast = dynamic_cast<CInstanceLogServerEvent*>(*it);
        if (!pLast)
        {
            BSAssertFail();
            return;
        }
        pMsg->Write(&pLast->ID, 4);
        pMsg->Write(&pLast->EventTime, 16);
        pMsg->Write(&pLast->ServerEventID, 4);
        pMsg->Write(&pLast->LogType, 1);
        AQ_WriteString(pMsg, pLast->strDesc);
    }
    if (!pLast)
        return;

    const TIMESTAMP_STRUCT& time = pLast->EventTime;
    char szTime[0x80];
    if (StringCchPrintfA(szTime, 0x80, "%d-%d-%d %d:%d:%d.%d", time.year, time.month, time.day,
                         time.hour, time.minute, time.second, time.fraction / 1000000) != S_OK)
    {
        BSAssertFail();
        return;
    }
    AQ_WriteText(pMsg, szTime);
}
