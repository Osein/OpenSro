#pragma once

#include <windows.h>

#include <map>
#include <queue>
#include <set>
#include <vector>

#include "ServerCommon/ByteStream.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/ServerEvent.h"

class CMsg;

// Opcode of the server event messages to the game servers; the first byte is one of
// SERVER_EVENT_MSG_*.
enum
{
    MSG_SHARD_SERVER_EVENT = 0x34F2,
};

enum
{
    SERVER_EVENT_MSG_COMPLETION_AND_REWARD = 1,
    SERVER_EVENT_MSG_SYNC_PROGRESS = 2,
    SERVER_EVENT_MSG_COMPLETION = 3,
    SERVER_EVENT_MSG_ROLLBACK_REWARD = 4,
    SERVER_EVENT_MSG_REGISTER = 5,
    SERVER_EVENT_MSG_END = 6,
    SERVER_EVENT_MSG_REWARD_LIST_BEGIN = 7,
    SERVER_EVENT_MSG_REWARD_PROGRESS = 9,
};

// The order of the delayed job heap (STL_push_heap_DelayJob 0x681190 / 0x681280): the job with
// the shortest delay on top. BUG(SM-137): the start time of the job is not part of the order.
struct ServerEventDelayJobGreater
{
    bool operator()(const CServerEventDelayJob* pLeft, const CServerEventDelayJob* pRight) const
    {
        return pLeft->m_Delay.m_lSpan > pRight->m_Delay.m_lSpan;
    }
};

typedef std::priority_queue<CServerEventDelayJob*, std::vector<CServerEventDelayJob*>,
                            ServerEventDelayJobGreater> ServerEventDelayJobHeap;

// The server event manager (static object 0x8a80d8, g_pServerEventMgr = dword_8A8960; vftable
// 0x7977d4, 132 bytes).
class CServerEventMgr
{
public:
    typedef std::map<DWORD, CServerEvent*>          ServerEventMap;
    typedef std::multimap<DWORD, DWORD>             DetectingTargetMap;
    typedef std::set<DWORD>                         FinishedTargetSet;
    typedef std::map<DWORD, CServerEventReward*>    ServerEventRewardMap;

    // CServerEventMgr::DynInit (0x67d540): asserts that there is no other manager.
    CServerEventMgr();
    virtual ~CServerEventMgr();                                             // 0 (0x67d6c0, 0x67d7f0)

    // CServerEventMgr_TickProcess (0x67d8f0, called by CMainProcess::InitGameWorld): the events,
    // then the rewards of the shard DB.
    BOOL LoadFromDB();
    // 0x67f020 / 0x67f350.
    BOOL LoadServerEvents();
    BOOL LoadServerEventRewards();

    // CServerEventMgr_Run (0x67e270, called by CMainProcess::OnTickScheduler).
    void Run();
    // 0x67da50: runs and releases the jobs on top of the heap that are due.
    void ProcessDelayJobs();
    // 0x67db40: moves the jobs appended while jobs ran into the heap.
    void FlushPendingDelayJobs();

    // 0x67d910 (CMainProcess::OnMsg_3CF1): a detecting notification of a game server.
    BOOL OnDetectingNotify(CMsg* pMsg);
    // 0x67dc20 (CMainProcess_TickServerEventMgr): updates the changed events and sends the
    // progress of the progress events.
    void SyncData();
    // 0x67ded0 (MainTimer_ServerEventRewardProgressSync): updates and sends the remaining time
    // of the rewards whose list went out.
    void SendRewardProgressSync();
    // 0x67e130 (CMainProcess_ServerEventMgrNotify): once, the begin of the reward list and the
    // rollback jobs of the loaded rewards.
    void SendRewardListOnce();
    // 0x67ef10 (ProcessingServerEventCompletion): the completion of an event.
    BOOL NotifyCompletionMsg(CServerEvent* pEvent);

    // The work of the delayed jobs.
    BOOL ProcessingDelayJob_CompletionAndReward(DWORD dwServerEventID, BYTE byRewardTarget,
                                                DWORD dwUserJID);              // 0x67e280
    BOOL ProcessingDelayJob_CheckEndServerEvent(DWORD dwServerEventID);       // 0x67e610
    BOOL ProcessingDelayJob_RollbackReward(DWORD dwServerEventID, DWORD dwRewardID); // 0x67e6a0
    BOOL ProcessingDelayJob_WaitRegisterServerEvent(CServerEvent* pEvent);    // 0x67e740

    // Appending delayed jobs. With bPending the job waits in the pending list until the jobs
    // that run now are done.
    BOOL AppendDelayJobForCompletionAndReward(CServerEvent* pEvent, DWORD dwUserJID); // 0x67ec30
    BOOL AppendDelayJobForRollbackReward(DWORD dwServerEventID, LONG lDelay, DWORD dwRewardID,
                                         BOOL bPending);                     // 0x67ed10
    BOOL AppendDelayJobForCheckEndServerEvent(CServerEvent* pEvent, BOOL bPending); // 0x67ed80
    BOOL AppendDelayJobForWaitRegisterServerEvent(RefServerEvent* pRef,
                                                  CInstanceServerEvent* pRecord); // 0x67ee50

    // The containers.
    BOOL InsertServerEvent(CServerEvent* pEvent, RefServerEvent* pRef);       // 0x67d250
    CServerEvent* FindServerEvent(DWORD dwServerEventID);                    // 0x67d300
    BOOL DeleteServerEvent(DWORD dwServerEventID);                           // 0x67f5c0
    // 0x67d350: notifications of the target are ignored from now on. FALSE when it was
    // finished already.
    BOOL AddFinishedTarget(DWORD dwTarget);
    BOOL InsertServerEventReward(CServerEventReward* pReward);                // 0x67d3b0
    BOOL DeleteServerEventReward(DWORD dwRewardID);                          // 0x67d420
    CServerEventReward* FindServerEventReward(DWORD dwRewardID);             // 0x67d4a0
    BOOL HasServerEventReward(DWORD dwRewardID);                             // 0x67d500

    // 0x681430: a new reward with a new _ServerEventReward record; NULL (assert) on failure.
    static CServerEventReward* AllocNewServerEventReward(DWORD dwServerEventID, DWORD dwRewardID,
                                                         int nRemainRewardTime);

private:
    // The inlined stream writer of SyncData / SendRewardProgressSync: m_Stream writes into
    // m_Buffer and gives the positions back at the end.
    void BeginStream();
    void EndStream();
    void ClearBuffer()
    {
        m_Buffer.m_nWritePos = 0;
        m_Buffer.m_nReadPos = 0;
    }
    void PushDelayJob(CServerEventDelayJob* pJob, BOOL bPending);

public:
    ServerEventDelayJobHeap     m_heapDelayJob;         // +04 (comparator byte +14)
    std::vector<CServerEventDelayJob*> m_vecPendingDelayJob; // +18
    ServerEventMap              m_mapServerEvent;       // +28  by event ID
    DetectingTargetMap          m_mmapDetectingTarget;  // +34  target -> event ID
    FinishedTargetSet           m_setFinishedTarget;    // +40
    ServerEventRewardMap        m_mapServerEventReward; // +4C  by reward ID
    CStreamBuffer               m_Buffer;               // +58  0x400 bytes
    CByteStream                 m_Stream;               // +6C
    CStreamBuffer*              m_pStreamOwner;         // +80
};

C_ASSERT(sizeof(ServerEventDelayJobHeap) == 0x14);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_vecPendingDelayJob) == 0x18);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_mapServerEvent) == 0x28);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_mmapDetectingTarget) == 0x34);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_setFinishedTarget) == 0x40);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_mapServerEventReward) == 0x4C);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_Buffer) == 0x58);
C_ASSERT(FIELD_OFFSET(CServerEventMgr, m_Stream) == 0x6C);
C_ASSERT(sizeof(CServerEventMgr) == 0x84);

extern CServerEventMgr* g_pServerEventMgr;
extern CServerEventMgr g_ServerEventMgr;
