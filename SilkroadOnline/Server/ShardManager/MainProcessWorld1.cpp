#include "ShardManager/MainProcessWorld1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/Climate.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    MSG_SHARD_WEATHER = 0x3809,
    MSG_SHARD_SUSPECTED_USER = 0x3CC3,
    MSG_SHARD_UNIQUE_SPAWN = 0x34BB,
    MSG_SHARD_RELAY_TO_GLOBAL = 0x2327,
};

// ---------------------------------------------------------------------------------------------
// Weather

void MainProcess_BroadcastWeather(CClimateManager* pMgr, bool bForce)
{
    std::vector<CClimate*> vecChanged;
    int nChanged = pMgr->UpdateAll(vecChanged, bForce);
    if (nChanged <= 0)
        return;
    if (nChanged != (int)vecChanged.size())
        BSAssertFail();

    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_WEATHER);
    BYTE byCount = (BYTE)nChanged;
    pMsg->WriteBytes(&byCount, 1);
    for (size_t i = 0; i < vecChanged.size(); ++i)
    {
        if (!vecChanged[i])
        {
            // The message is dropped without being freed.
            BSAssertFail();
            return;
        }
        vecChanged[i]->WriteToMsg(pMsg);
    }
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

// ---------------------------------------------------------------------------------------------
// Temple of Seth

BOOL TempleSeth_OpenGameWorld(WORD* pwWorldID, WORD* pwLayerID)
{
    RefGameWorld* pRef =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_TEMPLE_SETH"));
    if (!pRef)
    {
        LogPrintf(0x2000000,
                  "Seth Of Temple data is not exists!! check the gust and service for the data");
        BSAssertFail();
        return FALSE;
    }

    sGameWorldCreateParam param;
    WORD wWorldID = (WORD)pRef->ID;
    BYTE byResult = 1;
    param.m_dw0 = 0;
    param.m_b4 = 7;
    param.m_dw8 = 0;
    param.m_b12 = 1;
    param.m_b13 = 0;
    param.m_dwDuration = (DWORD)pRef->WorldDurationTime;
    param.m_dwEmptyRemainTime = (DWORD)pRef->WorldEmptyRemainTime;
    param.m_b24 = 0;

    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, "Failed to find Seth Of Temple GameWorld!!");
        BSAssertFail();
        return FALSE;
    }
    GameWorldLayer* pLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!pLayer)
    {
        LogPrintf(0x2000000, "Open Seth Of Temple GameWorld Failed!! error [ %d ] ", byResult);
        BSAssertFail();
        return FALSE;
    }
    LogPrintf(0, "Seth Of Temple is now Opened ...");
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, pLayer);
    *pwWorldID = (WORD)pWorld->m_pRef->ID;
    *pwLayerID = pLayer->m_wLayerID;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Suspected users

void SuspectedUser_LoadFromFile(CMainProcess* pProcess, DWORD dwCommand)
{
    std::vector<DWORD> vecJID;
    FILE* pFile = NULL;
    if (fopen_s(&pFile, "SuspectedUserJID.txt", "r"))
    {
        LogPrintf(0x2000000, "Cannot find %s file!", "SuspectedUserJID.txt");
        return;
    }
    // A line that does not convert stores the previous value once more and ends the list.
    int nJID = 0;
    int nRead;
    do
    {
        nRead = fscanf_s(pFile, "%d\r\n", &nJID);
        if (nRead == -1)
            break;
        vecJID.push_back((DWORD)nJID);
    }
    while (nRead == 1);
    fclose(pFile);
    SuspectedUser_SendCommand(&vecJID, pProcess, dwCommand);
}

void SuspectedUser_SendCommand(std::vector<DWORD>* pvecJID, CMainProcess* pProcess,
                               DWORD dwCommand)
{
    std::set<DWORD>& setSuspected = MainProcess_GetSuspectedUsers(pProcess);
    if (dwCommand < 2)
    {
        DWORD dwCount = (DWORD)pvecJID->size();
        CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
        pMsg->SetOpcode(MSG_SHARD_SUSPECTED_USER);
        DWORD dwZero = 0;
        pMsg->WriteBytes(&dwZero, 4);
        pMsg->WriteBytes(&dwCommand, 4);
        pMsg->WriteBytes(&dwCount, 4);
        for (DWORD i = 0; i < dwCount; ++i)
        {
            DWORD dwJID = pvecJID->at(i);
            pMsg->WriteBytes(&dwJID, 4);
            if (dwCommand == 0)
            {
                setSuspected.insert(dwJID);
                LogPrintf(0x2000000, "Send request to register suspected user list (count:%d)",
                          dwCount);
            }
            else if (dwCommand == 1)
            {
                setSuspected.erase(dwJID);
                LogPrintf(0x2000000,
                          "Send request to unregister suspected user list (count:%d)", dwCount);
            }
        }
        CMainProcess::BroadcastToGameServers(pMsg, NULL);
        return;
    }

    if (dwCommand != 2)
        LogPrintf(0x2000000, "Unhandled Detail Command (%d)", dwCommand);
    std::string strList;
    for (std::set<DWORD>::iterator it = setSuspected.begin(); it != setSuspected.end(); ++it)
    {
        char szJID[0x20];
        memset(szJID, 0, sizeof(szJID));
        _itoa_s((int)*it, szJID, sizeof(szJID), 10);
        strList.append(szJID, strlen(szJID));
        strList.append(", ", 2);
        pvecJID->push_back(*it);
    }
    LogPrintf(0x2000000, "Registered suspected user list : %s", strList.c_str());
}

// ---------------------------------------------------------------------------------------------
// Level 3 uniques

struct UNIQUE_SPAWN_SPOT
{
    const char* pszCodeName;
    WORD        wRegionID;
    float       fX;
    float       fY;
    float       fZ;
};

// The spots of the original in their order (one 0x34BB message each).
static const UNIQUE_SPAWN_SPOT s_aUniqueSpawnSpot[] =
{
    { "MOB_CH_TIGERWOMAN_L3", 24225, 971.0f, 695.1f, 143.0f },
    { "MOB_CH_TIGERWOMAN_L3", 23970, 1435.0f, 1057.5f, 1652.0f },
    { "MOB_CH_TIGERWOMAN_L3", 23712, 914.0f, 1368.6f, 924.0f },
    { "MOB_CH_TIGERWOMAN_L3", 24223, 1582.0f, 935.1f, 56.0f },
    { "MOB_CH_TIGERWOMAN_L3", 24478, 379.0f, 549.9f, 96.0f },
    { "MOB_OA_URUCHI_L3", 24468, 1207.0f, 1036.1f, 1640.0f },
    { "MOB_OA_URUCHI_L3", 24211, 1063.0f, 1120.6f, 1234.0f },
    { "MOB_OA_URUCHI_L3", 24722, 13.0f, 885.7f, 661.0f },
    { "MOB_OA_URUCHI_L3", 23447, 365.0f, 409.4f, 1481.0f },
    { "MOB_OA_URUCHI_L3", 23699, 1059.0f, 220.3f, 1590.0f },
    { "MOB_AM_IVY_L3", 25698, 1832.0f, 180.0f, 115.0f },
    { "MOB_AM_IVY_L3", 25954, 1743.0f, 180.0f, 914.0f },
    { "MOB_AM_IVY_L3", 25954, 1609.0f, 180.0f, 1008.0f },
    { "MOB_AM_IVY_L3", 25700, 185.0f, 180.0f, 1430.0f },
    { "MOB_AM_IVY_L3", 25954, 135.0f, 179.0f, 964.0f },
    { "MOB_EU_KERBEROS_L3", 25414, 1570.0f, -0.0f, 1700.0f },
    { "MOB_EU_KERBEROS_L3", 25671, 1578.0f, 0.5f, 1033.0f },
    { "MOB_EU_KERBEROS_L3", 25415, 1047.0f, 172.3f, 347.0f },
    { "MOB_EU_KERBEROS_L3", 25414, 401.0f, 0.0f, 867.0f },
    { "MOB_EU_KERBEROS_L3", 25670, 46.0f, 1.3f, 648.0f },
    { "MOB_KK_ISYUTARU_L3", 22396, 1738.0f, 864.1f, 1729.0f },
    { "MOB_KK_ISYUTARU_L3", 22654, 1732.0f, 800.0f, 1405.0f },
    { "MOB_KK_ISYUTARU_L3", 22656, 1148.0f, 800.0f, 1605.0f },
    { "MOB_KK_ISYUTARU_L3", 23424, 327.0f, 800.0f, 601.0f },
    { "MOB_KK_ISYUTARU_L3", 23422, 715.0f, 800.0f, 1627.0f },
    { "MOB_TK_BONELORD_L3", 27006, 1654.0f, 491.4f, 960.0f },
    { "MOB_TK_BONELORD_L3", 27007, 216.0f, 599.2f, 59.0f },
    { "MOB_TK_BONELORD_L3", 26240, 1767.0f, 80.6f, 1647.0f },
    { "MOB_TK_BONELORD_L3", 26246, 1839.0f, -39.7f, 956.0f },
    { "MOB_TK_BONELORD_L3", 26500, 1285.0f, -59.9f, 591.0f },
    { "MOB_RM_TAHOMET_L3", 23664, 1131.0f, 4207.0f, 852.0f },
    { "MOB_RM_TAHOMET_L3", 23153, 173.0f, 4149.7f, 1131.0f },
    { "MOB_RM_TAHOMET_L3", 22895, 97.0f, 3631.8f, 861.0f },
    { "MOB_RM_TAHOMET_L3", 23149, 1072.0f, 3189.3f, 1153.0f },
    { "MOB_RM_TAHOMET_L3", 23919, 260.0f, 3845.0f, 990.0f },
    { "MOB_TQ_BLACKSNAKE_L3", 32772, 972.0f, -47.0f, -82.0f },
    { "MOB_TQ_BLACKSNAKE_L3", 32772, 15771.0f, -45.2f, -20.0f },
    { "MOB_TQ_BLACKSNAKE_L3", 32772, -14801.0f, -17.5f, 1489.0f },
    { "MOB_TQ_BLACKSNAKE_L3", 32772, 437.0f, -39.0f, -15339.0f },
    { "MOB_TQ_BLACKSNAKE_L3", 32772, -69.0f, -10.4f, 15281.0f },
};

void UniqueSpawn_BroadcastL3Uniques_34BB(BYTE byFlag)
{
    if (byFlag == 0)
    {
        for (int i = 0; i < _countof(s_aUniqueSpawnSpot); ++i)
        {
            const UNIQUE_SPAWN_SPOT& spot = s_aUniqueSpawnSpot[i];
            CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
            pMsg->SetOpcode(MSG_SHARD_UNIQUE_SPAWN);
            *pMsg << spot.pszCodeName;
            pMsg->WriteBytes(&spot.wRegionID, 2);
            pMsg->WriteBytes(&spot.fX, 4);
            pMsg->WriteBytes(&spot.fY, 4);
            pMsg->WriteBytes(&spot.fZ, 4);
            CMainProcess::BroadcastToAllGameServers(pMsg);
        }
    }
    else if (byFlag == 1)
    {
        CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
        pMsg->SetOpcode(MSG_SHARD_RELAY_TO_GLOBAL);
        WORD wOpcode = MSG_SHARD_UNIQUE_SPAWN;
        BYTE byZero = 0;
        pMsg->WriteBytes(&wOpcode, 2);
        pMsg->WriteBytes(&byZero, 1);
        MainProcess_RelayToGlobalManager(pMsg);
    }
    else
    {
        BSAssertFail();
    }
}

// ---------------------------------------------------------------------------------------------
// Punishment

void sPunishment_SetSlot(int nType, const sPunishmentPeriod* pPeriod, sPunishmentSlot* aSlots)
{
    if (nType > 4)
        BSAssertFail();
    // Types below 3 are not checked and land before the list.
    sPunishmentSlot& slot = aSlots[nType - 3];
    slot.period = *pPeriod;
    slot.bSet = 1;
}
