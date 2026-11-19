#pragma once

#include <winsock2.h>
#include <windows.h>
#include <list>
#include <string>
#include <vector>

#include "BSLib/BSDebug.h"
#include "BSLib/DB/Database.h"
#include "BSNet/Msg.h"
#include "ServerCommon/PartyInfo.h"
#include "ShardManager/ShardExtern.h"

// What the social classes of SR_ShardManager (guilds, messenger, party manager, training camps,
// votes) use from other subsystems. Only the members they call are declared; the owning groups
// define them (the tests link stand-ins). The guild code adds its own in GuildExtern.h; the
// declarations shared with the other groups are in ShardExtern.h.

class CUser;

// ---------------------------------------------------------------------------------------------
// Provided by the transaction group (transaction, trs_messenger_job, trs_party_job,
// 0x41fcc0..0x425bf0)

// Pending request that waits for the answer of another user (COverlappedJobMgr).
class transaction
{
public:
    virtual ~transaction();                                                     // 0
    // transaction::vf01 (0x4206f0)
    virtual void Init(DWORD dwRequesterJID, DWORD dwOpcode, DWORD dwTargetJID, float fTimeout,
                      DWORD dw5, DWORD dw6);                                    // 1
    virtual void vf02();                                                        // 2
    // Returns the job to its pool (trs_*_job::vf03).
    virtual void Release();                                                     // 3

    // transaction::AddParticipantJID (0x420750)
    void AddParticipantJID(DWORD dwJID);

public:
    DWORD   m_dw04;                 // +0x04
    DWORD   m_dwRequesterJID;       // +0x08
    DWORD   m_dw0C;                 // +0x0C
    DWORD   m_dwTargetJID;          // +0x10
    BYTE    m_pad14[0x24];          // +0x14
    DWORD   m_dwTargetSessionID;    // +0x38
};

C_ASSERT(FIELD_OFFSET(transaction, m_dwRequesterJID) == 0x08);
C_ASSERT(FIELD_OFFSET(transaction, m_dwTargetJID) == 0x10);
C_ASSERT(FIELD_OFFSET(transaction, m_dwTargetSessionID) == 0x38);

class trs_messenger_job : public transaction
{
};

class trs_party_job : public transaction
{
public:
    sPartyInfo          m_Info;         // +0x3C
    sPartyMemberInfo    m_Master;       // +0xB0
    BYTE                m_pad114[0x0C]; // +0x114
    DWORD               m_dw120;        // +0x120
};

C_ASSERT(FIELD_OFFSET(trs_party_job, m_Info) == 0x3C);
C_ASSERT(FIELD_OFFSET(trs_party_job, m_Master) == 0xB0);
C_ASSERT(FIELD_OFFSET(trs_party_job, m_dw120) == 0x120);

// trs_messenger_job_PoolAlloc_420430 / trs_party_job_PoolAlloc_420530
trs_messenger_job* trs_messenger_job_Alloc();
trs_party_job* trs_party_job_Alloc();

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (COverlappedJobMgr; CShardDB is in ShardExtern.h)

class COverlappedJobMgr
{
public:
    // COverlappedJobMgr::StartJob (0x422150)
    BOOL StartJob(transaction* pJob, CUser* pRequester, CMsg* pMsg);
};

// GetSingleton_COverlappedJobMgr (0x40baf0, g_pOverlappedJobMgr 0x8a8910)
COverlappedJobMgr* GetSingleton_COverlappedJobMgr();


enum
{
    SHARD_TABLE_FRIEND  = 0x14,
    SHARD_TABLE_MEMO    = 0x15,
    SHARD_TABLE_TRAINING_CAMP           = 0x16,
    SHARD_TABLE_TRAINING_CAMP_MEMBER    = 0x17,
    SHARD_TABLE_TRAINING_CAMP_BUFF      = 0x19,
};
