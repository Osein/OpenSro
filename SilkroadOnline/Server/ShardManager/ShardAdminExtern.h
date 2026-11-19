#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/OverlapJob.h"

// Provided by SM_admin: the admin query (AQ) jobs the shard world code fills and posts. The AQ
// job classes and their pools belong to the admin branch (AQBase.h, AdminQueryJobs.h); these
// declarations and the AQ_SiegeDataUpdater stand-in are replaced by its headers when it is
// merged.

class CServerEvent;
class CServerEventReward;

namespace ServerFramework { class CMassiveMsg; }

// ---------------------------------------------------------------------------------------------
// AQ_ServerEvent (the server event and reward records)

class AQ_ServerEvent;
// UserCertifier_AllocJob (0x47a8b0): an AQ_ServerEvent from its pool, NULL when there is none.
AQ_ServerEvent* AQ_ServerEvent_Alloc();
// AQ_ServerEvent::InitUpdate (0x47a960): the update of the event record.
BOOL AQ_ServerEvent_InitUpdate(AQ_ServerEvent* pJob, CServerEvent* pEvent);
// AQ_ServerEvent::InitReward (0x47a9d0): nKind 2 insert, 3 delete, 4 update of the reward record.
BOOL AQ_ServerEvent_InitReward(AQ_ServerEvent* pJob, CServerEventReward* pReward, int nKind);
// The job's vftable slot 3 (+0x0C), called with the job itself as argument: back to the pool.
void AQ_ServerEvent_Free(AQ_ServerEvent* pJob);

// ---------------------------------------------------------------------------------------------
// The log and reputation jobs

// AQ_AssociationReputation::Alloc (UserCertifier_AllocJob_465460).
ServerFramework::COverlapJob* AQ_AssociationReputation_Alloc();
// AQ_ServerEventLog::Alloc (0x47b1b0, CChunkAllocatorST_AQ_ServerEventLog::AllocJob).
ServerFramework::COverlapJob* AQ_ServerEventLog_Alloc();
// AQ_ScheduleInsertLog::Alloc (CChunkAllocatorST_AQ_ScheduleInsertLog::AllocJob).
ServerFramework::COverlapJob* AQ_ScheduleInsertLog_Alloc();

// ---------------------------------------------------------------------------------------------
// AQ_SiegeDataUpdater (0x118 bytes; AdminQueryJobs.h of SM_admin): only the members used by
// SiegeDataUpdaterWorld1.cpp.
class AQ_SiegeDataUpdater
{
public:
    // 0x401990 (SiegeDataUpdaterWorld1.cpp): updates the record, writes the answer and notifies
    // the game servers (0x3542).
    bool ExecuteAndReply();

public:
    BYTE                            m_abBase[0x20];         // +00 AQ_Base
    BYTE                            m_byResult;             // +20
    BYTE                            m_ab21[0x2B];           // +21
    ServerFramework::CMassiveMsg*   m_pResponse;            // +4C
    BYTE                            m_byQueryTarget;        // +50  0 introduction, 1 permission
    BYTE                            m_byIntroPermission;    // +51
    DWORD                           m_dwFortressID;         // +54
    std::string                     m_strDesc;              // +58
    std::string                     m_strIntroduction;      // +74
    std::string                     m_str90;                // +90
    CInstanceSiegeFortress          m_Fortress;             // +B0
};

C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_byResult) == 0x20);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_pResponse) == 0x4C);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_dwFortressID) == 0x54);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_strIntroduction) == 0x74);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_Fortress) == 0xB0);
C_ASSERT(sizeof(AQ_SiegeDataUpdater) == 0x118);
