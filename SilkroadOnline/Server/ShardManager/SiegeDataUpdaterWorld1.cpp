// AQ_SiegeDataUpdater::ExecuteAndReply (0x401990) and its two game server notifications. The
// class belongs to SM_admin (AdminQueryJobs.h); the declaration below carries only the members
// used here and is replaced by AdminQueryJobs.h when the branches meet.

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/MassiveMsg.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

// ---------------------------------------------------------------------------------------------
// Provided by SM_admin (AdminQueryJobs.h): AQ_SiegeDataUpdater (0x118 bytes)

class AQ_SiegeDataUpdater
{
public:
    // Provided by SM_world1 (0x401990): updates the record, writes the answer and notifies the
    // game servers (0x3542).
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

// ---------------------------------------------------------------------------------------------

enum
{
    SHARD_TABLE_SIEGE_FORTRESS = 0x25,

    // 0x3542 to the game servers: BYTE kind, DWORD fortress ID, kind 0 the introduction text,
    // kind 1 the introduction modification permission.
    MSG_SHARD_SIEGE_FORTRESS_INTRODUCTION = 0x3542,

    // The update groups of _SiegeFortress written here.
    SIEGE_FORTRESS_GROUP_INTRODUCTION = 7,
    SIEGE_FORTRESS_GROUP_INTRO_PERMISSION = 10,
};

// AQ_SiegeDataUpdater::BroadcastIntroduction_3542 (0x401ad0)
static void SiegeDataUpdater_BroadcastIntroduction(AQ_SiegeDataUpdater* pJob)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_SIEGE_FORTRESS_INTRODUCTION);
    BYTE byKind = 0;
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&pJob->m_dwFortressID, 4);
    *pMsg << pJob->m_strIntroduction.c_str();
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

// AQ_SiegeDataUpdater::BroadcastIntroPermission_3542 (0x401b50)
static void SiegeDataUpdater_BroadcastIntroPermission(AQ_SiegeDataUpdater* pJob)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_SIEGE_FORTRESS_INTRODUCTION);
    BYTE byKind = 1;
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&pJob->m_dwFortressID, 4);
    pMsg->WriteBytes(&pJob->m_byIntroPermission, 1);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

bool AQ_SiegeDataUpdater::ExecuteAndReply()
{
    int nResult = 0;
    if (m_byQueryTarget == 0)
    {
        nResult = m_Fortress.Update(ShardWorld1_GetTable(SHARD_TABLE_SIEGE_FORTRESS),
                                    SIEGE_FORTRESS_GROUP_INTRODUCTION);
    }
    else if (m_byQueryTarget == 1)
    {
        nResult = m_Fortress.Update(ShardWorld1_GetTable(SHARD_TABLE_SIEGE_FORTRESS),
                                    SIEGE_FORTRESS_GROUP_INTRO_PERMISSION);
    }
    if (nResult & ~1)
    {
        m_byResult = 2;
        return false;
    }

    m_byResult = 1;
    BYTE byOk = 1;
    m_pResponse->Write(&byOk, 1);
    m_pResponse->Write(&m_byQueryTarget, 1);
    if (m_byQueryTarget == 0)
    {
        SiegeDataUpdater_BroadcastIntroduction(this);
    }
    else if (m_byQueryTarget == 1)
    {
        m_pResponse->Write(&m_byIntroPermission, 1);
        SiegeDataUpdater_BroadcastIntroPermission(this);
    }
    return true;
}
