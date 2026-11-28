#include "ShardManager/ShardMaintainer.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/QueMT.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CServerProcessBase;

enum
{
    SHARD_TABLE_DELETED_CHAR    = 0x10,     // _DeletedChar in the shard DB table list

    MAINTAINER_SUB_REVIVE       = 2,
    MAINTAINER_SUB_DELETED_TEMP = 4,

    MAINTAINER_NOTIFY_DELETED   = 1,
    MAINTAINER_NOTIFY_REVIVED   = 3,

    REVIVE_RESULT_OK            = 0x401,
    REVIVE_RESULT_FAILED        = 0x402,
};

CShardMaintainer* g_pShardMaintainer = NULL;

BS_IMPLEMENT_DYNCREATE(CShardMaintainer, &CServerProcessBase::classCServerProcessBase)

// -------------------------------------------------------------------------------------------
// DeletedCharEntry

DeletedCharEntry::~DeletedCharEntry()
{
    ReleaseRecord();
}

void DeletedCharEntry::SetRecord(CInstanceDeletedChar* pRec)
{
    if (!pRec)
        BSAssertFail();
    pRecord = pRec;
    const TIMESTAMP_STRUCT& ts = pRec->DeletedDate;
    dtDeleted = CDateTime(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
}

void DeletedCharEntry::ReleaseRecord()
{
    if (g_pShardMaintainer && pRecord)
    {
        if (!g_pShardDB)
            BSAssertFail();
        CInstanceDeletedChar* pRec = pRecord;
        g_pShardDB->GetTable(SHARD_TABLE_DELETED_CHAR)->FreeRecord(pRec);
    }
    pRecord = NULL;
}

CTimeSpan ShardMaintainer_GetRemainUntilPermanentDelete(const CDateTime& dtNow,
                                                        const CDateTime& dtDeleted)
{
    static CTimeSpan s_spanKeep(7, 0, 0, 0);
    CDateTime dtDue = dtDeleted + s_spanKeep;
    if (dtNow > dtDue)
        return CTimeSpan(0, 0, 0, 0);
    return dtDue - dtNow;
}

// -------------------------------------------------------------------------------------------
// CShardMaintainer

CShardMaintainer::CShardMaintainer()
{
    if (g_pShardMaintainer)
        BSAssertFail();
    g_pShardMaintainer = this;
}

CShardMaintainer::~CShardMaintainer()
{
    g_pShardMaintainer = NULL;
}

void CShardMaintainer::InitMsgHandlers()
{
    CServerProcessBase::InitMsgHandlers();
    RegisterHandler(SHARD_MAINTAINER_MSG, &CShardMaintainer::OnMsg_380E);
}

DWORD CShardMaintainer::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;

    DWORD dwLastTick = GetTickCount();
    while (!*pbStop)
    {
        CTask* pTask = (CTask*)m_pOwner;
        CMsg* pMsg = NULL;
        if (!pTask->m_pQueue)
            BSAssertFail();
        LONG nCount = pTask->m_pQueue ? pTask->m_pQueue->GetSize() : 0;
        if (nCount)
        {
            int n = 0;
            while (((CTask*)m_pOwner)->m_pQueue->Pop(&pMsg, 1, FALSE) == 1)
            {
                if (pMsg)
                {
                    if (!nIndex)
                        BSAssertFail();
                    pMsg->m_pOwner = (void*)nIndex;
                    Dispatch(pMsg, 0, 0, NULL);
                    ServerFramework::CheckMsgFullyConsumed(pMsg);
                    g_pBSNet->ReleaseMsg(pMsg);
                }
                if (++n >= nCount)
                    break;
            }
        }
        else
        {
            Sleep(1);
        }

        DWORD dwTick = GetTickCount();
        DWORD dwElapsed = dwTick - dwLastTick;
        dwLastTick = dwTick;
        m_Schedule.Update(dwElapsed);
    }
    LogPrintf(0x2000000, "ShardMaintainer Thread Closed!");
    return 0;
}

DeletedCharEntry* CShardMaintainer::AllocEntry()
{
    return m_EntryPool.Alloc();
}

void CShardMaintainer::FreeEntry(DeletedCharEntry* pEntry)
{
    if (!pEntry)
    {
        BSAssertFail();
        return;
    }
    m_EntryPool.Free(pEntry);
}

void CShardMaintainer::LoadDeletedChars()
{
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_TABLE_DELETED_CHAR);
    if (!pTable)
    {
        BSAssertFail();
        return;
    }

    CDBRecordList list;
    if (!pTable->SelectAll(3, &list))
    {
        // CDBObjectList_DetachAndClear
        for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
            pTable->FreeRecord(*it);
        list.clear();
        return;
    }

    m_dtNow = CDateTime::GetCurrent();
    for (CDBRecordList::iterator it = list.begin(); it != list.end(); ++it)
    {
        DeletedCharEntry* pEntry = AllocEntry();
        pEntry->SetRecord((CInstanceDeletedChar*)*it);
        int nBucket = ClassifyRemainTime(pEntry);
        if (nBucket == -1)
            m_listDelete.push_back(pEntry);
        else
            m_aBucket[nBucket].insert(DeletedCharMap::value_type(pEntry->GetCharID(), pEntry));
    }
    FlushDeleteList();

    m_Schedule.Add(60000, this, &CShardMaintainer::ProcessExpiredBucket);
    m_Schedule.Add(3600000, this, &CShardMaintainer::RebucketDayMap);
    m_Schedule.Add(86400000, this, &CShardMaintainer::RebucketHourMap);
}

int CShardMaintainer::ClassifyRemainTime(DeletedCharEntry* pEntry)
{
    CTimeSpan span = ShardMaintainer_GetRemainUntilPermanentDelete(m_dtNow, pEntry->dtDeleted);
    if (!span.GetTotalSeconds())
        return -1;
    if (span.GetTotalHours() > 24)
        return DELETE_BUCKET_LATER;
    return span.GetTotalMinutes() > 60 ? DELETE_BUCKET_DAY : DELETE_BUCKET_HOUR;
}

void CShardMaintainer::ProcessExpiredBucket()
{
    m_dtNow = CDateTime::GetCurrent();
    if (!m_listDelete.empty())
        BSAssertFail();

    DeletedCharMap& bucket = m_aBucket[DELETE_BUCKET_HOUR];
    DeletedCharMap::iterator it = bucket.begin();
    while (it != bucket.end())
    {
        DeletedCharMap::iterator itCur = it++;
        DeletedCharEntry* pEntry = itCur->second;
        CTimeSpan span = ShardMaintainer_GetRemainUntilPermanentDelete(m_dtNow,
                                                                       pEntry->dtDeleted);
        if (span.GetTotalSeconds() <= 1)
        {
            m_listDelete.push_back(pEntry);
            bucket.erase(itCur);
        }
    }
    FlushDeleteList();
}

void CShardMaintainer::RebucketDayMap()
{
    m_dtNow = CDateTime::GetCurrent();
    DeletedCharMap& bucket = m_aBucket[DELETE_BUCKET_DAY];
    DeletedCharMap::iterator it = bucket.begin();
    while (it != bucket.end())
    {
        DeletedCharMap::iterator itCur = it++;
        DeletedCharEntry* pEntry = itCur->second;
        CTimeSpan span = ShardMaintainer_GetRemainUntilPermanentDelete(m_dtNow,
                                                                       pEntry->dtDeleted);
        if (span.GetTotalMinutes() < 60)
        {
            m_aBucket[DELETE_BUCKET_HOUR].insert(
                DeletedCharMap::value_type(pEntry->GetCharID(), pEntry));
            bucket.erase(itCur);
        }
    }
}

void CShardMaintainer::RebucketHourMap()
{
    m_dtNow = CDateTime::GetCurrent();
    DeletedCharMap& bucket = m_aBucket[DELETE_BUCKET_LATER];
    DeletedCharMap::iterator it = bucket.begin();
    while (it != bucket.end())
    {
        DeletedCharMap::iterator itCur = it++;
        DeletedCharEntry* pEntry = itCur->second;
        CTimeSpan span = ShardMaintainer_GetRemainUntilPermanentDelete(m_dtNow,
                                                                       pEntry->dtDeleted);
        if (span.GetTotalHours() < 24)
        {
            m_aBucket[DELETE_BUCKET_DAY].insert(
                DeletedCharMap::value_type(pEntry->GetCharID(), pEntry));
            bucket.erase(itCur);
        }
    }
}

void CShardMaintainer::FlushDeleteList()
{
    for (std::list<DeletedCharEntry*>::iterator it = m_listDelete.begin();
         it != m_listDelete.end(); ++it)
    {
        DeletedCharEntry* pEntry = *it;
        int nErrorCode = 0;
        if (DeleteCharPermanently(&nErrorCode, pEntry) == 1)
            SendCharDeletedNotify(pEntry, nErrorCode);
        pEntry->ReleaseRecord();
        FreeEntry(pEntry);
    }
    m_listDelete.clear();
}

int CShardMaintainer::DeleteCharPermanently(int* pnErrorCode, DeletedCharEntry* pEntry)
{
    *pnErrorCode = 0;
    std::string strName = DB_Select_Char(pEntry->GetCharID());
    pEntry->strCharName.assign(strName, 0, std::string::npos);

    char szQuery[0xC00];
    if (FAILED(StringCchPrintfA(szQuery, 0xC00, "{?=CALL _DeleteCharPermanently (%d)}",
                                pEntry->GetCharID())))
    {
        BSAssertFail();
        return 0;
    }

    int anReturn[2] = { 0, 0 };
    if (!g_pShardDB)
        BSAssertFail();
    int nResult = CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), 2,
                                                       szQuery, 3, anReturn);
    if (nResult & ~1)
        return 0;

    int nCode = anReturn[0];
    if (nCode < 0)
    {
        LogPrintf(0x2000001,
                  "failed to permanent removing of character ! [JID: %d, CharID: %d, "
                  "ErrorCode: %d]",
                  pEntry->GetJID(), pEntry->GetCharID(), nCode);
        return 0;
    }
    LogPrintf(0, "character deleted permanently! [JID: %d, CharID: %d, ErrorCode: %d]",
              pEntry->GetJID(), pEntry->GetCharID(), nCode);
    *pnErrorCode = anReturn[0];
    DB_AddLogChar(pEntry->GetCharID(), pEntry->GetJID(), 0, 3, pEntry->GetCharID());
    return 1;
}

void CShardMaintainer::SendCharDeletedNotify(DeletedCharEntry* pEntry, int nErrorCode)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CTask* pTask = (CTask*)g_pMainProcess->GetOwner();
    if (!pTask)
        return;

    std::string strName;
    strName.assign(pEntry->strCharName, 0, std::string::npos);
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(SHARD_MAINTAINER_MSG);
    *pMsg << (WORD)MAINTAINER_NOTIFY_DELETED;
    *pMsg << (DWORD)pEntry->GetJID();
    *pMsg << (DWORD)pEntry->GetCharID();
    *pMsg << (DWORD)nErrorCode;
    *pMsg << strName.c_str();
    pTask->m_pQueue->Push(pMsg);
}

void CShardMaintainer::OnMsg_380E(CMsg* pMsg, DWORD, DWORD, ServerFramework::CMassiveMsg*)
{
    WORD wSub = pMsg->ReadForwardOnly<WORD>();
    if (wSub == MAINTAINER_SUB_REVIVE)
        OnReviveChar(pMsg);
    else if (wSub == MAINTAINER_SUB_DELETED_TEMP)
        OnCharDeletedTemp(pMsg);
}

void CShardMaintainer::OnReviveChar(CMsg* pMsg)
{
    DWORD dwJID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwCharID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwData = pMsg->ReadForwardOnly<DWORD>();
    RemoveFromDeleteBuckets((int)dwCharID);

    char szQuery[0xC00];
    if (FAILED(StringCchPrintfA(szQuery, 0xC00, "{?=CALL _ReviveChar (%d)}", dwCharID)))
        BSAssertFail();

    WORD wCode = REVIVE_RESULT_OK;
    int nReturn = 0;
    if (!g_pShardDB)
        BSAssertFail();
    int nResult = CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), 2,
                                                       szQuery, 3, &nReturn);
    // BUG(SM-302): a failed statement leaves the code at "restored".
    if ((nResult & ~1) == 0)
    {
        if (nReturn > 0)
        {
            LogPrintf(0, "successfully character restored!!! [JID: %d, CharID: %d, ErrorCode: %d]",
                      dwJID, dwCharID, nReturn);
            DB_AddLogChar((int)dwCharID, (int)dwJID, 0, 7, (int)dwData);
        }
        else
        {
            LogPrintf(0x2000001,
                      "failed to restore temporarily deleted character !!! [JID: %d, CharID: %d, "
                      "ErrorCode: %d]",
                      dwJID, dwCharID, nReturn);
            wCode = REVIVE_RESULT_FAILED;
        }
    }

    if (!g_pMainProcess)
        BSAssertFail();
    CTask* pTask = (CTask*)g_pMainProcess->GetOwner();
    if (!pTask)
        return;

    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(SHARD_MAINTAINER_MSG);
    *pReply << (WORD)MAINTAINER_NOTIFY_REVIVED;
    *pReply << dwJID;
    *pReply << dwCharID;
    if (wCode == REVIVE_RESULT_OK)
    {
        *pReply << (BYTE)1;
    }
    else
    {
        *pReply << (BYTE)2;
        *pReply << wCode;
    }
    pTask->m_pQueue->Push(pReply);
}

void CShardMaintainer::RemoveFromDeleteBuckets(int nCharID)
{
    int nBucket = 0;
    DeletedCharMap::iterator it;
    for (;;)
    {
        // BUG(SM-303): lower_bound without a key check takes the next character of the bucket
        // when the character is not in it.
        it = m_aBucket[nBucket].lower_bound(nCharID);
        if (it != m_aBucket[nBucket].end())
            break;
        if (++nBucket >= DELETE_BUCKET_COUNT)
            return;
    }

    DeletedCharEntry* pEntry = it->second;
    pEntry->ReleaseRecord();
    FreeEntry(pEntry);
    m_aBucket[nBucket].erase(it);
}

void CShardMaintainer::OnCharDeletedTemp(CMsg* pMsg)
{
    CInstanceDeletedChar* pRecord = (CInstanceDeletedChar*)pMsg->ReadForwardOnly<DWORD>();
    DeletedCharEntry* pEntry = AllocEntry();
    pEntry->SetRecord(pRecord);
    m_aBucket[DELETE_BUCKET_LATER].insert(
        DeletedCharMap::value_type(pEntry->GetCharID(), pEntry));
}
