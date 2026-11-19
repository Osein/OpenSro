#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/Util/TimeSpan.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/ServerProcess.h"

// Periodic member callbacks driven by the elapsed time the owner passes to Update. The
// August 2011 variant of CScheduledCallbacker (BSNet/Maintainer.h) with 24-byte entries
// (CScheduledCallbacker<CMaintainer, K>, vftable of 0x432550, element 24 bytes).
template <class T>
class CScheduledCallbackerK
{
public:
    typedef void (T::*PFN_CALLBACK)();

    struct ENTRY
    {
        DWORD           dwElapsed;      // +00
        DWORD           dwPeriod;       // +04  ms
        T*              pObject;        // +08
        DWORD           dwReserved;     // +0C
        PFN_CALLBACK    pfnCallback;    // +10
        int             nThisAdjust;    // +14  added to pObject for the call
    };

    virtual ~CScheduledCallbackerK() {}

    // The inlined push_back of CShardMaintainer::LoadDeletedChars.
    void Add(DWORD dwPeriod, T* pObject, PFN_CALLBACK pfnCallback)
    {
        ENTRY e;
        e.dwElapsed = 0;
        e.dwPeriod = dwPeriod;
        e.pObject = pObject;
        e.dwReserved = 0;
        e.pfnCallback = pfnCallback;
        e.nThisAdjust = 0;
        m_vecEntry.push_back(e);
    }

    // CScheduledCallbacker_CShardMaintainer::Update (0x432610): adds dwElapsed to every entry
    // and calls the ones whose period is reached (keeping the remainder).
    void Update(DWORD dwElapsed)
    {
        if (!dwElapsed)
            return;
        for (size_t i = 0; i < m_vecEntry.size(); ++i)
        {
            ENTRY& e = m_vecEntry.at(i);
            e.dwElapsed += dwElapsed;
            if (e.dwElapsed >= e.dwPeriod)
            {
                e.dwElapsed -= e.dwPeriod;
                T* pThis = (T*)((BYTE*)e.pObject + e.nThisAdjust);
                (pThis->*e.pfnCallback)();
            }
        }
    }

public:
    std::vector<ENTRY>  m_vecEntry;     // +04
};

// A temporarily deleted character waiting for its permanent deletion (0x48 bytes).
struct DeletedCharEntry
{
    DeletedCharEntry()                  // 0x430bc0
    {
        pRecord = NULL;
    }
    ~DeletedCharEntry();                // 0x430a80

    // DeletedCharEntry_SetRecord (0x430c20): asserts pRec, keeps it and its deletion time.
    void SetRecord(CInstanceDeletedChar* pRec);
    // DeletedCharEntry_ReleaseRecord (0x430c60): the record goes back to its table.
    void ReleaseRecord();
    // DeletedChar_GetKey18 (0x430cc0) / DeletedChar_GetField1C (0x430cb0): 0 without record.
    int GetCharID() const   { return pRecord ? pRecord->CharID : 0; }
    int GetJID() const      { return pRecord ? pRecord->UserJID : 0; }

    CInstanceDeletedChar*   pRecord;        // +00
    CDateTime               dtDeleted;      // +04
    std::string             strCharName;    // +2C  set by DeleteCharPermanently
};

C_ASSERT(FIELD_OFFSET(DeletedCharEntry, strCharName) == 0x2C);
C_ASSERT(sizeof(DeletedCharEntry) == 0x48);

// ShardMaintainer_GetRemainUntilPermanentDelete (0x430b00): time left until 7 days after
// dtDeleted, 0 once that is past.
CTimeSpan ShardMaintainer_GetRemainUntilPermanentDelete(const CDateTime& dtNow,
                                                        const CDateTime& dtDeleted);

enum
{
    SHARD_MAINTAINER_MSG    = 0x380E,

    DELETE_BUCKET_HOUR      = 0,    // due within the hour, checked every minute
    DELETE_BUCKET_DAY       = 1,    // due within the day, checked every hour
    DELETE_BUCKET_LATER     = 2,    // checked every day
    DELETE_BUCKET_COUNT     = 3,
};

typedef std::map<int, DeletedCharEntry*> DeletedCharMap;

// Shard maintainer (runtime class 0x781b60, 0x40184 bytes): deletes temporarily deleted
// characters permanently 7 days after their deletion and restores them on request.
class CShardMaintainer : public ServerFramework::CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CShardMaintainer)                              // 0 (0x430d30, 0x430cd0)

public:
    CShardMaintainer();                                                 // 0x430d40
    virtual ~CShardMaintainer();                                        // 1 (0x430e80, 0x430e10)

    // 0x430fa0: handles the queue and drives the schedule until the stop flag is set.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);               // 3
    // 0x430f50: the framework handlers and 0x380E.
    virtual void InitMsgHandlers();                                     // 6

    // 0x4310d0 (from CShardManager::vf11): loads _DeletedChar, deletes what is due and starts
    // the schedule.
    void LoadDeletedChars();

    // 0x4313e0: -1 due, otherwise the DELETE_BUCKET_* of the entry.
    int ClassifyRemainTime(DeletedCharEntry* pEntry);
    // 0x431440 (every minute): deletes the due entries of the hour bucket.
    void ProcessExpiredBucket();
    // 0x431570 (every hour): moves day entries due within the hour to the hour bucket.
    void RebucketDayMap();
    // 0x431670 (every day): moves later entries due within the day to the day bucket.
    void RebucketHourMap();
    // 0x431770: deletes every entry of the delete list and frees it.
    void FlushDeleteList();
    // 0x431850: 0x380E {WORD 1, JID, CharID, error code, name} to the main process.
    void SendCharDeletedNotify(DeletedCharEntry* pEntry, int nErrorCode);
    // 0x4319c0: _DeleteCharPermanently; 1 when it succeeded.
    int DeleteCharPermanently(int* pnErrorCode, DeletedCharEntry* pEntry);

    // 0x431bf0: subtype 2 revive, 4 temporarily deleted.
    void OnMsg_380E(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);
    // 0x431c30: _ReviveChar and 0x380E {WORD 3, JID, CharID, BYTE 1 | BYTE 2 WORD code}.
    void OnReviveChar(CMsg* pMsg);
    // 0x431e10: takes the character out of the bucket that holds it.
    void RemoveFromDeleteBuckets(int nCharID);
    // 0x431ef0: a new entry for the record in the message (later bucket).
    void OnCharDeletedTemp(CMsg* pMsg);

private:
    DeletedCharEntry* AllocEntry();
    void FreeEntry(DeletedCharEntry* pEntry);

public:
    DWORD                                   m_dw400B8;          // +400B8
    CChunkAllocatorST<DeletedCharEntry>     m_EntryPool;        // +400BC
    CDateTime                               m_dtNow;            // +40118
    DeletedCharMap                          m_aBucket[DELETE_BUCKET_COUNT]; // +40140
    std::list<DeletedCharEntry*>            m_listDelete;       // +40164
    CScheduledCallbackerK<CShardMaintainer> m_Schedule;        // +40170
};

C_ASSERT(FIELD_OFFSET(CShardMaintainer, m_EntryPool) == 0x400BC);
C_ASSERT(FIELD_OFFSET(CShardMaintainer, m_dtNow) == 0x40118);
C_ASSERT(FIELD_OFFSET(CShardMaintainer, m_aBucket) == 0x40140);
C_ASSERT(FIELD_OFFSET(CShardMaintainer, m_listDelete) == 0x40164);
C_ASSERT(FIELD_OFFSET(CShardMaintainer, m_Schedule) == 0x40170);
C_ASSERT(sizeof(CShardMaintainer) == 0x40184);

// 0x8A8914
extern CShardMaintainer* g_pShardMaintainer;
