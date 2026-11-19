#pragma once

#include <winsock2.h>
#include <windows.h>
#include <deque>
#include <map>

#include "BSLib/ChunkAllocator.h"
#include "ServerFramework/ServerProcess.h"

// One-by-one processor (runtime class of 0x41c860, 0x40100 bytes): serialises the silk (cash)
// history requests of the game servers. 0x791F registers a job per 8-byte key; the jobs are
// started one at a time (0x631A relayed to the GlobalManager) and completed by the answer
// 0xB91F, a cancel (0xFF) or a 60 s timeout.

// Silk history request (GM_SILK_HISTORY_REQ) kept as job parameter.
struct sSilkHistoryData
{
    DWORD               dw00;           // +00
    DWORD               dwJID;          // +04
    unsigned __int64    qwKey;          // +08
    BYTE                by10;           // +10
    BYTE                by11;           // +11
};

C_ASSERT(FIELD_OFFSET(sSilkHistoryData, qwKey) == 8);
C_ASSERT(FIELD_OFFSET(sSilkHistoryData, by11) == 0x11);

// sSilkHistoryData_Write (0x41fbf0): three DWORDs and two BYTEs from +4.
void sSilkHistoryData_Write(const sSilkHistoryData* pData, CMsg* pMsg);

enum
{
    ONE_BY_ONE_JOB_SILK_HISTORY = 1,
    ONE_BY_ONE_JOB_TYPE_COUNT   = 2,
    ONE_BY_ONE_JOB_TIMEOUT      = 60000,    // ms
};

// _JOB_DATA (0x28 bytes).
class JOB_DATA
{
public:
    // JOB_DATA::ctor (0x41c790): type 1, the key, the parameter and the QPC time in ms.
    JOB_DATA(const unsigned __int64& qwKey, sSilkHistoryData* pParam);
    // Pool storage; RegisterJob copies a constructed job into it.
    JOB_DATA() {}
    virtual ~JOB_DATA() {}

public:
    DWORD               m_dw08;         // +08
    int                 m_nType;        // +0C
    unsigned __int64    m_qwKey;        // +10
    DWORD               m_dw18;         // +18
    sSilkHistoryData*   m_pParam;       // +1C
    unsigned __int64    m_qwStartMs;    // +20
};

C_ASSERT(FIELD_OFFSET(JOB_DATA, m_nType) == 0xC);
C_ASSERT(FIELD_OFFSET(JOB_DATA, m_qwKey) == 0x10);
C_ASSERT(FIELD_OFFSET(JOB_DATA, m_pParam) == 0x1C);
C_ASSERT(sizeof(JOB_DATA) == 0x28);

// Milliseconds of QueryPerformanceCounter (Util_GetPerfFrequency 0x41fc50 keeps the frequency).
unsigned __int64 OneByOne_GetPerfMs();

class COneByOneProcessor : public ServerFramework::CServerProcessBase
{
    BS_DECLARE_DYNCREATE(COneByOneProcessor)                            // 0

public:
    typedef void (COneByOneProcessor::*PFN_JOB)(JOB_DATA* pJob);

    // Member pointer as the original stores it (function, this adjustment).
    struct JOB_HANDLER
    {
        PFN_JOB pfn;
        int     nThisAdjust;
    };

    // Provided by SM_main2 (ctor 0x41c990, Run 0x41cbf0, InitMsgHandlers 0x41cbc0,
    // OnMsg_B91F 0x41cdd0, vf09 0x41cd60).
    COneByOneProcessor();
    virtual ~COneByOneProcessor();                                      // 1 (0x41cb40)
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);               // 3
    virtual void InitMsgHandlers();                                     // 6

    // 0x41cd90: DWORD 1 -> OnCashRequest.
    void OnMsg_791F(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x41ce10: {BYTE operation, request}: 0 registers, 0xFF cancels.
    void OnCashRequest(CMsg* pMsg);
    // 0x41cf10: {BYTE result, QWORD key}: completes the job and frees the processor.
    void OnCashResult_B91F(CMsg* pMsg);
    // 0x41cf90: completes the jobs older than 60 s.
    void CheckJobTimeouts();
    // 0x41d050: a job for a new key (queued); an existing key is ignored.
    void RegisterJob(const unsigned __int64& qwKey, sSilkHistoryData* pParam);
    // 0x41d0f0: runs the start handler of the job.
    void DispatchJob(const unsigned __int64& qwKey);
    // 0x41d160: runs the completion handler, frees the job and forgets the key.
    void CompleteJob(const unsigned __int64& qwKey);
    // 0x41d1f0: takes the next queued key (the pointer stays in the deque block).
    unsigned __int64* PopPendingKey();
    // 0x41d280: start handler of type 1: 0x631A to the GlobalManager, 0xB91F {1, 0xA1, key}
    // to the own queue when the relay fails.
    void StartSilkQuery_631A(JOB_DATA* pJob);
    // 0x41d3b0: completion handler of type 1: the parameter goes back to its pool.
    void FreeJobParam(JOB_DATA* pJob);

public:
    DWORD                                       m_dw400B8;          // +400B8
    BOOL                                        m_bBusy;            // +400BC
    JOB_HANDLER                                 m_aStart[ONE_BY_ONE_JOB_TYPE_COUNT];    // +400C0
    JOB_HANDLER                                 m_aComplete[ONE_BY_ONE_JOB_TYPE_COUNT]; // +400D0
    std::map<unsigned __int64, JOB_DATA*>       m_mapJob;           // +400E0
    std::deque<unsigned __int64>                m_deqPending;       // +400EC
};

C_ASSERT(FIELD_OFFSET(COneByOneProcessor, m_bBusy) == 0x400BC);
C_ASSERT(FIELD_OFFSET(COneByOneProcessor, m_aStart) == 0x400C0);
C_ASSERT(FIELD_OFFSET(COneByOneProcessor, m_aComplete) == 0x400D0);
C_ASSERT(FIELD_OFFSET(COneByOneProcessor, m_mapJob) == 0x400E0);
C_ASSERT(FIELD_OFFSET(COneByOneProcessor, m_deqPending) == 0x400EC);
C_ASSERT(sizeof(COneByOneProcessor) == 0x40100);

// 0x8A890C
extern COneByOneProcessor* g_pOneByOneProcessor;
// The pools of the jobs and of their parameters (OneByOneProcessor_PoolAlloc_41C910 /
// JobDataPool_Free, SilkHistoryDataPool_Alloc / _Free).
extern CChunkAllocatorST<JOB_DATA> g_JobDataPool;
extern CChunkAllocatorST<sSilkHistoryData> g_SilkHistoryDataPool;
