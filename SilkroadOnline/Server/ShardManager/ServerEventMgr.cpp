#include "ShardManager/ServerEventMgr.h"

#include <time.h>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ServerEventExtern.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    SHARD_TABLE_SERVER_EVENT = 0x35,
    SHARD_TABLE_SERVER_EVENT_REWARD = 0x36,
};

// Values of RefServerEvent::b50 (the reward target) and b58 (progress events).
enum
{
    SERVER_EVENT_TARGET_ALL = 1,
    SERVER_EVENT_TARGET_USER = 2,
};

// GM_GetLocalTimestamp (0x41b490): the local time, fraction 0.
static void ServerEvent_GetLocalTimestamp(TIMESTAMP_STRUCT& ts)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    ts.year = st.wYear;
    ts.month = st.wMonth;
    ts.day = st.wDay;
    ts.hour = st.wHour;
    ts.minute = st.wMinute;
    ts.second = st.wSecond;
    ts.fraction = 0;
}

// Util_SystemTimeDiffSeconds (0x461240, RefData_TimestampDiffSeconds of ReferenceData.cpp):
// seconds from tsFrom to tsTo, both local times; the callers use the low 32 bits.
static int ServerEvent_DiffSeconds(const TIMESTAMP_STRUCT& tsFrom, const TIMESTAMP_STRUCT& tsTo)
{
    const TIMESTAMP_STRUCT* apts[2] = { &tsFrom, &tsTo };
    __time64_t at[2];
    for (int i = 0; i < 2; ++i)
    {
        struct tm t;
        memset(&t, 0, sizeof(t));
        t.tm_year = apts[i]->year - 1900;
        t.tm_mon = apts[i]->month - 1;
        t.tm_mday = apts[i]->day;
        t.tm_hour = apts[i]->hour;
        t.tm_min = apts[i]->minute;
        t.tm_sec = apts[i]->second;
        t.tm_isdst = -1;
        at[i] = _mktime64(&t);
    }
    return (int)(at[1] - at[0]);
}

// Seconds from now to ts.
static int ServerEvent_SecondsUntil(const TIMESTAMP_STRUCT& ts)
{
    TIMESTAMP_STRUCT tsTarget = ts;
    TIMESTAMP_STRUCT tsNow;
    ServerEvent_GetLocalTimestamp(tsNow);
    return ServerEvent_DiffSeconds(tsNow, tsTarget);
}

// CDBObjectList_DetachAndClear (0x40a7e0): every record of the list back to the table.
static void ServerEvent_FreeRecordList(CDBRecordList& list, CDBTable* pTable)
{
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
        pTable->FreeRecord(*it);
    list.clear();
}

static CMsg* ServerEvent_AllocMsg()
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    if (pMsg)
        pMsg->SetOpcode(MSG_SHARD_SERVER_EVENT);
    return pMsg;
}

static void ServerEvent_Broadcast(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

static CReferenceData* ServerEvent_GetRefData()
{
    if (!g_pReferenceData)
        BSAssertFail();
    return g_pReferenceData;
}

CServerEventMgr* g_pServerEventMgr = NULL;
CServerEventMgr g_ServerEventMgr;

// ---------------------------------------------------------------------------------------------
// Construction

CServerEventMgr::CServerEventMgr()
    : m_pStreamOwner(NULL)
{
    if (g_pServerEventMgr)
        BSAssertFail();
    g_pServerEventMgr = this;
    m_Stream.m_nReadPos = 0;
    m_Stream.m_nWritePos = 0;
    m_Stream.m_dw08 = 0;
    m_Stream.m_nCapacity = 0;
    m_Stream.m_pBuffer = NULL;
    m_Buffer.m_pBuffer = (BYTE*)operator new(0x400);
    m_Buffer.m_nCapacity = 1024;
    if (!m_Buffer.m_pBuffer)
        BSAssertFail();
    m_Buffer.m_nWritePos = 0;
    m_Buffer.m_nReadPos = 0;
}

CServerEventMgr::~CServerEventMgr()
{
    if (m_Stream.m_pBuffer)
        BSAssertFail();
    g_pServerEventMgr = NULL;
}

BOOL CServerEventMgr::LoadFromDB()
{
    if (!LoadServerEvents())
        return FALSE;
    return LoadServerEventRewards() != FALSE;
}

void CServerEventMgr::Run()
{
    ProcessDelayJobs();
    FlushPendingDelayJobs();
}

// ---------------------------------------------------------------------------------------------
// Loading

BOOL CServerEventMgr::LoadServerEvents()
{
    if (m_mapServerEvent.size() || m_mmapDetectingTarget.size())
    {
        LogPrintf(0x2000001, "Already loaded server event data");
        BSAssertFail();
        return FALSE;
    }
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_SERVER_EVENT);
    if (!pTable)
    {
        BSAssertFail();
        return FALSE;
    }
    CDBRecordList list;
    if (!pTable->SelectAll(6, &list))
    {
        ServerEvent_FreeRecordList(list, pTable);
        BSAssertFail();
        return FALSE;
    }
    if (list.empty())
    {
        LogPrintf(0x2000001, "Not exists ServerEvent Records");
        return TRUE;
    }
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
    {
        CInstanceServerEvent* pRecord = static_cast<CInstanceServerEvent*>(*it);
        if (!pRecord)
            goto failed;
        RefServerEvent* pRef = ServerEvent_GetRefData()->FindServerEvent(pRecord->ID);
        if (!pRef)
        {
            LogPrintf(0x2000001,
                      "Not found RefServerEvent[%d]. Check service and date value. progress next "
                      "data", pRecord->ID);
            pTable->FreeRecord(pRecord);
            continue;
        }
        if (ServerEvent_SecondsUntil(pRef->tsBegin) > 0)
        {
            if (!AppendDelayJobForWaitRegisterServerEvent(pRef, pRecord))
            {
                LogPrintf(0x2000001, "Failed to AppendDelayJobForWaitRegisterServerEvent[%d]",
                          pRecord->ID);
                goto failed;
            }
            continue;
        }
        CServerEvent* pEvent = CServerEvent::PoolAlloc();
        if (!pEvent)
        {
            LogPrintf(0x2000001, "Failed to Create ServerEvent[%d]", pRecord->ID);
            goto failed;
        }
        if (!pEvent->Attach(pRef, pRecord))
        {
            LogPrintf(0x2000001, "Failed to initialize ServerEvent[%d]", pRecord->ID);
            goto failed;
        }
        if (!InsertServerEvent(pEvent, pRef))
        {
            LogPrintf(0x2000001, "Failed to InsertServerEvent[%d]", pEvent->GetID());
            goto failed;
        }
        if (!AppendDelayJobForCheckEndServerEvent(pEvent, FALSE))
        {
            LogPrintf(0x2000001, "Failed to AppendDelayJobForCheckEndServerEvent[%d]",
                      pEvent->GetID());
            goto failed;
        }
        if (!ServerEventLog_LogDetecting(pEvent))
            LogPrintf(0x2000001, "Failed to append start ServerEvent[%d] Log", pEvent->GetID());
    }
    list.clear();
    return TRUE;

failed:
    // BUG(SM-138): every record of the list goes back to the table, also the ones the events
    // registered before hold.
    BSAssertFail();
    ServerEvent_FreeRecordList(list, pTable);
    return FALSE;
}

BOOL CServerEventMgr::LoadServerEventRewards()
{
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTable = ShardDB_GetTables(g_pShardDB);
    if (vecTable.empty() || vecTable.size() <= SHARD_TABLE_SERVER_EVENT_REWARD)
    {
        BSAssertFail();
        BSAssertFail();
        return FALSE;
    }
    CDBTable* pTable = vecTable[SHARD_TABLE_SERVER_EVENT_REWARD];
    if (!pTable)
    {
        BSAssertFail();
        return FALSE;
    }
    CDBRecordList list;
    if (!pTable->SelectAll(1, &list))
    {
        ServerEvent_FreeRecordList(list, pTable);
        BSAssertFail();
        return FALSE;
    }
    if (list.empty())
        return TRUE;
    CDateTime now = CDateTime::GetCurrent();
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
    {
        CInstanceServerEventReward* pRecord = static_cast<CInstanceServerEventReward*>(*it);
        if (!pRecord)
            goto failed;
        RefServerEventReward* pRef = ServerEvent_GetRefData()->FindServerEventReward(
            pRecord->RewardID);
        if (!pRef)
        {
            LogPrintf(0x2000001,
                      "Not found RefServerEventReward[%d]. Check Service and Date Data!! Pass!!",
                      pRecord->RewardID);
            pTable->FreeRecord(pRecord);
            continue;
        }
        CServerEventReward* pReward = CServerEventReward::PoolAlloc();
        if (!pReward)
            goto failed;
        if (!pReward->Link(pRecord, pRef))
        {
            LogPrintf(0x2000001, "Failed to Initialize ServerEventReward[%d]",
                      pReward->GetRewardID());
            goto failed;
        }
        if (!InsertServerEventReward(pReward))
        {
            LogPrintf(0x2000001, "Failed to InsertInstanceServerEventReward[%d]",
                      pReward->GetRewardID());
            goto failed;
        }
    }
    list.clear();
    return TRUE;

failed:
    // BUG(SM-138): as LoadServerEvents.
    BSAssertFail();
    ServerEvent_FreeRecordList(list, pTable);
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// Delayed jobs

void CServerEventMgr::ProcessDelayJobs()
{
    if (m_heapDelayJob.empty())
        return;
    CDateTime now = CDateTime::GetCurrent();
    size_t nCount = m_heapDelayJob.size();
    for (size_t i = 0; i < nCount; ++i)
    {
        CServerEventDelayJob* pJob = m_heapDelayJob.top();
        if (!pJob)
        {
            LogPrintf(0x2000001, "Not exists DelayJob. but progress next delay job");
            m_heapDelayJob.pop();
            BSAssertFail();
            continue;
        }
        if (!pJob->IsDue(now))
            return;
        if (!pJob->Execute())
        {
            LogPrintf(0x2000001, "Failed to Processing DelayJob[%d]. ServerEventID[%d]",
                      pJob->m_byType, pJob->m_dwServerEventID);
            BSAssertFail();
        }
        pJob->Release();
        m_heapDelayJob.pop();
    }
}

void CServerEventMgr::FlushPendingDelayJobs()
{
    if (m_vecPendingDelayJob.empty())
        return;
    for (std::vector<CServerEventDelayJob*>::iterator it = m_vecPendingDelayJob.begin();
         it != m_vecPendingDelayJob.end(); ++it)
    {
        m_heapDelayJob.push(*it);
    }
    m_vecPendingDelayJob.erase(m_vecPendingDelayJob.begin(), m_vecPendingDelayJob.end());
}

void CServerEventMgr::PushDelayJob(CServerEventDelayJob* pJob, BOOL bPending)
{
    if (bPending == TRUE)
        m_vecPendingDelayJob.push_back(pJob);
    else
        m_heapDelayJob.push(pJob);
}

BOOL CServerEventMgr::AppendDelayJobForCompletionAndReward(CServerEvent* pEvent, DWORD dwUserJID)
{
    RefServerEvent* pRef = pEvent->m_pRef;
    BYTE byTarget = pRef->b50;
    if (byTarget == SERVER_EVENT_TARGET_USER)
    {
        if (!dwUserJID)
        {
            LogPrintf(0x2000001,
                      "Abnormal UserJID!! Failed to completion_N_Reward delayjob!! "
                      "ServerEventId[%d]", pEvent->GetID());
            BSAssertFail();
            return FALSE;
        }
    }
    else if ((BYTE)(byTarget - 3) <= 1)
    {
        BSAssertFail();
    }
    CServerEventDelayJob_CompletionAndReward* pJob =
        CServerEventDelayJob_CompletionAndReward::Alloc();
    if (!pJob)
    {
        BSAssertFail();
        return FALSE;
    }
    pJob->Init((LONG)pRef->dw54, pEvent->GetID(), pRef->b50, dwUserJID);
    m_heapDelayJob.push(pJob);
    return TRUE;
}

BOOL CServerEventMgr::AppendDelayJobForRollbackReward(DWORD dwServerEventID, LONG lDelay,
                                                      DWORD dwRewardID, BOOL bPending)
{
    CServerEventDelayJob_RollbackReward* pJob = CServerEventDelayJob_RollbackReward::Alloc();
    if (!pJob)
    {
        BSAssertFail();
        return FALSE;
    }
    pJob->Init(lDelay, dwServerEventID, dwRewardID);
    PushDelayJob(pJob, bPending);
    return TRUE;
}

BOOL CServerEventMgr::AppendDelayJobForCheckEndServerEvent(CServerEvent* pEvent, BOOL bPending)
{
    CServerEventDelayJob_CheckEndServerEvent* pJob =
        CServerEventDelayJob_CheckEndServerEvent::Alloc();
    RefServerEvent* pRef;
    if (!pJob || (pRef = pEvent->m_pRef) == NULL)
    {
        BSAssertFail();
        return FALSE;
    }
    int nDelay = ServerEvent_SecondsUntil(pRef->tsEnd);
    pJob->Init(nDelay, pEvent->GetID());
    PushDelayJob(pJob, bPending);
    return TRUE;
}

BOOL CServerEventMgr::AppendDelayJobForWaitRegisterServerEvent(RefServerEvent* pRef,
                                                               CInstanceServerEvent* pRecord)
{
    CServerEvent* pEvent;
    CServerEventDelayJob_WaitRegisterServerEvent* pJob;
    if (!pRecord || !pRef || (pEvent = CServerEvent::PoolAlloc()) == NULL ||
        !pEvent->Attach(pRef, pRecord) ||
        (pJob = CServerEventDelayJob_WaitRegisterServerEvent::Alloc()) == NULL)
    {
        BSAssertFail();
        return FALSE;
    }
    int nDelay = ServerEvent_SecondsUntil(pRef->tsBegin);
    pJob->Init(pEvent, pRecord->ID, nDelay);
    m_heapDelayJob.push(pJob);
    return TRUE;
}

BOOL CServerEventMgr::ProcessingDelayJob_CompletionAndReward(DWORD dwServerEventID,
                                                             BYTE byRewardTarget,
                                                             DWORD dwUserJID)
{
    CServerEvent* pEvent = FindServerEvent(dwServerEventID);
    if (!pEvent)
    {
        LogPrintf(0x2000001,
                  "Not found ServerEvent[%d]!! Failed to notify completion and reward msg",
                  dwServerEventID);
        BSAssertFail();
        return FALSE;
    }
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        BSAssertFail();
        return FALSE;
    }
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_COMPLETION_AND_REWARD);
    pMsg->Write<DWORD>(dwServerEventID);
    pMsg->Write<BYTE>(byRewardTarget);
    // BUG(SM-135): the error returns below leave the message allocated.
    if (byRewardTarget == SERVER_EVENT_TARGET_USER)
    {
        CUser* pUser = ClientManager_FindUserByJID(dwUserJID);
        if (!pUser)
        {
            LogPrintf(0x2000001,
                      "ProcessingDelayJob_CompletionAndReward()!! Not found User[%d]!!. "
                      "ServerEventID[%d]", dwUserJID, dwServerEventID);
            BSAssertFail();
            return FALSE;
        }
        const char* pszName = User_GetCharName(pUser);
        pMsg->Write<DWORD>(dwUserJID);
        *pMsg << pszName;
    }
    else if (byRewardTarget > 2 && byRewardTarget <= 4)
    {
        LogPrintf(0x2000001,
                  "NotifyCompletionAndRewardMsg()!!! Not supported RewardTarget[%d] of "
                  "ServerEvent[%d]", byRewardTarget, dwServerEventID);
        BSAssertFail();
        return FALSE;
    }
    RefServerEvent* pRef = pEvent->m_pRef;
    if (!pRef)
    {
        LogPrintf(0x2000001, "NotifyCompletionAndRewardMsg()!!!! pRefServerEvent[%d] == NULL",
                  dwServerEventID);
        BSAssertFail();
        return FALSE;
    }
    if (pRef->b58 == 1)
    {
        if (!pRef->b4D)
        {
            int nCondition = pEvent->GetAchievementCondition();
            int nProgress = pEvent->GetProgressCount();
            pMsg->Write<BYTE>(3);
            pMsg->Write<int>(nProgress);
            pMsg->Write<int>(nCondition);
        }
        else
        {
            int nProgress = pEvent->GetProgressCount();
            pMsg->Write<BYTE>(1);
            pMsg->Write<int>(nProgress);
        }
    }
    else
    {
        pMsg->Write<BYTE>(0);
    }
    ServerEvent_Broadcast(pMsg);

    if (pRef->b50 != SERVER_EVENT_TARGET_ALL)
        return TRUE;

    CDateTime now = CDateTime::GetCurrent();
    CReferenceData::RefServerEventRewardIdMap& mmapReward =
        CReferenceData::GetInstance()->m_mmapServerEventRewardID;
    std::pair<CReferenceData::RefServerEventRewardIdMap::iterator,
              CReferenceData::RefServerEventRewardIdMap::iterator> range =
        mmapReward.equal_range(dwServerEventID);
    for (CReferenceData::RefServerEventRewardIdMap::iterator it = range.first;
         it != range.second; ++it)
    {
        DWORD dwRewardID = it->second;
        RefServerEventReward* pRefReward = ServerEvent_GetRefData()->FindServerEventReward(
            dwRewardID);
        if (!pRefReward)
        {
            LogPrintf(0x2000001, "Not found reference reward. ServerEventID[%d], RewardID[%d]",
                      dwServerEventID, dwRewardID);
            BSAssertFail();
            continue;
        }
        BYTE byType = pRefReward->Type;
        if (byType == RefServerEventReward::TYPE_SPAWN_MONSTER)
            continue;
        if (byType != RefServerEventReward::TYPE_EXP_UP_FOR_PLAYERS)
        {
            LogPrintf(0x2000001,
                      "ProcessingDelayJob_CompletionAndReward()!! Not support RewardClass[%d] "
                      "ServerEventID[%d] RewardID[%d]", byType, dwServerEventID, dwRewardID);
            BSAssertFail();
            continue;
        }
        if (FindServerEventReward(dwRewardID))
            continue;
        LONG lDuration = (LONG)static_cast<RefServerEventReward_ExpUPForPlayers*>(pRefReward)->dw18;
        CServerEventReward* pReward = AllocNewServerEventReward(dwServerEventID, dwRewardID,
                                                                lDuration);
        if (!pReward)
        {
            LogPrintf(0x2000001, "Failed to AllocNewServerEventReward[%d]. ServerEventID[%d]",
                      dwRewardID, dwServerEventID);
            BSAssertFail();
            continue;
        }
        if (InsertServerEventReward(pReward) == TRUE)
            pReward->RequestInsert(now);
        lDuration = (LONG)static_cast<RefServerEventReward_ExpUPForPlayers*>(pRefReward)->dw18;
        if (!AppendDelayJobForRollbackReward(dwServerEventID, lDuration, dwRewardID, TRUE))
        {
            LogPrintf(0x2000001,
                      "Failed to append delay job for rollback reward[%d]. ServerEventID[%d]",
                      dwRewardID, dwServerEventID);
            BSAssertFail();
        }
    }
    return TRUE;
}

BOOL CServerEventMgr::ProcessingDelayJob_CheckEndServerEvent(DWORD dwServerEventID)
{
    CServerEvent* pEvent = FindServerEvent(dwServerEventID);
    if (!pEvent)
    {
        LogPrintf(0x2000001,
                  "Failed to append end sever event log!! Why not found ServerEvent[%d]",
                  dwServerEventID);
    }
    // BUG(SM-141): a missing event is handed to the log as NULL.
    if (!ServerEventLog_LogCompletion(pEvent))
    {
        LogPrintf(0x2000001, "Failed to append end sever event log!! ServerEventID[%d]",
                  dwServerEventID);
    }
    if (!DeleteServerEvent(dwServerEventID))
    {
        LogPrintf(0x2000001, "Failed to DeleteServerEvent[%d]", dwServerEventID);
        BSAssertFail();
        return FALSE;
    }
    LogPrintf(0x2000001, "End ServerEvent[%d]!!!. delete server event", dwServerEventID);
    return TRUE;
}

BOOL CServerEventMgr::ProcessingDelayJob_RollbackReward(DWORD dwServerEventID, DWORD dwRewardID)
{
    if (!DeleteServerEventReward(dwRewardID))
        return FALSE;
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        BSAssertFail();
        return FALSE;
    }
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_ROLLBACK_REWARD);
    pMsg->Write<DWORD>(dwServerEventID);
    pMsg->Write<DWORD>(dwRewardID);
    ServerEvent_Broadcast(pMsg);
    return TRUE;
}

BOOL CServerEventMgr::ProcessingDelayJob_WaitRegisterServerEvent(CServerEvent* pEvent)
{
    if (!pEvent)
    {
        BSAssertFail();
        return FALSE;
    }
    RefServerEvent* pRef = pEvent->m_pRef;
    if (!pRef)
    {
        BSAssertFail();
        return FALSE;
    }
    if (!InsertServerEvent(pEvent, pRef))
    {
        LogPrintf(0x2000001, "Failed to InsertServerEvent[%d]", pEvent->GetID());
        BSAssertFail();
        return FALSE;
    }
    if (!AppendDelayJobForCheckEndServerEvent(pEvent, TRUE))
    {
        LogPrintf(0x2000001, "Failed to AppendDelayJobForCheckEndServerEvent[%d]",
                  pEvent->GetID());
        BSAssertFail();
        return FALSE;
    }
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        BSAssertFail();
        return FALSE;
    }
    int nID = pEvent->GetID();
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_REGISTER);
    pMsg->Write<int>(nID);
    pMsg->Write<BYTE>(pRef->b58);
    if (pRef->b58 == 1)
    {
        int nCondition = pEvent->GetAchievementCondition();
        pMsg->Write<int>(nCondition);
    }
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
    if (!ServerEventLog_LogDetecting(pEvent))
        LogPrintf(0x2000001, "Failed to append start ServerEvent[%d] Log", pEvent->GetID());
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Containers

BOOL CServerEventMgr::InsertServerEvent(CServerEvent* pEvent, RefServerEvent* pRef)
{
    if (!pEvent)
        return FALSE;
    if (FindServerEvent(pEvent->GetID()))
        return FALSE;
    int nID = pEvent->GetID();
    m_mmapDetectingTarget.insert(DetectingTargetMap::value_type(pRef->dw0C, nID));
    m_mapServerEvent.insert(ServerEventMap::value_type(pEvent->GetID(), pEvent));
    return TRUE;
}

CServerEvent* CServerEventMgr::FindServerEvent(DWORD dwServerEventID)
{
    ServerEventMap::iterator it = m_mapServerEvent.find(dwServerEventID);
    if (it == m_mapServerEvent.end())
        return NULL;
    return it->second;
}

BOOL CServerEventMgr::DeleteServerEvent(DWORD dwServerEventID)
{
    ServerEventMap::iterator it = m_mapServerEvent.find(dwServerEventID);
    if (it == m_mapServerEvent.end())
        return FALSE;
    CServerEvent* pEvent = it->second;
    if (!pEvent)
        return FALSE;
    m_mapServerEvent.erase(it);
    // BUG(SM-142): from here the failures return with the event out of the map but neither
    // updated nor released.
    RefServerEvent* pRef = pEvent->m_pRef;
    if (!pRef)
        return FALSE;
    std::pair<DetectingTargetMap::iterator, DetectingTargetMap::iterator> range =
        m_mmapDetectingTarget.equal_range(pRef->dw0C);
    for (DetectingTargetMap::iterator itTarget = range.first; itTarget != range.second;
         ++itTarget)
    {
        if (dwServerEventID == itTarget->second)
        {
            m_mmapDetectingTarget.erase(itTarget);
            break;
        }
    }
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        BSAssertFail();
        return FALSE;
    }
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_END);
    pMsg->Write<DWORD>(dwServerEventID);
    pMsg->Write<BYTE>(pRef->b08);
    if (!pRef->b08)
    {
        pMsg->Write<DWORD>(pRef->dw0C);
        AddFinishedTarget(pRef->dw0C);
    }
    ServerEvent_Broadcast(pMsg);
    pEvent->RequestUpdate();
    pEvent->Release();
    return TRUE;
}

BOOL CServerEventMgr::AddFinishedTarget(DWORD dwTarget)
{
    if (m_setFinishedTarget.find(dwTarget) != m_setFinishedTarget.end())
        return FALSE;
    m_setFinishedTarget.insert(dwTarget);
    return TRUE;
}

BOOL CServerEventMgr::InsertServerEventReward(CServerEventReward* pReward)
{
    if (!pReward)
    {
        BSAssertFail();
        return FALSE;
    }
    if (HasServerEventReward(pReward->GetRewardID()) == TRUE)
        return FALSE;
    m_mapServerEventReward.insert(
        ServerEventRewardMap::value_type(pReward->GetRewardID(), pReward));
    return TRUE;
}

BOOL CServerEventMgr::DeleteServerEventReward(DWORD dwRewardID)
{
    ServerEventRewardMap::iterator it = m_mapServerEventReward.find(dwRewardID);
    if (it == m_mapServerEventReward.end() || !it->second)
    {
        BSAssertFail();
        return FALSE;
    }
    it->second->RequestDelete();
    m_mapServerEventReward.erase(dwRewardID);
    return TRUE;
}

CServerEventReward* CServerEventMgr::FindServerEventReward(DWORD dwRewardID)
{
    ServerEventRewardMap::iterator it = m_mapServerEventReward.find(dwRewardID);
    if (it == m_mapServerEventReward.end())
        return NULL;
    return it->second;
}

BOOL CServerEventMgr::HasServerEventReward(DWORD dwRewardID)
{
    return m_mapServerEventReward.find(dwRewardID) != m_mapServerEventReward.end();
}

CServerEventReward* CServerEventMgr::AllocNewServerEventReward(DWORD dwServerEventID,
                                                               DWORD dwRewardID,
                                                               int nRemainRewardTime)
{
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTable = ShardDB_GetTables(g_pShardDB);
    if (vecTable.empty() || vecTable.size() <= SHARD_TABLE_SERVER_EVENT_REWARD)
    {
        BSAssertFail();
        BSAssertFail();
        return NULL;
    }
    CDBTable* pTable = vecTable[SHARD_TABLE_SERVER_EVENT_REWARD];
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }
    // BUG(SM-143): the failures below leave the new record and the reward allocated.
    CInstanceServerEventReward* pRecord =
        static_cast<CInstanceServerEventReward*>(pTable->CreateRecord());
    if (!pRecord)
    {
        BSAssertFail();
        return NULL;
    }
    CServerEventReward* pReward = CServerEventReward::PoolAlloc();
    if (!pReward)
    {
        BSAssertFail();
        return NULL;
    }
    RefServerEventReward* pRef = ServerEvent_GetRefData()->FindServerEventReward(dwRewardID);
    if (!pRef)
    {
        BSAssertFail();
        return NULL;
    }
    // 0x525b30 / 0x525b50 / 0x525b70: the key columns mark no update group, the remaining time
    // group 2.
    DBRecord_SetColumn(pRecord, CInstanceServerEventReward::s_TableInfo, pRecord->ServerEventID,
                       (int)dwServerEventID, -1);
    DBRecord_SetColumn(pRecord, CInstanceServerEventReward::s_TableInfo, pRecord->RewardID,
                       (int)dwRewardID, -1);
    DBRecord_SetColumn(pRecord, CInstanceServerEventReward::s_TableInfo,
                       pRecord->RemainRewardTime, nRemainRewardTime, 2);
    if (!pReward->Link(pRecord, pRef))
    {
        BSAssertFail();
        return NULL;
    }
    return pReward;
}

// ---------------------------------------------------------------------------------------------
// Messages

BOOL CServerEventMgr::OnDetectingNotify(CMsg* pMsg)
{
    DWORD dwTarget = 0;
    BYTE byType = 0;
    pMsg->Read(dwTarget);
    pMsg->Read(byType);
    if (m_setFinishedTarget.find(dwTarget) != m_setFinishedTarget.end())
    {
        pMsg->SkipRemaining();
        return FALSE;
    }
    DWORD dwValue = 0;
    pMsg->Read(dwValue);
    DWORD dwCount = 1;
    if (byType == 2)
        pMsg->Read(dwCount);
    return ServerEvent_ProcessingEvent(this, dwTarget, byType, dwValue, dwCount);
}

BOOL CServerEventMgr::NotifyCompletionMsg(CServerEvent* pEvent)
{
    RefServerEvent* pRef = pEvent->m_pRef;
    CMsg* pMsg;
    if (!pRef || (pMsg = ServerEvent_AllocMsg()) == NULL)
    {
        BSAssertFail();
        return FALSE;
    }
    int nID = pEvent->GetID();
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_COMPLETION);
    pMsg->Write<int>(nID);
    if (pRef->b58 != 1 || pRef->b4D)
    {
        pMsg->Write<BYTE>(0);
    }
    else
    {
        int nCondition = pEvent->GetAchievementCondition();
        pMsg->Write<BYTE>(2);
        pMsg->Write<int>(nCondition);
    }
    ServerEvent_Broadcast(pMsg);
    return TRUE;
}

void CServerEventMgr::BeginStream()
{
    m_pStreamOwner = &m_Buffer;
    m_Stream.m_nWritePos = m_Buffer.m_nWritePos;
    m_Stream.m_nReadPos = m_Buffer.m_nReadPos;
    m_Stream.m_dw08 = 0;
    m_Stream.m_nCapacity = m_Buffer.m_nCapacity;
    if (m_Buffer.m_nCapacity <= 0)
        BSAssertFail();
    m_Stream.m_pBuffer = m_Buffer.m_pBuffer;
}

void CServerEventMgr::EndStream()
{
    if (m_Stream.m_nReadPos != m_Buffer.m_nReadPos)
        m_Buffer.m_nReadPos = m_Stream.m_nReadPos;
    int nWritePos = m_Stream.m_nWritePos;
    if (nWritePos != m_Buffer.m_nWritePos)
    {
        if (nWritePos < 0 || nWritePos >= m_Buffer.m_nCapacity)
            BSAssertFail();
        m_Buffer.m_nWritePos = nWritePos;
    }
    m_pStreamOwner = NULL;
    m_Stream.m_nWritePos = 0;
    m_Stream.m_nReadPos = 0;
    m_Stream.m_dw08 = 0;
    m_Stream.m_nCapacity = 0;
    m_Stream.m_pBuffer = NULL;
}

void CServerEventMgr::SyncData()
{
    if (!m_mapServerEvent.size())
        return;
    BeginStream();
    DWORD dwCount = 0;
    for (ServerEventMap::iterator it = m_mapServerEvent.begin(); it != m_mapServerEvent.end();
         ++it)
    {
        CServerEvent* pEvent = it->second;
        // BUG(SM-144): these returns leave the stream writer bound to the buffer.
        if (!pEvent)
        {
            ClearBuffer();
            BSAssertFail();
            return;
        }
        RefServerEvent* pRef = pEvent->m_pRef;
        if (!pRef)
        {
            ClearBuffer();
            BSAssertFail();
            return;
        }
        if (pEvent->IsDirty() != TRUE)
            continue;
        if (pRef->b58 == 1)
        {
            ++dwCount;
            int nProgress = pEvent->GetProgressCount();
            m_Stream.Write<int>(pEvent->GetID()).Write<int>(nProgress);
        }
        if (!pEvent->RequestUpdate())
        {
            LogPrintf(0x2000001, "Progressing SyncData!! Failed to UpdateServerEvent[%d]!!",
                      pEvent->GetID());
        }
    }
    EndStream();
    if (!dwCount)
    {
        ClearBuffer();
        return;
    }
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        ClearBuffer();
        BSAssertFail();
        return;
    }
    BYTE abHead[3] = { SERVER_EVENT_MSG_SYNC_PROGRESS, 1, 1 };
    pMsg->WriteBytes(&abHead[0], 1);
    pMsg->WriteBytes(&abHead[1], 1);
    pMsg->WriteBytes(&abHead[2], 1);
    pMsg->WriteBytes(&dwCount, 4);
    pMsg->WriteBytes(m_Buffer.m_pBuffer, (WORD)m_Buffer.m_nWritePos);
    ClearBuffer();
    ServerEvent_Broadcast(pMsg);
}

void CServerEventMgr::SendRewardProgressSync()
{
    if (!m_mapServerEventReward.size())
        return;
    BeginStream();
    DWORD dwCount = 0;
    CDateTime now = CDateTime::GetCurrent();
    for (ServerEventRewardMap::iterator it = m_mapServerEventReward.begin();
         it != m_mapServerEventReward.end(); ++it)
    {
        CServerEventReward* pReward = it->second;
        if (!pReward)
        {
            // BUG(SM-144): as SyncData.
            ClearBuffer();
            BSAssertFail();
            return;
        }
        if (pReward->m_LastUpdate.GetTime() && pReward->UpdateRemainTime(now) == TRUE)
        {
            ++dwCount;
            int nRemain = pReward->GetRemainRewardTime();
            int nRewardID = pReward->GetRewardID();
            m_Stream.Write<int>(pReward->GetServerEventID()).Write<int>(nRewardID)
                .Write<int>(nRemain);
        }
    }
    EndStream();
    if (!dwCount)
    {
        ClearBuffer();
        return;
    }
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        ClearBuffer();
        BSAssertFail();
        return;
    }
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_REWARD_PROGRESS);
    pMsg->WriteBytes(&dwCount, 4);
    pMsg->WriteBytes(m_Buffer.m_pBuffer, (WORD)m_Buffer.m_nWritePos);
    ClearBuffer();
    ServerEvent_Broadcast(pMsg);
}

// Set by the first SendRewardListOnce (byte_8A8964).
static BYTE s_bRewardListSent = 0;

void CServerEventMgr::SendRewardListOnce()
{
    if (!m_mapServerEventReward.size() || s_bRewardListSent == 1)
        return;
    CMsg* pMsg = ServerEvent_AllocMsg();
    if (!pMsg)
    {
        BSAssertFail();
        return;
    }
    pMsg->Write<BYTE>(SERVER_EVENT_MSG_REWARD_LIST_BEGIN);
    ServerEvent_Broadcast(pMsg);
    s_bRewardListSent = 1;
    CDateTime now = CDateTime::GetCurrent();
    for (ServerEventRewardMap::iterator it = m_mapServerEventReward.begin();
         it != m_mapServerEventReward.end(); ++it)
    {
        CServerEventReward* pReward = it->second;
        if (!pReward)
        {
            BSAssertFail();
            return;
        }
        pReward->m_LastUpdate = now;
        int nRewardID = pReward->GetRewardID();
        int nRemain = pReward->GetRemainRewardTime();
        AppendDelayJobForRollbackReward(pReward->GetServerEventID(), nRemain, nRewardID, FALSE);
    }
}
