#include "ShardManager/FlagWorld.h"

#include <stdlib.h>

#include <deque>
#include <set>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/PartyManager.h"
#include "ShardManager/ShardScheduleJob.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    OP_FLAGWORLD_NOTICE         = 0x34B1,   // to the game servers
    OP_FLAGWORLD_LEVEL_REQ      = 0x74B7,
    OP_FLAGWORLD_REGISTER_ACK   = 0xB4B0
};

// The first byte of 0x34B1.
enum
{
    FLAGWORLD_JOIN                  = 1,    // JID, world, layer, side, grade
    FLAGWORLD_PARTICIPATION_BEGIN   = 2,    // begin time
    FLAGWORLD_PARTICIPATION_END     = 3,
    FLAGWORLD_TEAMS_DONE            = 4,
    FLAGWORLD_GAME_END              = 5,
    FLAGWORLD_TEAM_LAYER            = 6,    // world, layer
    FLAGWORLD_MEMBER_ADDED          = 7,    // world, layer, JID
    FLAGWORLD_MEMBER_REMOVED        = 11,   // world, layer, JID
    FLAGWORLD_FORCE_STOP            = 12,
    FLAGWORLD_5MIN_LEFT             = 13,
    FLAGWORLD_1MIN_LEFT             = 14,
    FLAGWORLD_RESULT                = 0xFF  // JID, result
};

// Results of 0x34B1 FLAGWORLD_RESULT.
enum
{
    FLAGWORLD_RESULT_BAD_LEVEL      = 5,
    FLAGWORLD_RESULT_NO_TEAM        = 6,
    FLAGWORLD_RESULT_NO_LAYER       = 7,
    FLAGWORLD_RESULT_LEFT           = 20
};

static CMsg* AllocNotice()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_FLAGWORLD_NOTICE);
    return pMsg;
}

static void BroadcastToGameServers(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, 0);
}

static void BroadcastNoticeByte(BYTE byType)
{
    CMsg* pMsg = AllocNotice();
    pMsg->WriteBytes(&byType, 1);
    BroadcastToGameServers(pMsg);
}

// ---------------------------------------------------------------------------------------------
// sFlagWorldTeam

sFlagWorldTeam::sFlagWorldTeam()
{
    m_wLayerID = 0;
    m_wWorldID = 0;
    m_byGrade = 0;
    m_byCount = 0;
}

void sFlagWorldTeam::AddMember(DWORD dwJID)
{
    m_listMember.push_back(dwJID);
    ++m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = FLAGWORLD_MEMBER_ADDED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
}

DWORD sFlagWorldTeam::PopMember()
{
    DWORD dwJID = m_listMember.front();
    m_listMember.pop_front();
    --m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = FLAGWORLD_MEMBER_REMOVED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
    return dwJID;
}

BYTE Arena_GetLevelBracket(BYTE byLevel)
{
    if (byLevel <= 19)
        return 0;
    if ((BYTE)(byLevel - 20) <= 15)
        return 1;
    if ((BYTE)(byLevel - 36) <= 14)
        return 2;
    if ((BYTE)(byLevel - 51) <= 14)
        return 3;
    if ((BYTE)(byLevel - 66) <= 14)
        return 4;
    if ((BYTE)(byLevel - 81) <= 9)
        return 5;
    if (byLevel >= 91)
        return 6;
    BSAssertFail();
    return 0;
}

// ---------------------------------------------------------------------------------------------
// CFlagWorldMgr

CFlagWorldMgr* g_pFlagWorldMgr = NULL;
CFlagWorldMgr g_FlagWorldMgr;

CFlagWorldMgr::CFlagWorldMgr()
{
    if (g_pFlagWorldMgr)
        BSAssertFail();
    g_pFlagWorldMgr = this;
    ResetState();
}

CFlagWorldMgr::~CFlagWorldMgr()
{
    ResetState();
    g_pFlagWorldMgr = NULL;
}

void CFlagWorldMgr::ResetState()
{
    m_nState = 0;
}

static void SendRegisterAck(BYTE byType, DWORD dwJID, BYTE byResult)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_FLAGWORLD_REGISTER_ACK);
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byResult, 1);
    BroadcastToGameServers(pMsg);
}

void CFlagWorldMgr::OnMsg_74B0(CMsg* pMsg)
{
    BYTE byType;
    *pMsg >> byType;
    DWORD dwValue;
    if (byType >= 1 && byType <= 2)
    {
        *pMsg >> dwValue;
        if (!m_nState)
        {
            // Not in the participation phase.
            pMsg->SkipRemaining();
            SendRegisterAck(byType, dwValue, 4);
            return;
        }
    }

    switch (byType)
    {
    case 1:     // register: JID, value
        {
            BYTE byValue = 0;
            *pMsg >> byValue;
            if (m_mapParticipant.find(dwValue) == m_mapParticipant.end())
            {
                m_mapParticipant.insert(ParticipantMap::value_type(dwValue, byValue));
                SendRegisterAck(byType, dwValue, 0);
            }
            else
            {
                SendRegisterAck(byType, dwValue, 2);
            }
        }
        break;

    case 2:     // unregister: JID
        {
            ParticipantMap::iterator it = m_mapParticipant.find(dwValue);
            if (it != m_mapParticipant.end())
            {
                m_mapParticipant.erase(it);
                SendRegisterAck(byType, dwValue, 0);
            }
            else
            {
                SendRegisterAck(byType, dwValue, 3);
            }
        }
        break;

    case 3:
        *pMsg >> dwValue;
        ForceState((int)dwValue);
        break;

    case 8:
    case 9:
        BroadcastNoticeByte(byType);
        break;

    default:
        BSAssertFail();
        break;
    }
}

void CFlagWorldMgr::ForceState(int nState)
{
    switch (nState)
    {
    case 1:
        BeginParticipationAndNotify();
        break;
    case 2:
        EndParticipationAndNotify();
        break;
    case 3:
        RequestParticipantLevels();
        break;
    case 4:
        EndGameAndNotify();
        break;
    case 5:
        LogPrintf(0, "FlagWorld: force stop playing");
        BroadcastNoticeByte(FLAGWORLD_FORCE_STOP);
        break;
    }
}

void CFlagWorldMgr::BeginParticipation(CMsg* pMsg)
{
    if (m_nState)
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    LogPrintf(0, "FlagWorld: Participation started");
    m_nState = 1;
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    BYTE byType = FLAGWORLD_PARTICIPATION_BEGIN;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwTime, 4);
}

void CFlagWorldMgr::EndParticipation(CMsg* pMsg)
{
    if (m_nState != 1)
        return;
    LogPrintf(0, "FlagWorld: Participation ended");
    m_nState = 0;
    BYTE byType = FLAGWORLD_PARTICIPATION_END;
    pMsg->WriteBytes(&byType, 1);
}

void CFlagWorldMgr::Notify5MinBeforeGame(CMsg* pMsg)
{
    LogPrintf(0, "FlagWorld: 5 Minute remains to begin game");
    BYTE byType = FLAGWORLD_5MIN_LEFT;
    pMsg->WriteBytes(&byType, 1);
}

void CFlagWorldMgr::Notify1MinBeforeGame(CMsg* pMsg)
{
    LogPrintf(0, "FlagWorld: 1 Minute remains to begin game");
    BYTE byType = FLAGWORLD_1MIN_LEFT;
    pMsg->WriteBytes(&byType, 1);
}

void CFlagWorldMgr::EndGame(CMsg* pMsg)
{
    LogPrintf(0, "FlagWorld: game ended");
    CloseTeamLayers();
    EndParticipationAndNotify();
    m_mapParticipant.clear();
    BYTE byType = FLAGWORLD_GAME_END;
    pMsg->WriteBytes(&byType, 1);
}

void CFlagWorldMgr::BeginParticipationAndNotify()
{
    if (m_nState)
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    LogPrintf(0, "FlagWorld: Participation started");
    m_nState = 1;
    CMsg* pMsg = AllocNotice();
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    BYTE byType = FLAGWORLD_PARTICIPATION_BEGIN;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwTime, 4);
    BroadcastToGameServers(pMsg);
}

void CFlagWorldMgr::EndParticipationAndNotify()
{
    if (m_nState != 1)
        return;
    LogPrintf(0, "FlagWorld: Participation ended");
    m_nState = 0;
    BroadcastNoticeByte(FLAGWORLD_PARTICIPATION_END);
}

void CFlagWorldMgr::EndGameAndNotify()
{
    LogPrintf(0, "FlagWorld: game ended");
    CloseTeamLayers();
    EndParticipationAndNotify();
    m_mapParticipant.clear();
    BroadcastNoticeByte(FLAGWORLD_GAME_END);
}

void CFlagWorldMgr::CloseTeamLayers()
{
    for (std::vector<sFlagWorldTeam>::iterator it = m_vecTeam.begin(); it != m_vecTeam.end(); ++it)
    {
        CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(it->m_wWorldID);
        if (pWorld && pWorld->LayerAt(it->m_wLayerID))
        {
            CGameWorldMgr::GetInstance();
            CGameWorldMgr::BroadcastClosed(it->m_wWorldID, it->m_wLayerID);
            it->m_wLayerID = 0;
            it->m_wWorldID = 0;
        }
    }
    m_vecTeam.erase(m_vecTeam.begin(), m_vecTeam.end());
}

BOOL CFlagWorldMgr::OpenTeamLayer(sFlagWorldTeam* pTeam)
{
    RefGameWorld* pRef = CReferenceData::GetInstance()->FindGameWorldByCodeName(
        std::string("INS_EVENT_RANDOMMATCH"));
    if (!pRef)
    {
        LogPrintf(0x2000000,
                  "ref flag world data is not exists!! check the gust and service for the data");
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
    param.m_dwDuration = 3600000;
    param.m_dwEmptyRemainTime = 120000;
    param.m_b24 = 0;
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return FALSE;
    }
    GameWorldLayer* pLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!pLayer)
    {
        LogPrintf(0x2000000, "Failed to create new game layer (for FlagWorld)!! error[%d]",
                  byResult);
        return FALSE;
    }
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, pLayer);
    pTeam->m_wWorldID = (WORD)pWorld->m_pRef->ID;
    pTeam->m_wLayerID = pLayer->m_wLayerID;
    return TRUE;
}

void CFlagWorldMgr::RequestParticipantLevels()
{
    std::deque<DWORD> dqJID;
    for (ParticipantMap::iterator it = m_mapParticipant.begin(); it != m_mapParticipant.end(); ++it)
    {
        DWORD dwJID = it->first;
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (pUser && pUser->GetCharID())
            dqJID.push_back(dwJID);
    }

    std::deque<CMsg*> dqMsg;
    std::set<DWORD> setSession;
    CMsg* pMsg = NULL;
    int nInMsg = 0;
    while (!dqJID.empty())
    {
        if (!pMsg)
        {
            pMsg = g_pBSNet->AllocMsg(FALSE);
            pMsg->SetOpcode(OP_FLAGWORLD_LEVEL_REQ);
        }
        DWORD dwJID = dqJID.front();
        pMsg->WriteBytes(&dwJID, 4);
        dqJID.pop_front();

        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (pUser && pUser->GetCharID())
        {
            DWORD dwSessionID = pUser->GetSessionID();
            if (setSession.find(dwSessionID) == setSession.end())
                setSession.insert(pUser->GetSessionID());
            ++nInMsg;
            m_mapPending[dwJID] = 0;
            if (nInMsg == 100 || dqJID.empty())
            {
                dqMsg.push_back(pMsg);
                pMsg = NULL;
                nInMsg = 0;
                continue;
            }
        }
        // BUG(SM-161): a participant who went offline since the list was made still has his JID
        // written; when he is the last one the message is neither sent nor released.
        if (dqJID.empty() && pMsg)
            BSAssertFail();
    }

    // Every pending participant waits for an answer of each game server.
    for (ParticipantMap::iterator it = m_mapPending.begin(); it != m_mapPending.end(); ++it)
        it->second = (BYTE)setSession.size();

    while (!dqMsg.empty())
    {
        CMsg* pSend = dqMsg.front();
        for (std::set<DWORD>::iterator it = setSession.begin(); it != setSession.end(); ++it)
            g_pBSNet->SendMsg(*it, pSend);
        g_pBSNet->ReleaseMsg(pSend);
        dqMsg.pop_front();
    }
}

void CFlagWorldMgr::OnParticipantLevels(CMsg* pMsg)
{
    std::deque<DWORD> dqLeft;
    BOOL bAllAnswered = TRUE;
    while (pMsg->GetUnreadSize() >= 5)
    {
        DWORD dwJID = 0;
        BYTE byLevel = 0;
        *pMsg >> dwJID;
        *pMsg >> byLevel;
        ParticipantMap::iterator it = m_mapPending.find(dwJID);
        if (it == m_mapPending.end())
            continue;
        if (it->second != 0)
            --it->second;
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (pUser && pUser->GetCharID())
        {
            if (byLevel != 0)
                continue;
            dqLeft.push_back(dwJID);
        }
        m_mapPending.erase(it);
    }

    ParticipantMap::iterator itPending;
    for (itPending = m_mapPending.begin(); itPending != m_mapPending.end(); ++itPending)
    {
        if (itPending->second != 0)
        {
            bAllAnswered = FALSE;
            break;
        }
    }

    if (bAllAnswered)
    {
        // The participants that are still pending keep the value of their registration.
        for (itPending = m_mapPending.begin(); itPending != m_mapPending.end(); ++itPending)
        {
            ParticipantMap::iterator it = m_mapParticipant.find(itPending->first);
            if (it == m_mapParticipant.end())
                itPending->second = 0;
            else
                itPending->second = it->second;
        }
        m_mapParticipant = m_mapPending;
        m_mapPending.clear();
    }

    while (!dqLeft.empty())
    {
        DWORD dwJID = dqLeft.front();
        CMsg* pNotice = AllocNotice();
        BYTE byType = FLAGWORLD_RESULT;
        BYTE byResult = FLAGWORLD_RESULT_LEFT;
        pNotice->WriteBytes(&byType, 1);
        pNotice->WriteBytes(&dwJID, 4);
        pNotice->WriteBytes(&byResult, 1);
        BroadcastToGameServers(pNotice);
        dqLeft.pop_front();
    }

    if (bAllAnswered)
        StartGame();
}

namespace
{

// A participant of the team building: the low dword the JID, the high dword the level.
struct FlagWorldEntry
{
    DWORD   dwJID;
    DWORD   dwLevel;
};

}

// SortPred_ByteAt4Less (0x669bf0): the higher level first.
static bool SortPred_LevelGreater(const FlagWorldEntry& a, const FlagWorldEntry& b)
{
    return (BYTE)b.dwLevel < (BYTE)a.dwLevel;
}

void CFlagWorldMgr::StartGame()
{
    LogPrintf(0, "FlagWorld: game started");
    std::map<DWORD, DWORD> mapResult;
    std::list<FlagWorldEntry> listEntry;

    for (ParticipantMap::iterator it = m_mapParticipant.begin(); it != m_mapParticipant.end(); ++it)
    {
        DWORD dwJID = it->first;
        BYTE byLevel = it->second;
        if (!GetSingleton_CClientManager()->FindUserByJID(dwJID))
            continue;
        if (Arena_GetLevelBracket(byLevel) && Arena_GetLevelBracket(byLevel) <= 6)
        {
            FlagWorldEntry entry;
            entry.dwJID = dwJID;
            entry.dwLevel = byLevel;
            listEntry.push_back(entry);
        }
        else
        {
            mapResult[dwJID] = FLAGWORLD_RESULT_BAD_LEVEL;
        }
    }
    listEntry.sort(SortPred_LevelGreater);

    sFlagWorldTeam team;
    for (std::list<FlagWorldEntry>::iterator it = listEntry.begin(); it != listEntry.end(); ++it)
    {
        DWORD dwJID = it->dwJID;
        BYTE byLevel = (BYTE)it->dwLevel;
        if (team.m_byCount == 16)
        {
            m_vecTeam.push_back(team);
            team.m_byCount = 0;
            team.m_byGrade = 0;
            team.m_wWorldID = 0;
            team.m_wLayerID = 0;
            team.m_listMember.clear();
        }

        CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(team.m_wWorldID);
        if (!pWorld || !pWorld->LayerAt(team.m_wLayerID))
        {
            if (!OpenTeamLayer(&team))
            {
                mapResult[dwJID] = FLAGWORLD_RESULT_NO_LAYER;
                continue;
            }
        }

        // Sent again for every member.
        CMsg* pMsg = AllocNotice();
        BYTE byType = FLAGWORLD_TEAM_LAYER;
        pMsg->WriteBytes(&byType, 1);
        pMsg->WriteBytes(&team.m_wWorldID, 2);
        pMsg->WriteBytes(&team.m_wLayerID, 2);
        BroadcastToGameServers(pMsg);

        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (pUser && pUser->GetCharID())
        {
            int nGrade = (int)Arena_GetLevelBracket(byLevel) - 1;
            if (nGrade > (int)team.m_byGrade)
                team.m_byGrade = (BYTE)(Arena_GetLevelBracket(byLevel) - 1);
            team.AddMember(dwJID);
        }
    }
    if (team.m_listMember.size())
    {
        // An odd member count leaves the last member out.
        if (team.m_listMember.size() & 1)
        {
            DWORD dwJID = team.PopMember();
            mapResult[dwJID] = FLAGWORLD_RESULT_NO_TEAM;
        }
        m_vecTeam.push_back(team);
    }

    for (std::vector<sFlagWorldTeam>::iterator itTeam = m_vecTeam.begin();
         itTeam != m_vecTeam.end(); ++itTeam)
    {
        BOOL bPlay = TRUE;
        BYTE byMinMember = 0;
        switch (itTeam->m_byGrade)
        {
        case 0:
        case 1:
            byMinMember = 4;
            break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            byMinMember = 8;
            break;
        default:
            BSAssertFail();
            break;
        }
        if (itTeam->m_listMember.size() < byMinMember)
            bPlay = FALSE;

        // The members alternate between the sides in pairs, starting on a random side.
        BYTE bySide = (BYTE)(rand() % 2);
        BYTE byPrevSide = bySide;
        for (std::list<DWORD>::iterator it = itTeam->m_listMember.begin();
             it != itTeam->m_listMember.end(); ++it)
        {
            if (!bPlay)
            {
                mapResult[*it] = FLAGWORLD_RESULT_NO_TEAM;
                continue;
            }
            DWORD dwJID = *it;
            CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
            if (!pUser)
                continue;
            if (pUser->GetPartyMemberInfo().m_dwPartyID)
                GetSingleton_CPartyManager()->RemoveMember(pUser->GetPartyMemberInfo().m_dwPartyID,
                                                           pUser->GetJID(), 2);

            CMsg* pMsg = AllocNotice();
            BYTE byGrade = (BYTE)(itTeam->m_byGrade + 1);
            BYTE byType = FLAGWORLD_JOIN;
            pMsg->WriteBytes(&byType, 1);
            pMsg->WriteBytes(&dwJID, 4);
            pMsg->WriteBytes(&itTeam->m_wWorldID, 2);
            pMsg->WriteBytes(&itTeam->m_wLayerID, 2);
            pMsg->WriteBytes(&bySide, 1);
            pMsg->WriteBytes(&byGrade, 1);
            if (bySide == byPrevSide)
                bySide = (byPrevSide == 0);
            else
                byPrevSide = bySide;
            BroadcastToGameServers(pMsg);
        }
    }

    for (std::map<DWORD, DWORD>::iterator it = mapResult.begin(); it != mapResult.end(); ++it)
    {
        DWORD dwJID = it->first;
        BYTE byResult = (BYTE)it->second;
        CMsg* pMsg = AllocNotice();
        BYTE byType = FLAGWORLD_RESULT;
        pMsg->WriteBytes(&byType, 1);
        pMsg->WriteBytes(&dwJID, 4);
        pMsg->WriteBytes(&byResult, 1);
        BroadcastToGameServers(pMsg);
    }

    BroadcastNoticeByte(FLAGWORLD_TEAMS_DONE);
}

// ---------------------------------------------------------------------------------------------
// The flag world schedule jobs

BOOL CShard_FlagWorldParticipation::OnBegin()           // 0x66d230
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    BeginNotify(NULL);
    if (!g_pFlagWorldMgr)
        BSAssertFail();
    g_pFlagWorldMgr->BeginParticipation(m_pNotifyMsg);
    BroadcastNotify();
    LogSchedule("Scheduleing : FlagWorld Request is Start ........!");
    return TRUE;
}

void CShard_FlagWorldParticipation::OnEnd(CDateTime& /*now*/)  // 0x66d3c0
{
    BeginNotify(NULL);
    if (!g_pFlagWorldMgr)
        BSAssertFail();
    g_pFlagWorldMgr->EndParticipation(m_pNotifyMsg);
    BroadcastNotify();
    LogSchedule("Scheduleing : FlagWorld Request is End ........!");
}

BOOL CSahrd_FlagWorld::OnBegin()                        // 0x66d520
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    DWORD dwBegin = m_Info.dwBegin;
    m_dwBegin = dwBegin;
    m_dwOneMinuteLeft = dwBegin + 240;
    m_dwGameStart = dwBegin + 300;
    BeginNotify(NULL);
    if (!g_pFlagWorldMgr)
        BSAssertFail();
    g_pFlagWorldMgr->Notify5MinBeforeGame(m_pNotifyMsg);
    BroadcastNotify();
    m_byStage = 0;
    LogSchedule("Scheduleing : FlagWorld is Start Before 5 Min ......!");
    return TRUE;
}

BOOL CSahrd_FlagWorld::OnTick(CDateTime& now)           // 0x66d6c0
{
    // The notices of the tick go out as state 2.
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    if ((DWORD)now.GetTime() > m_dwOneMinuteLeft && !m_byStage)
    {
        BeginNotify(&info);
        if (!g_pFlagWorldMgr)
            BSAssertFail();
        g_pFlagWorldMgr->Notify1MinBeforeGame(m_pNotifyMsg);
        BroadcastNotify();
        m_byStage = 1;
        LogPrintf(ShardScheduleLogFlags(),
                  "Scheduleing : FlagWorld is Start Before 1 Min ......!");
    }
    if ((DWORD)now.GetTime() > m_dwGameStart && m_byStage == 1)
    {
        if (!g_pFlagWorldMgr)
            BSAssertFail();
        g_pFlagWorldMgr->RequestParticipantLevels();
        m_byStage = 2;
        LogPrintf(ShardScheduleLogFlags(),
                  "Scheduleing : FlagWorld is Start ......!");
    }
    return TRUE;
}

void CSahrd_FlagWorld::OnEnd(CDateTime& /*now*/)       // 0x66d810
{
    BeginNotify(NULL);
    if (!g_pFlagWorldMgr)
        BSAssertFail();
    g_pFlagWorldMgr->EndGame(m_pNotifyMsg);
    BroadcastNotify();
    LogSchedule("Scheduleing : FlagWorld is End ......!");
}
