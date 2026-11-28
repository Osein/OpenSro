// AQ_SiegeDataUpdater::ExecuteAndReply (0x401990) and its two game server notifications (the
// class is in AdminQueryJobs.h).

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/MassiveMsg.h"
#include "ShardManager/AdminQueryJobs.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

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
        nResult = m_Fortress.Update(GetSingleton_CShardDB()->GetTable(SHARD_TABLE_SIEGE_FORTRESS),
                                    SIEGE_FORTRESS_GROUP_INTRODUCTION);
    }
    else if (m_byQueryTarget == 1)
    {
        nResult = m_Fortress.Update(GetSingleton_CShardDB()->GetTable(SHARD_TABLE_SIEGE_FORTRESS),
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
