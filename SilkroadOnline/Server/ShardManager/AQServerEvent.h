#pragma once

#include "ShardManager/AQBase.h"

// AQ_ServerEvent (vftable 0x787940, 0x7C bytes): a change of a server event of the shard
// (update, insert reward, delete or update reward). The owner fills the fields; the job has no
// console answer (OnComplete / OnFail are the shared 0x69bb10).
class AQ_ServerEvent : public CAQJobT<AQ_ServerEvent>
{
public:
    AQ_ServerEvent();                                                   // 0x47a8f0
    virtual ~AQ_ServerEvent() {}                                        // 0 (0x47a660, 0x47cf60)

    virtual void ReleaseId();                                           // 2 (0x47a6e0)
    virtual bool Execute();                                             // 6 (0x47aad0)
    virtual bool OnComplete();                                          // 7 (0x69bb10)
    virtual bool OnFail();                                              // 8 (0x69bb10)

    // Provided by SM_world1 (0x47af20, 0x47aff0, 0x47b0d0): run m_strQuery for the type.
    bool ExecUpdate();
    bool ExecInsertReward();
    bool ExecDeleteOrUpdateReward();

public:
    DWORD           m_dwServerEventID;  // +50
    DWORD           m_dw54;             // +54
    DWORD           m_dw58;             // +58
    int             m_nEventType;       // +5C  1 update, 2 insert reward, 3 / 4 reward change
    std::string     m_strQuery;         // +60
};

C_ASSERT(FIELD_OFFSET(AQ_ServerEvent, m_nEventType) == 0x5C);
C_ASSERT(FIELD_OFFSET(AQ_ServerEvent, m_strQuery) == 0x60);
C_ASSERT(sizeof(AQ_ServerEvent) == 0x7C);
