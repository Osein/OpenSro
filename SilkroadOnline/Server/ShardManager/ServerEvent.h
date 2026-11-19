#pragma once

#include <windows.h>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/Util/TimeSpan.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/ReferenceData.h"

// Server events of SR_ShardManager: the running events (CServerEvent, a row of _ServerEvent and
// its _RefServerEvent), the granted rewards (CServerEventReward, _ServerEventReward and
// _RefServerEventReward) and the delayed jobs of the server event manager (begin, end, reward
// and rollback of an event). CServerEventMgr is in ServerEventMgr.h.

// ---------------------------------------------------------------------------------------------
// Pooled objects

// Every class here comes from a static CChunkAllocatorST of its own (500 per block, named after
// the class) and carries an in-use byte as its last member. The pool Alloc helpers of the
// original (e.g. ServerEventReward_PoolAlloc 0x6813b0) set the byte without checking the
// object; Release (the "why do you about to delete already deleted" functions) clears it, resets
// the object (ResetForPool) and gives it back.
#define SERVER_EVENT_POOL_MEMBERS(class_name)                                               \
public:                                                                                     \
    static CChunkAllocatorST<class_name> s_Pool;                                            \
    static const char* const s_pszPoolName;                                                 \
    BYTE m_bInUse;

#define SERVER_EVENT_POOL_IMPLEMENT(class_name)                                             \
    CChunkAllocatorST<class_name> class_name::s_Pool(NULL, 0, 500, #class_name);            \
    const char* const class_name::s_pszPoolName = #class_name;

template <class T>
T* ServerEventPool_Alloc()
{
    T* p = T::s_Pool.Alloc();
    p->m_bInUse = TRUE;
    return p;
}

void ServerEventPool_LogDoubleRelease(const char* pszName);

template <class T>
void ServerEventPool_Release(T* p)
{
    if (!p->m_bInUse)
    {
        ServerEventPool_LogDoubleRelease(T::s_pszPoolName);
        return;
    }
    p->m_bInUse = FALSE;
    p->ResetForPool();
    T::s_Pool.Free(p);
}

// ---------------------------------------------------------------------------------------------
// CServerEvent (24 bytes, vftable 0x796d94)

class CServerEvent
{
public:
    CServerEvent();                                                         // 0x678ba0
    ~CServerEvent()
    {
    }

    // 0x678b60: back to the pool (ResetAndDetach).
    virtual void Release();                                                 // 0

    // ServerEvent_PoolAlloc (0x678b50).
    static CServerEvent* PoolAlloc()
    {
        return ServerEventPool_Alloc<CServerEvent>();
    }
    void ResetForPool()
    {
        ResetAndDetach();
    }

    // 0x678bd0: binds the record and its reference once and takes the synced values of the
    // record. FALSE (assert) when one is missing or the object is bound already.
    BOOL Attach(RefServerEvent* pRef, CInstanceServerEvent* pRecord);
    // 0x678c10: unbinds the reference and hands the record back to the _ServerEvent table.
    BOOL ResetAndDetach();
    // 0x678ca0: takes the record values as synced and posts the record update (AQ_ServerEvent).
    BOOL RequestUpdate();
    // 0x67d070: the progress or the completion count changed since the last update.
    BOOL IsDirty();
    // 0x67d0b0: the full rounds of the progress go to the completion count, the rest stays.
    void ApplyCompletionCount();
    // 0x67d150: the achievement condition becomes nCount * the reference step + 100; returns it.
    int SetRequiredValue(int nCount);
    // 0x67d1b0 / 0x67d200: adds to the progress / completion count; returns the new value.
    int AddCurrentValue(int nValue);
    int AddCompletionCount(int nCount);

    // The record getters of the original (ServerEvent_GetField18 0x47a830, GetField1C,
    // GetField20 0x47a850, GetField24 0x47a870): -1 (assert) without record.
    int GetID() const
    {
        return m_pRecord ? m_pRecord->ID : (BSAssertFail(), -1);
    }
    int GetCompletionValue() const
    {
        return m_pRecord ? m_pRecord->CompletionValue : (BSAssertFail(), -1);
    }
    int GetAchievementCondition() const
    {
        return m_pRecord ? m_pRecord->AchievementCondition : (BSAssertFail(), -1);
    }
    int GetProgressCount() const
    {
        return m_pRecord ? m_pRecord->ProgressCount : (BSAssertFail(), -1);
    }

public:
    CInstanceServerEvent*   m_pRecord;          // +04
    RefServerEvent*         m_pRef;             // +08
    int                     m_nSyncedProgress;  // +0C  ProgressCount at the last update
    int                     m_nSyncedCompletion; // +10 CompletionValue at the last update

    SERVER_EVENT_POOL_MEMBERS(CServerEvent)     // +14
};

C_ASSERT(FIELD_OFFSET(CServerEvent, m_nSyncedCompletion) == 0x10);
C_ASSERT(FIELD_OFFSET(CServerEvent, m_bInUse) == 0x14);
C_ASSERT(sizeof(CServerEvent) == 0x18);

// ---------------------------------------------------------------------------------------------
// CServerEventReward (56 bytes, vftable 0x797860)

class CServerEventReward
{
public:
    CServerEventReward();                                                   // 0x681400
    ~CServerEventReward()
    {
    }

    // 0x6813c0: back to the pool (ResetAndDetach).
    virtual void Release();                                                 // 0

    // ServerEventReward_PoolAlloc (0x6813b0).
    static CServerEventReward* PoolAlloc()
    {
        return ServerEventPool_Alloc<CServerEventReward>();
    }
    void ResetForPool()
    {
        ResetAndDetach();
    }

    // ServerEventReward_Link (0x681510): binds the record and its reference. FALSE (assert) when
    // one is missing.
    BOOL Link(CInstanceServerEventReward* pRecord, RefServerEventReward* pRef);
    // 0x6817c0: clears the time, unbinds the reference and hands the record back to the
    // _ServerEventReward table.
    BOOL ResetAndDetach();
    // 0x681530 / 0x6815f0: posts the insert (with now as the last update) / the delete of the
    // record.
    BOOL RequestInsert(const CDateTime& now);
    BOOL RequestDelete();
    // 0x6816a0: takes the time since the last update off the remaining reward time and posts
    // the update.
    BOOL UpdateRemainTime(const CDateTime& now);

    // The record getters (ServerEvent_GetField1C 0x47a890 and the inlined ones).
    int GetServerEventID() const
    {
        return m_pRecord ? m_pRecord->ServerEventID : (BSAssertFail(), -1);
    }
    int GetRewardID() const
    {
        return m_pRecord ? m_pRecord->RewardID : (BSAssertFail(), -1);
    }
    int GetRemainRewardTime() const
    {
        return m_pRecord ? m_pRecord->RemainRewardTime : (BSAssertFail(), -1);
    }

private:
    BOOL PostJob(int nKind);

public:
    CInstanceServerEventReward* m_pRecord;      // +04
    RefServerEventReward*       m_pRef;         // +08
    CDateTime                   m_LastUpdate;   // +0C  0 until the reward list went out

    SERVER_EVENT_POOL_MEMBERS(CServerEventReward) // +34
};

C_ASSERT(FIELD_OFFSET(CServerEventReward, m_LastUpdate) == 0x0C);
C_ASSERT(FIELD_OFFSET(CServerEventReward, m_bInUse) == 0x34);
C_ASSERT(sizeof(CServerEventReward) == 0x38);

// ---------------------------------------------------------------------------------------------
// Delayed jobs of CServerEventMgr (base vftable 0x796e50, 56 bytes)

class CServerEventDelayJob
{
public:
    enum
    {
        TYPE_NONE = 0,
        TYPE_COMPLETION_AND_REWARD = 1,
        TYPE_ROLLBACK_REWARD = 2,
        TYPE_CHECK_END_SERVER_EVENT = 3,
        TYPE_WAIT_REGISTER_SERVER_EVENT = 4,
    };

    CServerEventDelayJob();                                                 // 0x679930
    virtual ~CServerEventDelayJob()                                         // 0 (0x679960)
    {
    }

    // 0x6799d0: type 0, no event, the delay and the start are reset.
    virtual void Reset();                                                   // 1
    // The work of the job, done by CServerEventMgr::ProcessingDelayJob_*.
    virtual BOOL Execute() = 0;                                             // 2
    // Back to the pool of the job class.
    virtual void Release() = 0;                                             // 3
    // 0x679a10: the delay since the start has passed.
    virtual BOOL IsDue(const CDateTime& now);                               // 4

    // 0x679990: the type, the event and the delay in seconds, started now.
    void Init(BYTE byType, LONG lDelay, DWORD dwServerEventID);

public:
    BYTE        m_byType;                       // +04  TYPE_*
    DWORD       m_dwServerEventID;              // +08
    CTimeSpan   m_Delay;                        // +0C
    CDateTime   m_Start;                        // +10
};

C_ASSERT(FIELD_OFFSET(CServerEventDelayJob, m_Delay) == 0x0C);
C_ASSERT(sizeof(CServerEventDelayJob) == 0x38);

// The pooled part of every delayed job: Release (slot 3, "why do you about to delete already
// deleted !!! [class]") resets the job with its slot 1 and gives it back to the pool of T.
template <class T, BYTE TYPE>
class CServerEventDelayJobT : public CServerEventDelayJob
{
public:
    // The PoolAlloc helpers (0x679a40, 0x679bc0, 0x679d30, 0x679e80).
    static T* Alloc()
    {
        return ServerEventPool_Alloc<T>();
    }

    virtual void Release()                                                  // 3
    {
        ServerEventPool_Release(static_cast<T*>(this));
    }

    void ResetForPool()
    {
        Reset();
    }

protected:
    void InitJob(LONG lDelay, DWORD dwServerEventID)
    {
        Init(TYPE, lDelay, dwServerEventID);
    }
};

// Reward of a completed event (vftable: Execute 0x679b80, Release 0x679a50; 68 bytes).
class CServerEventDelayJob_CompletionAndReward
    : public CServerEventDelayJobT<CServerEventDelayJob_CompletionAndReward,
                                   CServerEventDelayJob::TYPE_COMPLETION_AND_REWARD>
{
public:
    CServerEventDelayJob_CompletionAndReward();                             // 0x679a90

    virtual void Reset();                                                   // 1 (0x679b60)
    virtual BOOL Execute();                                                 // 2

    // ServerEventDelayJob_InitCompletionAndReward (0x679b40).
    void Init(LONG lDelay, DWORD dwServerEventID, BYTE byRewardTarget, DWORD dwUserJID);

public:
    BYTE        m_byRewardTarget;               // +38
    DWORD       m_dwUserJID;                    // +3C

    SERVER_EVENT_POOL_MEMBERS(CServerEventDelayJob_CompletionAndReward) // +40
};

C_ASSERT(FIELD_OFFSET(CServerEventDelayJob_CompletionAndReward, m_bInUse) == 0x40);
C_ASSERT(sizeof(CServerEventDelayJob_CompletionAndReward) == 0x44);

// End of a granted reward (vftable: Execute 0x679d00, Release 0x679bd0; 64 bytes).
class CServerEventDelayJob_RollbackReward
    : public CServerEventDelayJobT<CServerEventDelayJob_RollbackReward,
                                   CServerEventDelayJob::TYPE_ROLLBACK_REWARD>
{
public:
    CServerEventDelayJob_RollbackReward();                                  // 0x679c10

    virtual void Reset();                                                   // 1 (0x679ce0)
    virtual BOOL Execute();                                                 // 2

    // ServerEventDelayJob_InitRollbackReward (0x679cc0).
    void Init(LONG lDelay, DWORD dwServerEventID, DWORD dwRewardID);

public:
    DWORD       m_dwRewardID;                   // +38

    SERVER_EVENT_POOL_MEMBERS(CServerEventDelayJob_RollbackReward) // +3C
};

C_ASSERT(FIELD_OFFSET(CServerEventDelayJob_RollbackReward, m_bInUse) == 0x3C);
C_ASSERT(sizeof(CServerEventDelayJob_RollbackReward) == 0x40);

// End of a running event (vftable: Reset 0x679e40, Execute 0x679e50, Release 0x679d40; 60 bytes).
class CServerEventDelayJob_CheckEndServerEvent
    : public CServerEventDelayJobT<CServerEventDelayJob_CheckEndServerEvent,
                                   CServerEventDelayJob::TYPE_CHECK_END_SERVER_EVENT>
{
public:
    CServerEventDelayJob_CheckEndServerEvent();                             // 0x679d80

    virtual BOOL Execute();                                                 // 2

    // ServerEventDelayJob_InitCheckEnd (0x679e30).
    void Init(LONG lDelay, DWORD dwServerEventID)
    {
        InitJob(lDelay, dwServerEventID);
    }

    SERVER_EVENT_POOL_MEMBERS(CServerEventDelayJob_CheckEndServerEvent) // +38
};

C_ASSERT(FIELD_OFFSET(CServerEventDelayJob_CheckEndServerEvent, m_bInUse) == 0x38);
C_ASSERT(sizeof(CServerEventDelayJob_CheckEndServerEvent) == 0x3C);

// Begin of an event that has not started yet (vftable: Execute 0x679fd0, Release 0x679e90;
// 64 bytes).
class CServerEventDelayJob_WaitRegisterServerEvent
    : public CServerEventDelayJobT<CServerEventDelayJob_WaitRegisterServerEvent,
                                   CServerEventDelayJob::TYPE_WAIT_REGISTER_SERVER_EVENT>
{
public:
    CServerEventDelayJob_WaitRegisterServerEvent();                         // 0x679ed0

    virtual void Reset();                                                   // 1 (0x679fb0)
    virtual BOOL Execute();                                                 // 2

    // 0x679f80: asserts pEvent.
    void Init(CServerEvent* pEvent, DWORD dwServerEventID, LONG lDelay);

public:
    CServerEvent*   m_pServerEvent;             // +38  not set by the constructor

    SERVER_EVENT_POOL_MEMBERS(CServerEventDelayJob_WaitRegisterServerEvent) // +3C
};

C_ASSERT(FIELD_OFFSET(CServerEventDelayJob_WaitRegisterServerEvent, m_bInUse) == 0x3C);
C_ASSERT(sizeof(CServerEventDelayJob_WaitRegisterServerEvent) == 0x40);
