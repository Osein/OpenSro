#include "ShardManager/SiegeFortress.h"

#include <time.h>

#include <list>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSLib/NewSchedule/ScheduleManager.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardSchedule.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    OP_SIEGE_FORTRESS = 0x385F
};

CSiegeFortressMgr* g_pSiegeFortressMgr = NULL;
CSiegeFortressMgr g_SiegeFortressMgr;

CSiegeFortressMgr::CSiegeFortressMgr()
{
    if (g_pSiegeFortressMgr)
        BSAssertFail();
    g_pSiegeFortressMgr = this;
}

CSiegeFortressMgr::~CSiegeFortressMgr()
{
    g_pSiegeFortressMgr = NULL;
}

void CSiegeFortressMgr::InitSiegeScheduleTimes()
{
    if (!g_pMainProcess)
        BSAssertFail();
    SHARD_SIEGE_SCHEDULE* pSiege = MainProcess_GetSiegeSchedule();
    NewSchedule::CScheduleManager* pMgr = &g_ShardScheduleManager;

    // The period is cleared before each use and at the end instead of being destroyed
    // (BUG(FW11-023)).
    NewSchedule::sSchedulePeriod period;
    NewSchedule::ZeroScheduleStruct(period);
    CDateTime now(_time32(NULL));

    std::list<NewSchedule::sSchedulePeriod> lstPrev;
    pMgr->GetPrevPeriods(lstPrev, std::string("SCHEDULE_DEF_SIEGE_PERIOD"), 1, now);
    if (!lstPrev.empty())
        period = lstPrev.front();
    CDateTime(period.tBegin).GetAsSystemTime(pSiege->stPrevSiegeBegin);

    NewSchedule::ZeroScheduleStruct(period);
    std::list<NewSchedule::sSchedulePeriod> lstSiege;
    pMgr->GetNextPeriods(lstSiege, std::string("SCHEDULE_DEF_SIEGE_PERIOD"), 1, now);
    if (!lstSiege.empty())
        period = lstSiege.front();
    CDateTime(period.tBegin + 1800).GetAsSystemTime(pSiege->stSiegeBegin);
    CDateTime(period.tEnd).GetAsSystemTime(pSiege->stSiegeEnd);

    NewSchedule::ZeroScheduleStruct(period);
    std::list<NewSchedule::sSchedulePeriod> lstRequest;
    pMgr->GetNextPeriods(lstRequest, std::string("SCHEDULE_DEF_SIEGE_REQUEST_PERIOD"), 1, now);
    if (!lstRequest.empty())
        period = lstRequest.front();
    CDateTime(period.tBegin).GetAsSystemTime(pSiege->stPeriodBegin);
    CDateTime(period.tEnd).GetAsSystemTime(pSiege->stPeriodEnd);

    NewSchedule::ZeroScheduleStruct(period);
}

void CSiegeFortressMgr::OnMsg_385F(CMsg* pMsg)
{
    BYTE byType;
    *pMsg >> byType;
    switch (byType)
    {
    case 7:
        {
            DWORD dw1;
            WORD w2;
            *pMsg >> dw1;
            *pMsg >> w2;
            CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
            pRelay->SetOpcode(OP_SIEGE_FORTRESS);
            pRelay->WriteBytes(&byType, 1);
            pRelay->WriteBytes(&dw1, 4);
            pRelay->WriteBytes(&w2, 2);
            if (!g_pMainProcess)
                BSAssertFail();
            // BUG(SM-160): the received message is broadcast; the relay built above leaks.
            CMainProcess::BroadcastToGameServers(pMsg, 0);
        }
        break;

    case 8:
        {
            DWORD dw1;
            BYTE by2;
            DWORD dwOldGuildID;
            DWORD dwNewGuildID;
            *pMsg >> dw1;
            *pMsg >> by2;
            *pMsg >> dwOldGuildID;
            *pMsg >> dwNewGuildID;
            if (by2 == 2 && dwOldGuildID != dwNewGuildID)
                GuildManager_ResetSiegeAuthority(dwNewGuildID);
            CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
            pRelay->SetOpcode(OP_SIEGE_FORTRESS);
            pRelay->WriteBytes(&byType, 1);
            pRelay->WriteBytes(&dw1, 4);
            pRelay->WriteBytes(&dwOldGuildID, 4);
            if (!g_pMainProcess)
                BSAssertFail();
            CMainProcess::BroadcastToGameServers(pRelay, 0);
        }
        break;

    case 12:
    case 13:
        {
            DWORD dw1;
            DWORD dw2;
            BYTE by3;
            *pMsg >> dw1;
            *pMsg >> dw2;
            *pMsg >> by3;
            CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
            pRelay->SetOpcode(OP_SIEGE_FORTRESS);
            pRelay->WriteBytes(&byType, 1);
            pRelay->WriteBytes(&dw1, 4);
            pRelay->WriteBytes(&dw2, 4);
            pRelay->WriteBytes(&by3, 1);
            if (!g_pMainProcess)
                BSAssertFail();
            // BUG(SM-160): as above.
            CMainProcess::BroadcastToGameServers(pMsg, 0);
        }
        break;

    case 25:
    case 26:
        {
            CMsg* pRelay = g_pBSNet->AllocMsg(FALSE);
            if (pRelay)
            {
                pRelay->SetOpcode(pMsg->GetOpcode());
                pRelay->WriteBytes(&byType, 1);
                pRelay->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos,
                                   (WORD)(pMsg->m_wWritePos - pMsg->m_wReadPos));
                if (!g_pMainProcess)
                    BSAssertFail();
                CMainProcess::BroadcastToGameServers(pRelay, 0);
            }
            pMsg->SkipRemaining();
        }
        break;

    default:
        BSAssertFail();
        break;
    }
}
