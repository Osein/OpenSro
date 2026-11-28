#include "ShardManager/OneByOneProcessor.h"

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/QueMT.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerFramework.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CServerProcessBase;

enum
{
    ONE_BY_ONE_MSG_REQUEST      = 0x791F,
    ONE_BY_ONE_MSG_RESULT       = 0xB91F,
    SILK_HISTORY_QUERY          = 0x631A,

    CASH_OP_REGISTER            = 0,
    CASH_OP_CANCEL              = 0xFF,

    SILK_QUERY_RELAY_FAILED     = 0xA1,
};

COneByOneProcessor* g_pOneByOneProcessor = NULL;
CChunkAllocatorST<JOB_DATA> g_JobDataPool;
CChunkAllocatorST<sSilkHistoryData> g_SilkHistoryDataPool;

void sSilkHistoryData_Write(const sSilkHistoryData* pData, CMsg* pMsg)
{
    const BYTE* p = (const BYTE*)pData;
    pMsg->WriteBytes(p + 4, 4);
    pMsg->WriteBytes(p + 8, 4);
    pMsg->WriteBytes(p + 12, 4);
    pMsg->WriteBytes(p + 16, 1);
    pMsg->WriteBytes(p + 17, 1);
}

unsigned __int64 OneByOne_GetPerfMs()
{
    static LARGE_INTEGER s_liFrequency;
    static bool s_bFrequency = false;
    if (!s_bFrequency)
    {
        s_bFrequency = true;
        QueryPerformanceFrequency(&s_liFrequency);
    }
    LARGE_INTEGER liCount;
    QueryPerformanceCounter(&liCount);
    return (unsigned __int64)((double)liCount.QuadPart / (double)s_liFrequency.QuadPart * 1000.0);
}

JOB_DATA::JOB_DATA(const unsigned __int64& qwKey, sSilkHistoryData* pParam)
{
    m_qwKey = 0;
    m_nType = ONE_BY_ONE_JOB_SILK_HISTORY;
    m_qwKey = qwKey;
    m_dw18 = 0;
    m_pParam = pParam;
    m_qwStartMs = OneByOne_GetPerfMs();
}

// -------------------------------------------------------------------------------------------

COneByOneProcessor::~COneByOneProcessor()
{
    g_pOneByOneProcessor = NULL;
}

void COneByOneProcessor::OnMsg_791F(CMsg* pMsg, DWORD, DWORD, ServerFramework::CMassiveMsg*)
{
    DWORD dwType;
    *pMsg >> dwType;
    if (dwType == 1)
        OnCashRequest(pMsg);
}

void COneByOneProcessor::OnCashRequest(CMsg* pMsg)
{
    BYTE byOperation = 0;
    DWORD dwJID = 0;
    unsigned __int64 qwKey = 0;
    BYTE by10 = 0;
    BYTE by11 = 0;
    *pMsg >> byOperation;
    // CMsg::Read_GM_SILK_HISTORY_REQ
    *pMsg >> dwJID >> qwKey >> by10 >> by11;

    if (byOperation == CASH_OP_REGISTER)
    {
        sSilkHistoryData* pData = g_SilkHistoryDataPool.Alloc();
        pData->dwJID = dwJID;
        pData->by10 = by10;
        pData->qwKey = qwKey;
        pData->by11 = by11;
        unsigned __int64 qwJobKey = qwKey;
        RegisterJob(qwJobKey, pData);
    }
    else if (byOperation == CASH_OP_CANCEL)
    {
        unsigned __int64 qwJobKey = qwKey;
        CompleteJob(qwJobKey);
    }
    else
    {
        LogPrintf(0x2000000, "UnRegistered Operation was detected!! (%d)", byOperation);
    }
}

void COneByOneProcessor::OnCashResult_B91F(CMsg* pMsg)
{
    BYTE byResult;
    unsigned __int64 qwKey = 0;
    *pMsg >> byResult;
    *pMsg >> qwKey;
    CompleteJob(qwKey);
    m_bBusy = FALSE;
}

void COneByOneProcessor::CheckJobTimeouts()
{
    std::map<unsigned __int64, JOB_DATA*>::iterator it = m_mapJob.begin();
    while (it != m_mapJob.end())
    {
        JOB_DATA* pJob = it->second;
        unsigned __int64 qwAge = OneByOne_GetPerfMs() - pJob->m_qwStartMs;
        if (qwAge > ONE_BY_ONE_JOB_TIMEOUT)
        {
            // BUG(SM-304): the job's node is erased here and the loop then steps from it.
            CompleteJob(pJob->m_qwKey);
        }
        ++it;
    }
}

void COneByOneProcessor::RegisterJob(const unsigned __int64& qwKey, sSilkHistoryData* pParam)
{
    if (m_mapJob.find(qwKey) != m_mapJob.end())
    {
        // BUG(SM-306): the parameter of a repeated key is neither stored nor freed.
        return;
    }
    JOB_DATA* pJob = g_JobDataPool.Alloc();
    *pJob = JOB_DATA(qwKey, pParam);
    m_mapJob.insert(std::map<unsigned __int64, JOB_DATA*>::value_type(qwKey, pJob));
    m_deqPending.push_back(qwKey);
}

void COneByOneProcessor::DispatchJob(const unsigned __int64& qwKey)
{
    // An unknown key dereferences end() (the checked iterator throws).
    JOB_DATA* pJob = m_mapJob.find(qwKey)->second;
    const JOB_HANDLER& h = m_aStart[pJob->m_nType];
    (((COneByOneProcessor*)((BYTE*)this + h.nThisAdjust))->*h.pfn)(pJob);
}

void COneByOneProcessor::CompleteJob(const unsigned __int64& qwKey)
{
    std::map<unsigned __int64, JOB_DATA*>::iterator it = m_mapJob.find(qwKey);
    if (it == m_mapJob.end())
        return;
    JOB_DATA* pJob = it->second;
    if (pJob->m_nType)
    {
        const JOB_HANDLER& h = m_aComplete[pJob->m_nType];
        (((COneByOneProcessor*)((BYTE*)this + h.nThisAdjust))->*h.pfn)(pJob);
    }
    g_JobDataPool.Free(pJob);
    m_mapJob.erase(qwKey);
}

unsigned __int64* COneByOneProcessor::PopPendingKey()
{
    unsigned __int64* pKey = &m_deqPending.front();
    m_deqPending.pop_front();
    return pKey;
}

void COneByOneProcessor::StartSilkQuery_631A(JOB_DATA* pJob)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(SILK_HISTORY_QUERY);
    DWORD dwOne = 1;
    pMsg->WriteBytes(&dwOne, 4);
    // The body ID of this server (the first WORD of g_pSelfBody 0x8588B4; the GlobalManager
    // answers to that body).
    pMsg->WriteBytes(&g_pSelfBody->wID, 2);
    sSilkHistoryData_Write(pJob->m_pParam, pMsg);

    // BUG(SM-305): in the divisions of group 3 the request is neither sent nor released.
    if (SelfDivision_IsInGroup3() == 1)
        return;

    if (!g_pShardMainProcess)
        BSAssertFail();
    if (ServerFramework::RelayMsgToBody_6008(g_pShardMainProcess, pMsg,
                                             g_pSelfBody->pDivision->wGlobalManagerID, 0, 0,
                                             (DWORD)pJob))
    {
        return;
    }

    CMsg* pResult = g_pBSNet->AllocMsg(FALSE);
    pResult->SetOpcode(ONE_BY_ONE_MSG_RESULT);
    DWORD dwType = 1;
    BYTE byResult = SILK_QUERY_RELAY_FAILED;
    pResult->WriteBytes(&dwType, 4);
    pResult->WriteBytes(&byResult, 1);
    pResult->WriteBytes(&pJob->m_qwKey, 8);
    CTask* pTask = (CTask*)GetOwner();
    pTask->m_pQueue->Push(pResult);
}

void COneByOneProcessor::FreeJobParam(JOB_DATA* pJob)
{
    if (!pJob || !pJob->m_pParam)
        return;
    g_SilkHistoryDataPool.Free(pJob->m_pParam);
}
