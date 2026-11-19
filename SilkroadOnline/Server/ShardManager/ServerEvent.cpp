#include "ShardManager/ServerEvent.h"

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ServerEventExtern.h"
#include "ShardManager/ServerEventMgr.h"
#include "ShardManager/ShardWorldExtern.h"

// Table indices of the shard DB.
enum
{
    SHARD_TABLE_SERVER_EVENT = 0x35,
    SHARD_TABLE_SERVER_EVENT_REWARD = 0x36,
};

// ---------------------------------------------------------------------------------------------
// Pools

SERVER_EVENT_POOL_IMPLEMENT(CServerEvent)
SERVER_EVENT_POOL_IMPLEMENT(CServerEventReward)
SERVER_EVENT_POOL_IMPLEMENT(CServerEventDelayJob_CompletionAndReward)
SERVER_EVENT_POOL_IMPLEMENT(CServerEventDelayJob_RollbackReward)
SERVER_EVENT_POOL_IMPLEMENT(CServerEventDelayJob_CheckEndServerEvent)
SERVER_EVENT_POOL_IMPLEMENT(CServerEventDelayJob_WaitRegisterServerEvent)

void ServerEventPool_LogDoubleRelease(const char* pszName)
{
    LogPrintf(0x2000000, "why do you about to delete already deleted !!! [%s]", pszName);
    BSAssertFail();
}

// Posts an AQ_ServerEvent prepared by the caller to the DB thread of the main process.
static void ServerEvent_PostJob(AQ_ServerEvent* pJob)
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!g_pMainProcess)
        BSAssertFail();
    MainProcess_PostDBJob(pJob);
}

// The table of nIndex after the inlined size check of the callers; NULL (assert) when the
// table list is too short.
static BOOL ServerEvent_GetTable(int nIndex, CDBTable** ppTable)
{
    std::vector<CDBTable*>& vecTable = ShardDB_GetTables(g_pShardDB);
    if (vecTable.empty() || vecTable.size() <= (size_t)nIndex)
        return FALSE;
    *ppTable = vecTable[nIndex];
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CServerEvent

CServerEvent::CServerEvent()
    : m_pRecord(NULL), m_pRef(NULL), m_nSyncedProgress(-1), m_nSyncedCompletion(-1)
{
}

void CServerEvent::Release()
{
    ServerEventPool_Release(this);
}

BOOL CServerEvent::Attach(RefServerEvent* pRef, CInstanceServerEvent* pRecord)
{
    // BUG(SM-139): ResetAndDetach does not clear m_pRecord, so a CServerEvent that went back to
    // the pool is refused here when the pool hands it out again.
    if (!pRecord || !pRef || m_pRecord || m_pRef)
    {
        BSAssertFail();
        return FALSE;
    }
    m_pRecord = pRecord;
    m_pRef = pRef;
    m_nSyncedCompletion = pRecord->CompletionValue;
    m_nSyncedProgress = pRecord->ProgressCount;
    return TRUE;
}

BOOL CServerEvent::ResetAndDetach()
{
    m_nSyncedProgress = -1;
    m_nSyncedCompletion = -1;
    m_pRef = NULL;
    if (!g_pShardDB)
    {
        m_pRecord->InitData();
        return TRUE;
    }
    CDBTable* pTable;
    if (!ServerEvent_GetTable(SHARD_TABLE_SERVER_EVENT, &pTable))
    {
        BSAssertFail();
        m_pRecord->InitData();
        BSAssertFail();
        return FALSE;
    }
    if (!pTable)
    {
        m_pRecord->InitData();
        BSAssertFail();
        return FALSE;
    }
    if (m_pRecord)
    {
        // BUG(SM-140): the record is used (InitData, vftable slot 5) after FreeRecord gave it
        // back to its allocator.
        pTable->FreeRecord(m_pRecord);
        m_pRecord->InitData();
    }
    return TRUE;
}

BOOL CServerEvent::RequestUpdate()
{
    m_nSyncedCompletion = GetCompletionValue();
    m_nSyncedProgress = GetProgressCount();
    AQ_ServerEvent* pJob = AQ_ServerEvent_Alloc();
    if (!pJob)
    {
        LogPrintf(0x2000001, "Failed to UpdateServerEvent[%d]", GetID());
        BSAssertFail();
        return FALSE;
    }
    if (!AQ_ServerEvent_InitUpdate(pJob, this))
    {
        AQ_ServerEvent_Free(pJob);
        return FALSE;
    }
    ServerEvent_PostJob(pJob);
    return TRUE;
}

BOOL CServerEvent::IsDirty()
{
    if (GetProgressCount() != m_nSyncedProgress)
        return TRUE;
    return GetCompletionValue() != m_nSyncedCompletion;
}

void CServerEvent::ApplyCompletionCount()
{
    AddCompletionCount((DWORD)GetProgressCount() / (DWORD)GetAchievementCondition());
    int nRest = (int)((DWORD)GetProgressCount() % (DWORD)GetAchievementCondition());
    if (!m_pRecord)
    {
        BSAssertFail();
        return;
    }
    DBRecord_SetColumn(m_pRecord, CInstanceServerEvent::s_TableInfo, m_pRecord->ProgressCount,
                       nRest, 2);
}

int CServerEvent::SetRequiredValue(int nCount)
{
    if (!m_pRecord)
    {
        BSAssertFail();
        return -1;
    }
    int nValue = nCount * (short)m_pRef->w4E + 100;
    DBRecord_SetColumn(m_pRecord, CInstanceServerEvent::s_TableInfo,
                       m_pRecord->AchievementCondition, nValue, 2);
    return m_pRecord->AchievementCondition;
}

int CServerEvent::AddCurrentValue(int nValue)
{
    if (m_pRecord)
    {
        DBRecord_SetColumn(m_pRecord, CInstanceServerEvent::s_TableInfo,
                           m_pRecord->ProgressCount, nValue + m_pRecord->ProgressCount, 2);
        if (m_pRecord)
            return m_pRecord->ProgressCount;
    }
    BSAssertFail();
    return -1;
}

int CServerEvent::AddCompletionCount(int nCount)
{
    if (m_pRecord)
    {
        DBRecord_SetColumn(m_pRecord, CInstanceServerEvent::s_TableInfo,
                           m_pRecord->CompletionValue, nCount + m_pRecord->CompletionValue, 2);
        if (m_pRecord)
            return m_pRecord->CompletionValue;
    }
    BSAssertFail();
    return -1;
}

// ---------------------------------------------------------------------------------------------
// CServerEventReward

CServerEventReward::CServerEventReward()
    : m_pRecord(NULL), m_pRef(NULL)
{
    m_LastUpdate = (__time32_t)0;
}

void CServerEventReward::Release()
{
    ServerEventPool_Release(this);
}

BOOL CServerEventReward::Link(CInstanceServerEventReward* pRecord, RefServerEventReward* pRef)
{
    if (!pRecord || !pRef)
    {
        BSAssertFail();
        return FALSE;
    }
    m_pRecord = pRecord;
    m_pRef = pRef;
    return TRUE;
}

BOOL CServerEventReward::ResetAndDetach()
{
    m_LastUpdate = (__time32_t)0;
    m_pRef = NULL;
    if (!g_pShardDB)
    {
        m_pRecord->InitData();
        return TRUE;
    }
    CDBTable* pTable;
    if (!ServerEvent_GetTable(SHARD_TABLE_SERVER_EVENT_REWARD, &pTable))
    {
        BSAssertFail();
        m_pRecord->InitData();
        BSAssertFail();
        return FALSE;
    }
    if (!pTable)
    {
        m_pRecord->InitData();
        BSAssertFail();
        return FALSE;
    }
    if (m_pRecord)
    {
        // BUG(SM-140): as CServerEvent::ResetAndDetach.
        pTable->FreeRecord(m_pRecord);
        m_pRecord->InitData();
    }
    return TRUE;
}

BOOL CServerEventReward::PostJob(int nKind)
{
    AQ_ServerEvent* pJob = AQ_ServerEvent_Alloc();
    if (!pJob)
    {
        int nServerEventID = GetServerEventID();
        int nRewardID = GetRewardID();
        if (nKind == 2)
        {
            LogPrintf(0x2000001, "Failed to InsertServerEventReward[%d] ServerEventID[%d]",
                      nRewardID, nServerEventID);
        }
        else
        {
            // BUG(SM-136): the update of the remaining time logs a failed delete.
            LogPrintf(0x2000001, "Failed to DeleteServerEventReward[%d] ServerEventID[%d]",
                      nRewardID, nServerEventID);
        }
        BSAssertFail();
        return FALSE;
    }
    if (!AQ_ServerEvent_InitReward(pJob, this, nKind))
    {
        AQ_ServerEvent_Free(pJob);
        return FALSE;
    }
    ServerEvent_PostJob(pJob);
    return TRUE;
}

BOOL CServerEventReward::RequestInsert(const CDateTime& now)
{
    m_LastUpdate = now;
    return PostJob(2);
}

BOOL CServerEventReward::RequestDelete()
{
    return PostJob(3);
}

BOOL CServerEventReward::UpdateRemainTime(const CDateTime& now)
{
    CTimeSpan elapsed = now - m_LastUpdate;
    int nRemain = GetRemainRewardTime() - elapsed.GetTotalSeconds();
    if (m_pRecord)
    {
        DBRecord_SetColumn(m_pRecord, CInstanceServerEventReward::s_TableInfo,
                           m_pRecord->RemainRewardTime, nRemain, 2);
    }
    else
    {
        BSAssertFail();
    }
    m_LastUpdate = now;
    return PostJob(4);
}

// ---------------------------------------------------------------------------------------------
// Delayed jobs

CServerEventDelayJob::CServerEventDelayJob()
    : m_byType(TYPE_NONE), m_dwServerEventID((DWORD)-1), m_Delay(0, 0, 0, 0)
{
}

void CServerEventDelayJob::Reset()
{
    m_byType = TYPE_NONE;
    m_dwServerEventID = (DWORD)-1;
    // The original copies default-constructed (uninitialised) objects here.
    m_Delay = CTimeSpan();
    m_Start = CDateTime();
}

BOOL CServerEventDelayJob::IsDue(const CDateTime& now)
{
    return !(now < m_Start + m_Delay);
}

void CServerEventDelayJob::Init(BYTE byType, LONG lDelay, DWORD dwServerEventID)
{
    m_byType = byType;
    m_dwServerEventID = dwServerEventID;
    m_Delay = CTimeSpan(0, 0, 0, lDelay);
    m_Start = CDateTime::GetCurrent();
}

static CServerEventMgr* ServerEvent_GetMgr()
{
    if (!g_pServerEventMgr)
        BSAssertFail();
    return g_pServerEventMgr;
}

// CompletionAndReward

CServerEventDelayJob_CompletionAndReward::CServerEventDelayJob_CompletionAndReward()
    : m_byRewardTarget(0xFF), m_dwUserJID((DWORD)-1)
{
}

void CServerEventDelayJob_CompletionAndReward::Reset()
{
    CServerEventDelayJob::Reset();
    m_byRewardTarget = 0xFF;
    m_dwUserJID = (DWORD)-1;
}

BOOL CServerEventDelayJob_CompletionAndReward::Execute()
{
    DWORD dwUserJID = m_dwUserJID;
    DWORD dwServerEventID = m_dwServerEventID;
    BYTE byRewardTarget = m_byRewardTarget;
    return ServerEvent_GetMgr()->ProcessingDelayJob_CompletionAndReward(dwServerEventID,
                                                                        byRewardTarget, dwUserJID);
}

void CServerEventDelayJob_CompletionAndReward::Init(LONG lDelay, DWORD dwServerEventID,
                                                    BYTE byRewardTarget, DWORD dwUserJID)
{
    InitJob(lDelay, dwServerEventID);
    m_byRewardTarget = byRewardTarget;
    m_dwUserJID = dwUserJID;
}

// RollbackReward

CServerEventDelayJob_RollbackReward::CServerEventDelayJob_RollbackReward()
    : m_dwRewardID((DWORD)-1)
{
}

void CServerEventDelayJob_RollbackReward::Reset()
{
    CServerEventDelayJob::Reset();
    m_dwRewardID = (DWORD)-1;
}

BOOL CServerEventDelayJob_RollbackReward::Execute()
{
    DWORD dwRewardID = m_dwRewardID;
    DWORD dwServerEventID = m_dwServerEventID;
    return ServerEvent_GetMgr()->ProcessingDelayJob_RollbackReward(dwServerEventID, dwRewardID);
}

void CServerEventDelayJob_RollbackReward::Init(LONG lDelay, DWORD dwServerEventID,
                                               DWORD dwRewardID)
{
    InitJob(lDelay, dwServerEventID);
    m_dwRewardID = dwRewardID;
}

// CheckEndServerEvent

CServerEventDelayJob_CheckEndServerEvent::CServerEventDelayJob_CheckEndServerEvent()
{
}

BOOL CServerEventDelayJob_CheckEndServerEvent::Execute()
{
    DWORD dwServerEventID = m_dwServerEventID;
    return ServerEvent_GetMgr()->ProcessingDelayJob_CheckEndServerEvent(dwServerEventID);
}

// WaitRegisterServerEvent

CServerEventDelayJob_WaitRegisterServerEvent::CServerEventDelayJob_WaitRegisterServerEvent()
{
}

void CServerEventDelayJob_WaitRegisterServerEvent::Reset()
{
    CServerEventDelayJob::Reset();
    m_pServerEvent = NULL;
}

BOOL CServerEventDelayJob_WaitRegisterServerEvent::Execute()
{
    CServerEvent* pEvent = m_pServerEvent;
    return ServerEvent_GetMgr()->ProcessingDelayJob_WaitRegisterServerEvent(pEvent);
}

void CServerEventDelayJob_WaitRegisterServerEvent::Init(CServerEvent* pEvent,
                                                        DWORD dwServerEventID, LONG lDelay)
{
    if (!pEvent)
    {
        BSAssertFail();
        return;
    }
    InitJob(lDelay, dwServerEventID);
    m_pServerEvent = pEvent;
}
