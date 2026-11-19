#include "ShardManager/BattleArena.h"

#include <stdlib.h>

#include <string>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/FlagWorld.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/PartyManager.h"
#include "ShardManager/ShardScheduleJob.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    OP_BATTLEARENA_NOTICE       = 0x34D2,   // to the game servers
    OP_BATTLEARENA_CHECK        = 0x74D1,   // to one game server, answered with 0xB4D1
    OP_BATTLEARENA_REGISTER_ACK = 0xB4D0
};

// The first byte of 0x34D2.
enum
{
    BATTLEARENA_PARTICIPATION_BEGIN = 2,    // kind, begin time, active match flags
    BATTLEARENA_PARTICIPATION_END   = 3,    // kind
    BATTLEARENA_GAME_END            = 5,    // kind
    BATTLEARENA_MEMBER_ADDED        = 7,    // world, layer, JID
    BATTLEARENA_PARTICIPATION_TIME  = 8,    // JID, ms
    BATTLEARENA_END_PLAY            = 9,    // kind
    BATTLEARENA_MEMBER_REMOVED      = 11,   // world, layer, JID
    BATTLEARENA_5MIN_LEFT           = 13,   // kind
    BATTLEARENA_1MIN_LEFT           = 14,   // kind
    BATTLEARENA_LETS_PARTICIPATE    = 15,   // kind
    BATTLEARENA_STOP_RESURRECT      = 18,   // kind
    BATTLEARENA_KICK                = 0xFF  // JID, reason
};

// Errors of 0xB4D0.
enum
{
    BATTLEARENA_ERR_INVALID         = 1,
    BATTLEARENA_ERR_REGISTERED      = 2,
    BATTLEARENA_ERR_NOT_REGISTERED  = 3,
    BATTLEARENA_ERR_NOT_OPEN        = 4,
    BATTLEARENA_ERR_PARTY_SIZE      = 14,
    BATTLEARENA_ERR_GUILD_LIST      = 15
};

static CMsg* AllocNotice()
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_NOTICE);
    return pMsg;
}

static void BroadcastToGameServers(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, 0);
}

static void WriteTypeAndKind(CMsg* pMsg, BYTE byType, BYTE byKind)
{
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&byKind, 1);
}

// ---------------------------------------------------------------------------------------------
// sBattleArenaTeam

sBattleArenaTeam::sBattleArenaTeam()
{
    m_wWorldID = 0;
    m_wLayerID = 0;
    m_byCount = 0;
    m_byBracket = 0;
    m_dwLevelSum = 0;
}

void sBattleArenaTeam::AddMember(DWORD dwJID)
{
    m_listMember.push_back(dwJID);
    ++m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_MEMBER_ADDED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
}

DWORD sBattleArenaTeam::PopMember()
{
    DWORD dwJID = m_listMember.front();
    m_listMember.pop_front();
    --m_byCount;
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_MEMBER_REMOVED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&m_wWorldID, 2);
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&dwJID, 4);
    BroadcastToGameServers(pMsg);
    return dwJID;
}

// ---------------------------------------------------------------------------------------------
// CBattleArenaMgr

CBattleArenaMgr* g_pBattleArenaMgr = NULL;
CBattleArenaMgr g_BattleArenaMgr;

CBattleArenaMgr::CBattleArenaMgr()
{
    if (g_pBattleArenaMgr)
        BSAssertFail();
    g_pBattleArenaMgr = this;
    Reset();
}

CBattleArenaMgr::~CBattleArenaMgr()
{
    Clear();
    g_pBattleArenaMgr = NULL;
}

void CBattleArenaMgr::Reset()
{
    for (int i = 0; i < BATTLEARENA_KIND_COUNT; ++i)
    {
        m_adwParticipation[i] = 0;
        m_abClosed[i] = 1;
    }
    m_adwMatchActive[0] = 0;
    m_adwMatchActive[1] = 0;
    m_adwMatchActive[2] = 0;
}

void CBattleArenaMgr::Clear()
{
    for (int i = 0; i < BATTLEARENA_KIND_COUNT; ++i)
        m_adwParticipation[i] = 0;
    m_adwMatchActive[0] = 0;
    m_adwMatchActive[1] = 0;
    m_adwMatchActive[2] = 0;
}

WORD CBattleArenaMgr::GetActiveMatchFlags() const
{
    WORD wFlags = 0;
    if (m_adwMatchActive[0])
        wFlags = 0x10;
    if (m_adwMatchActive[1])
        wFlags |= 0x20;
    if (m_adwMatchActive[2])
        wFlags |= 0x40;
    return wFlags;
}

void CBattleArenaMgr::OnMsg_74D0(CMsg* pMsg)
{
    BYTE byType;
    *pMsg >> byType;
    switch (byType)
    {
    case 1:
    case 2:
        OnRegisterReq(pMsg, byType);
        break;

    case 3:     // GM commands
        {
            BYTE byEvent;
            *pMsg >> byEvent;
            if (byEvent == 6)
            {
                OnRegisterReq(pMsg, 1);
            }
            else if (byEvent == 7)
            {
                OnRegisterReq(pMsg, 2);
            }
            else
            {
                BYTE byKind = pMsg->ReadForwardOnly<BYTE>();
                OnScheduleEvent(byKind, byEvent);
            }
        }
        break;

    case 8:     // the participation time
        {
            DWORD dwJID;
            *pMsg >> dwJID;
            CMsg* pReply = AllocNotice();
            BYTE byReply = BATTLEARENA_PARTICIPATION_TIME;
            pReply->WriteBytes(&byReply, 1);
            pReply->WriteBytes(&dwJID, 4);
            pReply->WriteBytes(&m_dwParticipationTime, 4);
            BroadcastToGameServers(pReply);
        }
        break;

    case 9:
        {
            BYTE byKind;
            *pMsg >> byKind;
            SendEndPlay(byKind);
        }
        break;

    case 0x11:
        {
            BYTE byKind;
            *pMsg >> byKind;
            CloseArenaOnce(byKind);
        }
        break;
    }
}

void CBattleArenaMgr::OnRegisterReq(CMsg* pMsg, BYTE byAction)
{
    DWORD dwJID;
    BYTE byKind;
    BYTE byMatch;
    *pMsg >> dwJID;
    *pMsg >> byKind;
    *pMsg >> byMatch;

    BYTE byError = 0;
    if (byKind >= BATTLEARENA_KIND_COUNT || byMatch >= BATTLEARENA_MATCH_COUNT)
        byError = BATTLEARENA_ERR_INVALID;
    else if (!m_adwParticipation[byKind] || !m_adwMatchActive[byMatch])
        byError = BATTLEARENA_ERR_NOT_OPEN;
    else
    {
        if (!g_pMainProcess)
            BSAssertFail();
        if (byMatch <= 2 && !MainProcess_IsArenaMatchOpen(byMatch))
            byError = BATTLEARENA_ERR_INVALID;
    }
    if (byError)
    {
        pMsg->SkipRemaining();
        SendRegisterAck_Error(byAction, dwJID, byError);
        return;
    }

    if (byAction == 2)
    {
        OnUnregisterReq(byKind, dwJID, byMatch);
        return;
    }
    if (byAction != 1)
        return;

    switch (byKind)
    {
    case 0:     // random: value
        {
            BYTE byValue = 0;
            byValue = pMsg->ReadForwardOnly<BYTE>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            m_amapRandom[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;

    case 1:     // party: party ID
        {
            DWORD dwPartyID = pMsg->ReadForwardOnly<DWORD>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            Party* pParty = GetSingleton_CPartyManager()->FindParty(dwPartyID);
            if (!pParty)
            {
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_INVALID);
                break;
            }
            if (pParty->GetMemberCount() < 5)
            {
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_PARTY_SIZE);
                break;
            }
            m_amapParty[byMatch].insert(DwordMap::value_type(dwJID, dwPartyID));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;

    case 2:     // guild: guild ID and the JIDs of the members (up to 16)
        {
            DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            if (pMsg->GetUnreadSize() < 40)
            {
                pMsg->SkipRemaining();
                SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_GUILD_LIST);
                break;
            }
            m_amapGuild[byMatch].insert(DwordMap::value_type(dwJID, dwGuildID));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);

            std::list<DWORD> listMember;
            BYTE byCount = 0;
            if (pMsg->GetUnreadSize() >= 4)
            {
                for (;;)
                {
                    DWORD dwMember = pMsg->ReadForwardOnly<DWORD>();
                    ++byCount;
                    listMember.push_back(dwMember);
                    if (byCount >= 16)
                    {
                        pMsg->SkipRemaining();
                        break;
                    }
                    if (pMsg->GetUnreadSize() < 4)
                        break;
                }
            }
            m_amapGuildMemberList[byMatch].insert(
                MemberListMap::value_type(dwGuildID, listMember));
        }
        break;

    case 3:     // job: value, job type
        {
            BYTE byValue = pMsg->ReadForwardOnly<BYTE>();
            BYTE byJob = pMsg->ReadForwardOnly<BYTE>();
            if (IsUserRegistered(byMatch, byKind, dwJID))
                break;
            if (byJob == 1 || byJob == 3)
                m_amapJobB[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            else
                m_amapJobA[byMatch].insert(ByteMap::value_type(dwJID, byValue));
            SendRegisterAck_OK(1, dwJID, byKind, byMatch);
        }
        break;
    }
}

BOOL CBattleArenaMgr::IsUserRegistered(BYTE byMatch, BYTE byKind, DWORD dwJID)
{
    bool bFound;
    switch (byKind)
    {
    case 0:
        bFound = m_amapRandom[byMatch].find(dwJID) != m_amapRandom[byMatch].end();
        break;
    case 1:
        bFound = m_amapParty[byMatch].find(dwJID) != m_amapParty[byMatch].end();
        break;
    case 2:
        bFound = m_amapGuild[byMatch].find(dwJID) != m_amapGuild[byMatch].end();
        break;
    case 3:
        bFound = m_amapJobB[byMatch].find(dwJID) != m_amapJobB[byMatch].end()
                 || m_amapJobA[byMatch].find(dwJID) != m_amapJobA[byMatch].end();
        break;
    default:
        return FALSE;
    }
    if (!bFound)
        return FALSE;
    SendRegisterAck_Error(1, dwJID, BATTLEARENA_ERR_REGISTERED);
    return TRUE;
}

BOOL CBattleArenaMgr::OnUnregisterReq(BYTE byKind, DWORD dwJID, BYTE byMatch)
{
    switch (byKind)
    {
    case 0:
        {
            ByteMap::iterator it = m_amapRandom[byMatch].find(dwJID);
            if (it == m_amapRandom[byMatch].end())
                break;
            m_amapRandom[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 0, byMatch);
            return TRUE;
        }

    case 1:
        {
            DwordMap::iterator it = m_amapParty[byMatch].find(dwJID);
            if (it == m_amapParty[byMatch].end())
                break;
            m_amapParty[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 1, byMatch);
            return TRUE;
        }

    case 2:
        {
            DwordMap::iterator it = m_amapGuild[byMatch].find(dwJID);
            if (it == m_amapGuild[byMatch].end())
                break;
            DWORD dwGuildID = it->second;
            m_amapGuild[byMatch].erase(it);
            SendRegisterAck_OK(2, dwJID, 2, byMatch);
            MemberListMap::iterator itList = m_amapGuildMemberList[byMatch].find(dwGuildID);
            if (itList != m_amapGuildMemberList[byMatch].end())
            {
                itList->second.clear();
                m_amapGuildMemberList[byMatch].erase(itList);
            }
            else
            {
                BSAssertFail();
            }
            return TRUE;
        }

    case 3:
        {
            ByteMap::iterator it = m_amapJobB[byMatch].find(dwJID);
            if (it != m_amapJobB[byMatch].end())
            {
                m_amapJobB[byMatch].erase(it);
            }
            else
            {
                it = m_amapJobA[byMatch].find(dwJID);
                if (it == m_amapJobA[byMatch].end())
                    break;
                m_amapJobA[byMatch].erase(it);
            }
            SendRegisterAck_OK(2, dwJID, 3, byMatch);
            return TRUE;
        }
    }
    SendRegisterAck_Error(2, dwJID, BATTLEARENA_ERR_NOT_REGISTERED);
    return FALSE;
}

void CBattleArenaMgr::OnScheduleEvent(BYTE byKind, BYTE byEvent)
{
    switch (byEvent)
    {
    case 1:
        m_dwParticipationTime = 1200000;
        SendParticipationStarted(byKind);
        break;
    case 2:
        SendParticipationEnded(byKind);
        break;
    case 3:
        CheckAndGo(byKind);
        break;
    case 4:
        SendEndPlay(byKind);
        break;
    case 5:
        OnGameEnded(byKind);
        break;
    }
}

void CBattleArenaMgr::SendRegisterAck_OK(BYTE byAction, DWORD dwJID, BYTE byKind, BYTE byMatch)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_REGISTER_ACK);
    BYTE byResult = 0;
    pMsg->WriteBytes(&byAction, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byResult, 1);
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&byMatch, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendRegisterAck_Error(BYTE byAction, DWORD dwJID, BYTE byError)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_BATTLEARENA_REGISTER_ACK);
    pMsg->WriteBytes(&byAction, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byError, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendKickUser(DWORD dwJID, BYTE byReason)
{
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_KICK;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&byReason, 1);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendParticipationStarted(BYTE byKind)
{
    if (m_adwParticipation[byKind])
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    LogPrintf(0, "BattleArena: Participation started : %d", byKind);
    m_adwParticipation[byKind] = 1;
    CMsg* pMsg = AllocNotice();
    WORD wFlags = GetActiveMatchFlags();
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_BEGIN, byKind);
    pMsg->WriteBytes(&dwTime, 4);
    pMsg->WriteBytes(&wFlags, 2);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendParticipationEnded(BYTE byKind)
{
    if (m_adwParticipation[byKind] != 1)
        return;
    LogPrintf(0, "BattleArena: Participation ended : %d", byKind);
    m_adwParticipation[byKind] = 0;
    CMsg* pMsg = AllocNotice();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_END, byKind);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::SendEndPlay(BYTE byKind)
{
    LogPrintf(0, "BattleArena: End Play %d", byKind);
    CMsg* pMsg = AllocNotice();
    WriteTypeAndKind(pMsg, BATTLEARENA_END_PLAY, byKind);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::WriteParticipationStarted(CMsg* pMsg, BYTE byKind)
{
    if (m_adwParticipation[byKind])
        return;
    m_dtParticipationBegin = CDateTime::GetCurrent();
    m_adwParticipation[byKind] = 1;
    WORD wFlags = GetActiveMatchFlags();
    DWORD dwTime = (DWORD)m_dtParticipationBegin.GetTime();
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_BEGIN, byKind);
    pMsg->WriteBytes(&dwTime, 4);
    pMsg->WriteBytes(&wFlags, 2);
    LogPrintf(0, "BattleArena: Participation started : %d", byKind);
}

void CBattleArenaMgr::WriteParticipationEnded(CMsg* pMsg, BYTE byKind)
{
    if (m_adwParticipation[byKind] != 1)
        return;
    m_adwParticipation[byKind] = 0;
    WriteTypeAndKind(pMsg, BATTLEARENA_PARTICIPATION_END, byKind);
    LogPrintf(0, "BattleArena: Participation ended : %d", byKind);
}

void CBattleArenaMgr::WriteLetsParticipate(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_LETS_PARTICIPATE, byKind);
    LogPrintf(0, "BattleArena: Let's Participate Battle Arena %d!", byKind);
}

void CBattleArenaMgr::Write5MinRemain(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_5MIN_LEFT, byKind);
    LogPrintf(0, "BattleArena: 5 Minute remains to begin game : %d", byKind);
}

void CBattleArenaMgr::Write1MinRemain(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_1MIN_LEFT, byKind);
    LogPrintf(0, "BattleArena: 1 Minute remains to begin game : %d", byKind);
}

void CBattleArenaMgr::WriteEndPlay(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_END_PLAY, byKind);
    LogPrintf(0, "BattleArena: End Play %d", byKind);
}

void CBattleArenaMgr::WriteStopResurrect(CMsg* pMsg, BYTE byKind)
{
    WriteTypeAndKind(pMsg, BATTLEARENA_STOP_RESURRECT, byKind);
    LogPrintf(0, "BattleArena: Stop Resurrect %d", byKind);
}

void CBattleArenaMgr::OnScheduledGameEnd(CMsg* pMsg, BYTE byKind)
{
    SendParticipationEnded(byKind);
    ClearRegistrations(byKind);
    WriteTypeAndKind(pMsg, BATTLEARENA_GAME_END, byKind);
    LogPrintf(0, "BattleArena: Game Ended : %d", byKind);
}

void CBattleArenaMgr::OnGameEnded(BYTE byKind)
{
    LogPrintf(0, "BattleArena: Game Ended : %d", byKind);
    SendParticipationEnded(byKind);
    ClearRegistrations(byKind);
    CMsg* pMsg = AllocNotice();
    WriteTypeAndKind(pMsg, BATTLEARENA_GAME_END, byKind);
    BroadcastToGameServers(pMsg);
}

void CBattleArenaMgr::ClearRegistrations(BYTE byKind)
{
    int i;
    switch (byKind)
    {
    case 0:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
            m_amapRandom[i].clear();
        break;
    case 1:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapParty[i].clear();
            m_amapPartyOfUser[i].clear();
            m_amapPartyLevel[i].clear();
            m_alistParty[i].clear();
            for (GroupMap::iterator it = m_amapPartyMember[i].begin();
                 it != m_amapPartyMember[i].end(); ++it)
                it->second.clear();
            m_amapPartyMember[i].clear();
        }
        break;
    case 2:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapGuild[i].clear();
            m_amapGuildOfUser[i].clear();
            m_amapGuildLevel[i].clear();
            m_alistGuild[i].clear();
            for (GroupMap::iterator it = m_amapGuildMember[i].begin();
                 it != m_amapGuildMember[i].end(); ++it)
                it->second.clear();
            m_amapGuildMember[i].clear();
            m_amapGuildMemberList[i].clear();
        }
        break;
    case 3:
        for (i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
        {
            m_amapJobB[i].clear();
            m_amapJobA[i].clear();
        }
        break;
    }
}

RefGameWorld* CBattleArenaMgr::GetRefGameWorld(BYTE byMatchType, BYTE byMatch)
{
    static const char* const s_apszWorld[3][3] =
    {
        { "INS_ARENA_OCCUPY_8", "INS_ARENA_OCCUPY_16", "INS_ARENA_OCCUPY_32" },
        { "INS_ARENA_SCORE_8", "INS_ARENA_SCORE_16", "INS_ARENA_SCORE_32" },
        { "INS_ARENA_FLAG_8", "INS_ARENA_FLAG_16", "INS_ARENA_FLAG_32" }
    };
    if (byMatchType > 2 || byMatch < 1 || byMatch > 3)
        return NULL;
    return CReferenceData::GetInstance()->FindGameWorldByCodeName(
        std::string(s_apszWorld[byMatchType][byMatch - 1]));
}

BOOL CBattleArenaMgr::OpenArenaLayer(sBattleArenaTeam* pTeam, BYTE byMatchType, BYTE byMatch)
{
    RefGameWorld* pRef = GetRefGameWorld(byMatchType, byMatch);
    if (!pRef)
    {
        LogPrintf(0x2000000, "BattleArena: RefGameWorld is NULL %d %d", byMatchType, byMatch);
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
        LogPrintf(0x2000000, "Failed to create new game layer (for BattleArena)!! error[%d]",
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

void CBattleArenaMgr::CloseArenaLayers(BYTE byKind)
{
    for (int i = 0; i < BATTLEARENA_MATCH_COUNT; ++i)
    {
        TeamVector& vecTeam = m_avecTeam[byKind][i];
        for (TeamVector::iterator it = vecTeam.begin(); it != vecTeam.end(); ++it)
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
        vecTeam.erase(vecTeam.begin(), vecTeam.end());
    }
}

void CBattleArenaMgr::CloseArenaOnce(BYTE byKind)
{
    if (m_abClosed[byKind])
        return;
    CloseArenaLayers(byKind);
    m_abClosed[byKind] = 1;
}

void CBattleArenaMgr::CheckAndGo(BYTE byKind)
{
    LogPrintf(0, "BattleArena: CheckAndGo %d", byKind);
    switch (byKind)
    {
    case 0:
        CheckAndGoRandom();
        break;
    case 1:
        CheckAndGoParty();
        break;
    case 2:
        CheckAndGoGuild();
        break;
    case 3:
        CheckAndGoJob();
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// The battle arena schedule jobs

static CBattleArenaMgr* GetBattleArenaMgr()
{
    if (!g_pBattleArenaMgr)
        BSAssertFail();
    return g_pBattleArenaMgr;
}

BOOL CShard_BattleArenaScheduleParticipation::OnBegin() // 0x677e50
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    m_dwRunning = 1;
    m_dwNoticeTick = GetTickCount();
    BeginNotify(NULL);
    GetBattleArenaMgr()->WriteParticipationStarted(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) Request is Start ......!", m_byKind);
    return TRUE;
}

BOOL CShard_BattleArenaScheduleParticipation::OnTick(CDateTime& /*now*/)  // 0x678000
{
    if (!m_dwRunning)
        return TRUE;
    // The notices of the tick go out as state 2.
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    DWORD dwTick = GetTickCount();
    if (dwTick - m_dwNoticeTick > 600000)
    {
        BeginNotify(&info);
        GetBattleArenaMgr();
        CBattleArenaMgr::WriteLetsParticipate(m_pNotifyMsg, m_byKind);
        BroadcastNotify();
        m_dwNoticeTick = dwTick;
    }
    return TRUE;
}

void CShard_BattleArenaScheduleParticipation::OnEnd(CDateTime& /*now*/)   // 0x6780e0
{
    m_dwRunning = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr()->WriteParticipationEnded(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) Request is End ......!", m_byKind);
}

BOOL CShard_BattleArenaSchedule::OnBegin()              // 0x6782c0
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    DWORD dwBegin = m_Info.dwBegin;
    m_dwBegin = dwBegin;
    m_dwOneMinuteLeft = dwBegin + 240;
    m_dwGameStart = dwBegin + 300;
    m_byStage = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr();
    CBattleArenaMgr::Write5MinRemain(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) is Start Before 5 Min......!", m_byKind);
    return TRUE;
}

BOOL CShard_BattleArenaSchedule::OnTick(CDateTime& now) // 0x678480
{
    // The notices of the tick go out as state 2.
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    if ((DWORD)now.GetTime() > m_dwOneMinuteLeft && !m_byStage)
    {
        BeginNotify(&info);
        GetBattleArenaMgr();
        CBattleArenaMgr::Write1MinRemain(m_pNotifyMsg, m_byKind);
        BroadcastNotify();
        m_byStage = 1;
        LogPrintf(ShardScheduleLogFlags(),
                  "Scheduleing : BattleArena(%d) is Start Before 1 Min......!", m_byKind);
    }
    DWORD dwNow = (DWORD)now.GetTime();
    if (dwNow > m_dwGameStart && m_byStage == 1)
    {
        m_dwRunning = 1;
        m_bEndPlaySent = 0;
        m_dwCheckInterval = 60000;
        m_dwCheckTick = GetTickCount();
        // The play time: ms from the game start to the end of the period.
        GetBattleArenaMgr()->SetParticipationTime(1000 * (info.dwEnd - m_dwGameStart));
        GetBattleArenaMgr()->CheckAndGo(m_byKind);
        m_byStage = 2;
        LogPrintf(ShardScheduleLogFlags(),
                  "Scheduleing : BattleArena(%d) is Start ......!", m_byKind);
    }
    // The check tick is not renewed: once the interval has passed, every tick checks.
    if (m_dwRunning == 1 && m_byStage == 2 && !m_bEndPlaySent
        && GetTickCount() - m_dwCheckTick > m_dwCheckInterval)
    {
        DWORD dwRemain = GetRemainSeconds(now);
        if (dwRemain < 60)
            m_dwCheckInterval = 1000;
        if (!m_bStopResurrectSent && dwRemain < 25)
        {
            BeginNotify(&info);
            GetBattleArenaMgr();
            CBattleArenaMgr::WriteStopResurrect(m_pNotifyMsg, m_byKind);
            BroadcastNotify();
            m_bStopResurrectSent = 1;
        }
        if (dwRemain < 15)
        {
            BeginNotify(&info);
            GetBattleArenaMgr();
            CBattleArenaMgr::WriteEndPlay(m_pNotifyMsg, m_byKind);
            BroadcastNotify();
            m_bEndPlaySent = 1;
        }
    }
    return TRUE;
}

void CShard_BattleArenaSchedule::OnEnd(CDateTime& /*now*/)     // 0x678720
{
    m_dwRunning = 0;
    BeginNotify(NULL);
    GetBattleArenaMgr()->OnScheduledGameEnd(m_pNotifyMsg, m_byKind);
    BroadcastNotify();
    LogSchedule("Scheduleing : BattleArena(%d) is End ......!", m_byKind);
}

DWORD CShard_BattleArenaSchedule::GetRemainSeconds(CDateTime now)
{
    CDateTime end((__time32_t)m_Info.dwEnd);
    return (DWORD)end.GetTime() - (DWORD)now.GetTime();
}

BOOL CShard_BattleArenaMatchSchedule::OnBegin()         // 0x6788c0
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    GetBattleArenaMgr()->SetMatchActive(m_byMatchType, 1);
    LogSchedule(NULL);
    return TRUE;
}

void CShard_BattleArenaMatchSchedule::OnEnd(CDateTime& /*now*/)    // 0x678a20
{
    GetBattleArenaMgr()->SetMatchActive(m_byMatchType, 0);
    LogSchedule(NULL);
}

void CBattleArenaMgr::OnCheckAndGoAck(CMsg* pMsg)
{
    BYTE byKind;
    BYTE byMatch;
    *pMsg >> byKind;
    *pMsg >> byMatch;
    switch (byKind)
    {
    case 0:
        ConfirmPlayUserRandom(pMsg, 0, byMatch);
        break;
    case 1:
        ConfirmPlayUserParty(pMsg, 1, byMatch);
        break;
    case 2:
        ConfirmPlayUserGuild(pMsg, 2, byMatch);
        break;
    case 3:
        ConfirmPlayUserJob(pMsg, 3, byMatch);
        break;
    }
    // BUG(SM-162): kind and match type of the answer are not range checked; the count is read
    // (and StartGame writes) past the tables.
    const DWORD* pdwCount = &m_adwCheckCount[0][0];
    if (!pdwCount[byKind * BATTLEARENA_MATCH_COUNT + byMatch])
        StartGame(byKind, byMatch);
}

void CBattleArenaMgr::StartGame(BYTE byKind, BYTE byMatch)
{
    LogPrintf(0, "BattleArena: game started %d, %d", byKind, byMatch);
    switch (byKind)
    {
    case 0:
        StartGameRandom(byMatch);
        break;
    case 1:
        StartGameParty(byMatch);
        break;
    case 2:
        StartGameGuild(byMatch);
        break;
    case 3:
        StartGameJob(byMatch);
        break;
    }
    CMsg* pMsg = AllocNotice();
    BYTE byType = 4;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&byMatch, 1);
    BroadcastToGameServers(pMsg);
    // BUG(SM-162): no range check of the kind.
    BYTE* pbClosed = &m_abClosed[0];
    pbClosed[byKind] = 0;
}

// ---------------------------------------------------------------------------------------------
// The check of the registered players (0x74D1 {kind, match, JIDs...} to each game server)

namespace
{

// The check requests of one kind and match type: one message per game server session. A message
// is sent as soon as its payload grows over the limit; the rest go out at the end.
class CArenaCheckBatch
{
public:
    typedef std::map<DWORD, CMsg*> SessionMsgMap;

    CArenaCheckBatch(BYTE byKind, BYTE byMatch, WORD wLimit)
        : m_byKind(byKind), m_byMatch(byMatch), m_wLimit(wLimit)
    {
    }

    void Add(DWORD dwSessionID, DWORD dwJID)
    {
        SessionMsgMap::iterator it = m_mapMsg.find(dwSessionID);
        if (it == m_mapMsg.end())
        {
            CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
            pMsg->SetOpcode(OP_BATTLEARENA_CHECK);
            pMsg->WriteBytes(&m_byKind, 1);
            pMsg->WriteBytes(&m_byMatch, 1);
            pMsg->WriteBytes(&dwJID, 4);
            m_mapMsg.insert(SessionMsgMap::value_type(dwSessionID, pMsg));
            return;
        }
        CMsg* pMsg = it->second;
        pMsg->WriteBytes(&dwJID, 4);
        if (pMsg->GetPayloadSize() > m_wLimit)
        {
            g_pBSNet->SendMsg(dwSessionID, pMsg);
            g_pBSNet->ReleaseMsg(pMsg);
            m_mapMsg.erase(it);
        }
    }

    void Flush()
    {
        for (SessionMsgMap::iterator it = m_mapMsg.begin(); it != m_mapMsg.end(); ++it)
        {
            CMsg* pMsg = it->second;
            g_pBSNet->SendMsg(it->first, pMsg);
            g_pBSNet->ReleaseMsg(pMsg);
        }
        m_mapMsg.clear();
    }

private:
    BYTE            m_byKind;
    BYTE            m_byMatch;
    WORD            m_wLimit;
    SessionMsgMap   m_mapMsg;
};

// Random and job: registrations of players that are offline (or have no character selected)
// are dropped, the others are checked.
void CheckSoloRegistrations(CArenaCheckBatch& batch, CBattleArenaMgr::ByteMap& mapReg,
                            DWORD& dwCount)
{
    CBattleArenaMgr::ByteMap::iterator it = mapReg.begin();
    while (it != mapReg.end())
    {
        DWORD dwJID = it->first;
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (!pUser || !pUser->GetCharID())
        {
            mapReg.erase(it++);
            continue;
        }
        batch.Add(pUser->GetSessionID(), dwJID);
        ++dwCount;
        ++it;
    }
}

}

void CBattleArenaMgr::CheckAndGoRandom()
{
    for (BYTE byMatch = 0; byMatch < BATTLEARENA_MATCH_COUNT; ++byMatch)
    {
        CArenaCheckBatch batch(0, byMatch, 0x190);
        m_adwCheckCount[0][byMatch] = 0;
        CheckSoloRegistrations(batch, m_amapRandom[byMatch], m_adwCheckCount[0][byMatch]);
        batch.Flush();
    }
}

void CBattleArenaMgr::CheckAndGoJob()
{
    for (BYTE byMatch = 0; byMatch < BATTLEARENA_MATCH_COUNT; ++byMatch)
    {
        CArenaCheckBatch batch(3, byMatch, 0x190);
        m_adwCheckCount[3][byMatch] = 0;
        CheckSoloRegistrations(batch, m_amapJobB[byMatch], m_adwCheckCount[3][byMatch]);
        CheckSoloRegistrations(batch, m_amapJobA[byMatch], m_adwCheckCount[3][byMatch]);
        batch.Flush();
    }
}

// The registering player must still be in the registered party; every member of the party is
// checked and gets a level entry of 0 (ConfirmPlayUserParty fills it in).
void CBattleArenaMgr::CheckAndGoParty()
{
    for (BYTE byMatch = 0; byMatch < BATTLEARENA_MATCH_COUNT; ++byMatch)
    {
        // The party messages are flushed above 0x192 bytes, the others above 0x190.
        CArenaCheckBatch batch(1, byMatch, 0x192);
        DWORD& dwCount = m_adwCheckCount[1][byMatch];
        dwCount = 0;
        DwordMap& mapReg = m_amapParty[byMatch];
        DwordMap::iterator it = mapReg.begin();
        while (it != mapReg.end())
        {
            CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(it->first);
            Party* pParty = NULL;
            if (pUser && it->second == pUser->GetPartyMemberInfo().m_dwPartyID)
                pParty = GetSingleton_CPartyManager()->FindParty(
                    pUser->GetPartyMemberInfo().m_dwPartyID);
            if (!pParty)
            {
                mapReg.erase(it++);
                continue;
            }

            DWORD dwPartyID = pParty->m_dwID;
            ByteMap mapLevel;
            DWORD dwMembers = pParty->GetMemberCount();
            for (DWORD i = 0; i < dwMembers; ++i)
            {
                CUser* pMember = pParty->m_vecMember[i];
                DWORD dwJID = pMember->GetJID();
                batch.Add(pMember->GetSessionID(), dwJID);
                m_amapPartyOfUser[byMatch].insert(DwordMap::value_type(dwJID, dwPartyID));
                mapLevel.insert(ByteMap::value_type(dwJID, 0));
                ++dwCount;
            }
            m_amapPartyMember[byMatch].insert(GroupMap::value_type(dwPartyID, mapLevel));
            ++it;
        }
        batch.Flush();
    }
}

// The members listed at the registration (char IDs) are checked when they are online; offline
// ones are dropped from the list, and the list itself is dropped afterwards.
void CBattleArenaMgr::CheckAndGoGuild()
{
    for (BYTE byMatch = 0; byMatch < BATTLEARENA_MATCH_COUNT; ++byMatch)
    {
        CArenaCheckBatch batch(2, byMatch, 0x190);
        DWORD& dwCount = m_adwCheckCount[2][byMatch];
        dwCount = 0;
        DwordMap& mapReg = m_amapGuild[byMatch];
        for (DwordMap::iterator it = mapReg.begin(); it != mapReg.end(); ++it)
        {
            DWORD dwGuildID = it->second;
            MemberListMap::iterator itList = m_amapGuildMemberList[byMatch].find(dwGuildID);
            if (itList == m_amapGuildMemberList[byMatch].end())
                continue;

            ByteMap mapLevel;
            std::list<DWORD>& listChar = itList->second;
            std::list<DWORD>::iterator itChar = listChar.begin();
            while (itChar != listChar.end())
            {
                CUser* pUser = GetSingleton_CClientManager()->FindUserByCharID(*itChar);
                if (!pUser)
                {
                    itChar = listChar.erase(itChar);
                    continue;
                }
                DWORD dwJID = pUser->GetJID();
                mapLevel.insert(ByteMap::value_type(dwJID, 0));
                batch.Add(pUser->GetSessionID(), dwJID);
                m_amapGuildOfUser[byMatch].insert(DwordMap::value_type(dwJID, dwGuildID));
                ++dwCount;
                ++itChar;
            }
            m_amapGuildMember[byMatch].insert(GroupMap::value_type(dwGuildID, mapLevel));
            m_amapGuildMemberList[byMatch].erase(itList);
        }
        batch.Flush();
    }
}

// ---------------------------------------------------------------------------------------------
// The answers of the check (0xB4D1); the match type is not range checked (SM-162)

void CBattleArenaMgr::ConfirmPlayUserRandom(CMsg* pMsg, BYTE byKind, BYTE byMatch)
{
    ByteMap* apMap[1] = { &m_amapRandom[byMatch] };
    ConfirmSoloUsers(pMsg, byKind, byMatch, apMap, 1,
                     "Unknown JID(%d) in CBattleArenaMgr::ConfirmPlayUserRandom() %d %d");
}

void CBattleArenaMgr::ConfirmPlayUserJob(CMsg* pMsg, BYTE byKind, BYTE byMatch)
{
    ByteMap* apMap[2] = { &m_amapJobB[byMatch], &m_amapJobA[byMatch] };
    ConfirmSoloUsers(pMsg, byKind, byMatch, apMap, 2,
                     "Unknown JID(%d) in CBattleArenaMgr::OnCheckAndGo_Ack() %d %d");
}

void CBattleArenaMgr::ConfirmPlayUserParty(CMsg* pMsg, BYTE byKind, BYTE byMatch)
{
    ConfirmGroupUsers(pMsg, byKind, byMatch, m_amapPartyOfUser[byMatch],
                      m_amapPartyLevel[byMatch], m_amapPartyMember[byMatch],
                      "Unknown Party Member(%d) in CBattleArenaMgr::ConfirmPlayUserParty() %d %d");
}

void CBattleArenaMgr::ConfirmPlayUserGuild(CMsg* pMsg, BYTE byKind, BYTE byMatch)
{
    ConfirmGroupUsers(pMsg, byKind, byMatch, m_amapGuildOfUser[byMatch],
                      m_amapGuildLevel[byMatch], m_amapGuildMember[byMatch],
                      "Unknown Guild Member(%d) in CBattleArenaMgr::ConfirmPlayUserGuild() %d %d");
}

// Result 0: the level is stored for the team building; any other result drops the registration
// and kicks the player with it. Unknown players are only logged.
void CBattleArenaMgr::ConfirmSoloUsers(CMsg* pMsg, BYTE byKind, BYTE byMatch,
                                       ByteMap* const* apMap, int nMap,
                                       const char* pszUnknownFormat)
{
    if (!pMsg)
        return;
    while (pMsg->GetUnreadSize() >= 6)
    {
        DWORD dwJID = 0;
        BYTE byResult = 0;
        BYTE byLevel = 0;
        *pMsg >> dwJID >> byResult >> byLevel;

        ByteMap* pMap = NULL;
        ByteMap::iterator it;
        for (int i = 0; i < nMap; ++i)
        {
            it = apMap[i]->find(dwJID);
            if (it != apMap[i]->end())
            {
                pMap = apMap[i];
                break;
            }
        }
        if (!pMap)
        {
            LogPrintf(0x1000000, pszUnknownFormat, dwJID, byKind, byMatch);
            continue;
        }

        --m_adwCheckCount[byKind][byMatch];
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (!pUser || !pUser->GetCharID())
        {
            pMap->erase(it);
        }
        else if (byResult)
        {
            pMap->erase(it);
            SendKickUser(dwJID, byResult);
        }
        else
        {
            it->second = byLevel;
        }
    }
}

// The level of the member and the highest level of its party / guild are stored. Players with a
// result, unknown and offline players (the last two with 3) are kicked at the end and leave the
// member map of their group; the level of a player with a result is stored all the same.
void CBattleArenaMgr::ConfirmGroupUsers(CMsg* pMsg, BYTE byKind, BYTE byMatch,
                                        DwordMap& mapGroupOfUser, ByteMap& mapGroupLevel,
                                        GroupMap& mapGroupMember, const char* pszUnknownFormat)
{
    ByteMap mapKick;
    while (pMsg->GetUnreadSize() >= 6)
    {
        DWORD dwJID = 0;
        BYTE byResult = 0;
        BYTE byLevel = 0;
        *pMsg >> dwJID >> byResult >> byLevel;

        DwordMap::iterator it = mapGroupOfUser.find(dwJID);
        if (it == mapGroupOfUser.end())
        {
            LogPrintf(0x1000000, pszUnknownFormat, dwJID, byKind, byMatch);
            mapKick.insert(ByteMap::value_type(dwJID, 3));
            continue;
        }

        --m_adwCheckCount[byKind][byMatch];
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (!pUser || !pUser->GetCharID())
        {
            mapKick.insert(ByteMap::value_type(dwJID, 3));
            continue;
        }
        if (byResult)
            mapKick.insert(ByteMap::value_type(dwJID, byResult));

        it = mapGroupOfUser.find(dwJID);
        if (it == mapGroupOfUser.end())
            continue;
        DWORD dwGroupID = it->second;
        ByteMap::iterator itLevel = mapGroupLevel.find(dwGroupID);
        if (itLevel == mapGroupLevel.end())
            mapGroupLevel.insert(ByteMap::value_type(dwGroupID, byLevel));
        else if (itLevel->second < byLevel)
            itLevel->second = byLevel;

        GroupMap::iterator itGroup = mapGroupMember.find(dwGroupID);
        if (itGroup != mapGroupMember.end())
        {
            ByteMap::iterator itMember = itGroup->second.find(dwJID);
            if (itMember != itGroup->second.end())
                itMember->second = byLevel;
        }
    }

    for (ByteMap::iterator itKick = mapKick.begin(); itKick != mapKick.end(); ++itKick)
    {
        DWORD dwJID = itKick->first;
        SendKickUser(dwJID, itKick->second);
        DwordMap::iterator it = mapGroupOfUser.find(dwJID);
        if (it == mapGroupOfUser.end())
            continue;
        GroupMap::iterator itGroup = mapGroupMember.find(it->second);
        if (itGroup == mapGroupMember.end())
            continue;
        ByteMap::iterator itMember = itGroup->second.find(dwJID);
        if (itMember != itGroup->second.end())
            itGroup->second.erase(itMember);
    }
}

// ---------------------------------------------------------------------------------------------
// The team building (StartGame)

// Kick reasons of the team building (0x34D2 {0xFF, JID, reason}).
enum
{
    BATTLEARENA_KICK_LEVEL      = 5,    // no level bracket
    BATTLEARENA_KICK_NO_PARTNER = 6,    // left over (odd count, too few players)
    BATTLEARENA_KICK_NO_LAYER   = 7     // the arena layer could not be opened
};

// The rest of 0x34D2.
enum
{
    BATTLEARENA_MEMBER_ENTERED  = 1,    // JID, world, layer, side
    BATTLEARENA_LAYER_OPENED    = 6,    // world, layer, kind, match
    BATTLEARENA_TEAM_READY      = 16    // world, layer, level bracket
};

namespace
{

// SortPred_ByteAt4Less (0x669bf0): the higher level first.
bool SortPred_LevelGreater(const sArenaLevelEntry& a, const sArenaLevelEntry& b)
{
    return (BYTE)b.dwLevel < (BYTE)a.dwLevel;
}

// Online players with a level bracket go into the list, the others without one are kicked.
void CollectLevelEntries(CBattleArenaMgr::ByteMap& mapReg, CBattleArenaMgr::LevelList& listEntry,
                         CBattleArenaMgr::DwordMap& mapKick)
{
    for (CBattleArenaMgr::ByteMap::iterator it = mapReg.begin(); it != mapReg.end(); ++it)
    {
        DWORD dwJID = it->first;
        BYTE byLevel = it->second;
        if (!GetSingleton_CClientManager()->FindUserByJID(dwJID))
            continue;
        if (Arena_GetLevelBracket(byLevel))
        {
            sArenaLevelEntry entry;
            entry.dwID = dwJID;
            entry.dwLevel = byLevel;
            listEntry.push_back(entry);
        }
        else
        {
            mapKick[dwJID] = BATTLEARENA_KICK_LEVEL;
        }
    }
}

BOOL IsTeamLayerOpen(sBattleArenaTeam& team)
{
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(team.m_wWorldID);
    return pWorld && pWorld->LayerAt(team.m_wLayerID);
}

BYTE GetTeamBracket(const sBattleArenaTeam& team)
{
    return Arena_GetLevelBracket((BYTE)(team.m_dwLevelSum / (DWORD)team.m_listMember.size()));
}

void RemoveFromParty(CUser* pUser)
{
    if (pUser->GetPartyMemberInfo().m_dwPartyID)
        GetSingleton_CPartyManager()->RemoveMember(pUser->GetPartyMemberInfo().m_dwPartyID,
                                                   pUser->GetJID(), 2);
}

void SendLayerOpened(const sBattleArenaTeam& team, BYTE byKind, BYTE byMatch)
{
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_LAYER_OPENED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&team.m_wWorldID, 2);
    pMsg->WriteBytes(&team.m_wLayerID, 2);
    pMsg->WriteBytes(&byKind, 1);
    pMsg->WriteBytes(&byMatch, 1);
    BroadcastToGameServers(pMsg);
}

void SendMemberEntered(DWORD dwJID, const sBattleArenaTeam& team, BYTE bySide)
{
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_MEMBER_ENTERED;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&team.m_wWorldID, 2);
    pMsg->WriteBytes(&team.m_wLayerID, 2);
    pMsg->WriteBytes(&bySide, 1);
    BroadcastToGameServers(pMsg);
}

void SendTeamReady(const sBattleArenaTeam& team)
{
    CMsg* pMsg = AllocNotice();
    BYTE byType = BATTLEARENA_TEAM_READY;
    pMsg->WriteBytes(&byType, 1);
    pMsg->WriteBytes(&team.m_wWorldID, 2);
    pMsg->WriteBytes(&team.m_wLayerID, 2);
    pMsg->WriteBytes(&team.m_byBracket, 1);
    BroadcastToGameServers(pMsg);
}

// Online members with a selected character join the team on the given side.
void AddMemberIfOnline(sBattleArenaTeam& team, DWORD dwJID, BYTE byLevel, BYTE bySide,
                       BOOL bLeaveParty)
{
    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
    if (!pUser || !pUser->GetCharID())
        return;
    team.m_dwLevelSum += byLevel;
    team.AddMember(dwJID);
    if (bLeaveParty)
        RemoveFromParty(pUser);
    SendMemberEntered(dwJID, team, bySide);
}

template <class TMap>
void SendKicks(const TMap& mapKick)
{
    for (typename TMap::const_iterator it = mapKick.begin(); it != mapKick.end(); ++it)
        CBattleArenaMgr::SendKickUser(it->first, (BYTE)it->second);
}

}

// Teams of up to 16 players by level; a team starts with at least 4 (level brackets 1 and 2) or
// 8 players, and the members are put on the sides A B B A A B B ... from a random start.
void CBattleArenaMgr::StartGameRandom(BYTE byMatch)
{
    DwordMap mapKick;
    LevelList listEntry;
    CollectLevelEntries(m_amapRandom[byMatch], listEntry, mapKick);
    listEntry.sort(SortPred_LevelGreater);

    TeamVector& vecTeam = m_avecTeam[0][byMatch];
    sBattleArenaTeam team;
    for (LevelList::iterator it = listEntry.begin(); it != listEntry.end(); ++it)
    {
        DWORD dwJID = it->dwID;
        BYTE byLevel = (BYTE)it->dwLevel;
        if (team.m_byCount == 16)
        {
            team.m_byBracket = GetTeamBracket(team);
            vecTeam.push_back(team);
            team = sBattleArenaTeam();
        }
        if (!IsTeamLayerOpen(team))
        {
            if (!OpenArenaLayer(&team, byMatch, 1))
            {
                mapKick[dwJID] = BATTLEARENA_KICK_NO_LAYER;
                continue;
            }
            team.m_byLevel = byLevel;
            SendLayerOpened(team, 0, byMatch);
        }
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (pUser && pUser->GetCharID())
        {
            team.m_dwLevelSum += byLevel;
            team.AddMember(dwJID);
        }
    }
    if (team.m_listMember.size())
    {
        if (team.m_listMember.size() & 1)
        {
            // BUG(SM-164): the level of the removed member stays in the level sum.
            DWORD dwJID = team.PopMember();
            mapKick[dwJID] = BATTLEARENA_KICK_NO_PARTNER;
        }
        if (team.m_listMember.size())
            team.m_byBracket = GetTeamBracket(team);
        vecTeam.push_back(team);
    }

    for (TeamVector::iterator itTeam = vecTeam.begin(); itTeam != vecTeam.end(); ++itTeam)
    {
        BOOL bStart = TRUE;
        BYTE bySide = (BYTE)(rand() % 2);
        BYTE byPrevSide = bySide;
        DWORD dwMin = itTeam->m_byBracket <= 2 ? 4 : 8;
        if (itTeam->m_listMember.size() >= dwMin)
            SendTeamReady(*itTeam);
        else
            bStart = FALSE;

        for (std::list<DWORD>::iterator itJID = itTeam->m_listMember.begin();
             itJID != itTeam->m_listMember.end(); ++itJID)
        {
            DWORD dwJID = *itJID;
            if (!bStart)
            {
                mapKick[dwJID] = BATTLEARENA_KICK_NO_PARTNER;
                continue;
            }
            CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
            if (!pUser)
                continue;
            RemoveFromParty(pUser);
            SendMemberEntered(dwJID, *itTeam, bySide);
            if (bySide == byPrevSide)
                bySide = (bySide == 0);
            else
                byPrevSide = bySide;
        }
    }
    SendKicks(mapKick);
}

// Two parties / guilds of neighbouring level make one team, one per side. The party version
// opens the layer before it looks up the members; the guild version looks them up first, picks
// the 32 player layer for more than 16 members and takes the members out of their parties.
template <class TKickMap>
void CBattleArenaMgr::StartGameGroup(BYTE byKind, BYTE byMatch)
{
    const BOOL bGuild = (byKind == 2);
    ByteMap& mapGroupLevel = bGuild ? m_amapGuildLevel[byMatch] : m_amapPartyLevel[byMatch];
    LevelList& listGroup = bGuild ? m_alistGuild[byMatch] : m_alistParty[byMatch];
    GroupMap& mapGroupMember = bGuild ? m_amapGuildMember[byMatch] : m_amapPartyMember[byMatch];

    TKickMap mapKick;
    // BUG(SM-165): the list is not cleared first; a second start of the same match type takes
    // the groups of the first one again.
    for (ByteMap::iterator it = mapGroupLevel.begin(); it != mapGroupLevel.end(); ++it)
    {
        sArenaLevelEntry entry;
        entry.dwID = it->first;
        entry.dwLevel = it->second;
        listGroup.push_back(entry);
    }
    listGroup.sort(SortPred_LevelGreater);

    sBattleArenaTeam team;
    LevelList::iterator itA = listGroup.begin();
    while (itA != listGroup.end())
    {
        LevelList::iterator itB = itA;
        ++itB;
        if (itB == listGroup.end())
        {
            mapKick[itA->dwID] = BATTLEARENA_KICK_NO_PARTNER;
            break;
        }
        DWORD dwGroupA = itA->dwID;
        DWORD dwGroupB = itB->dwID;
        BYTE byLevelA = (BYTE)itA->dwLevel;
        itA = itB;
        ++itA;

        GroupMap::iterator itMemberA = mapGroupMember.end();
        GroupMap::iterator itMemberB = mapGroupMember.end();
        BYTE byArenaSize = 1;
        if (bGuild)
        {
            itMemberA = mapGroupMember.find(dwGroupA);
            itMemberB = mapGroupMember.find(dwGroupB);
            if (itMemberA == mapGroupMember.end() || itMemberB == mapGroupMember.end())
                continue;
            if (itMemberA->second.size() + itMemberB->second.size() > 16)
                byArenaSize = 2;
        }
        if (!OpenArenaLayer(&team, byMatch, byArenaSize))
        {
            mapKick[dwGroupA] = BATTLEARENA_KICK_NO_LAYER;
            mapKick[dwGroupB] = BATTLEARENA_KICK_NO_LAYER;
            continue;
        }
        team.m_byLevel = byLevelA;
        SendLayerOpened(team, byKind, byMatch);
        if (!bGuild)
        {
            itMemberA = mapGroupMember.find(dwGroupA);
            itMemberB = mapGroupMember.find(dwGroupB);
            // BUG(SM-163): the layer is open and announced already; it is left behind.
            if (itMemberA == mapGroupMember.end() || itMemberB == mapGroupMember.end())
                continue;
        }

        BYTE bySide = (BYTE)(rand() % 2);
        ByteMap::iterator itMember;
        for (itMember = itMemberA->second.begin(); itMember != itMemberA->second.end();
             ++itMember)
            AddMemberIfOnline(team, itMember->first, itMember->second, bySide, bGuild);
        for (itMember = itMemberB->second.begin(); itMember != itMemberB->second.end();
             ++itMember)
            AddMemberIfOnline(team, itMember->first, itMember->second, bySide == 0, bGuild);

        if (team.m_listMember.size())
            team.m_byBracket = GetTeamBracket(team);
        m_avecTeam[byKind][byMatch].push_back(team);
        SendTeamReady(team);
        team = sBattleArenaTeam();
    }

    // The members of the kicked groups are kicked; the party version also drops the members.
    for (typename TKickMap::iterator itKick = mapKick.begin(); itKick != mapKick.end(); ++itKick)
    {
        BYTE byReason = (BYTE)itKick->second;
        GroupMap::iterator itGroup = mapGroupMember.find(itKick->first);
        if (itGroup == mapGroupMember.end())
            continue;
        for (ByteMap::iterator itMember = itGroup->second.begin();
             itMember != itGroup->second.end(); ++itMember)
            SendKickUser(itMember->first, byReason);
        if (!bGuild)
        {
            itGroup->second.clear();
            mapGroupMember.erase(itGroup);
        }
    }
}

void CBattleArenaMgr::StartGameParty(BYTE byMatch)
{
    StartGameGroup<DwordMap>(1, byMatch);
}

void CBattleArenaMgr::StartGameGuild(BYTE byMatch)
{
    StartGameGroup<ByteMap>(2, byMatch);
}

// The two job lists (jobs 1 and 3 against jobs 0 and 2) are paired by level into teams of 32,
// 16, 8, 4 and 2 pairs while both lists have enough players left. Pairs of 2 are not made when
// the best player left of the first list is above level 50. The rest is kicked.
void CBattleArenaMgr::StartGameJob(BYTE byMatch)
{
    LevelList listB;
    LevelList listA;
    DwordMap mapKick;
    CollectLevelEntries(m_amapJobB[byMatch], listB, mapKick);
    CollectLevelEntries(m_amapJobA[byMatch], listA, mapKick);
    listB.sort(SortPred_LevelGreater);
    listA.sort(SortPred_LevelGreater);

    DWORD dwRemainB = (DWORD)listB.size();
    DWORD dwRemainA = (DWORD)listA.size();
    LevelList::iterator itB = listB.begin();
    LevelList::iterator itA = listA.begin();
    TeamVector& vecTeam = m_avecTeam[3][byMatch];
    for (BYTE bySize = 32; bySize >= 2; bySize >>= 1)
    {
        if (bySize == 2 && dwRemainB && (BYTE)itB->dwLevel > 50)
            break;
        DWORD dwSize = bySize;
        while (dwRemainB >= dwSize && dwRemainA >= dwSize)
        {
            BYTE byKick = 0;
            BYTE bySide = (BYTE)(rand() % 2);
            sBattleArenaTeam team;
            BYTE byArenaSize = bySize >= 32 ? 3 : (bySize < 16 ? 1 : 2);
            if (OpenArenaLayer(&team, byMatch, byArenaSize))
                SendLayerOpened(team, 3, byMatch);
            else
                byKick = BATTLEARENA_KICK_NO_LAYER;

            for (DWORD i = 0; i < dwSize; ++i, ++itB, ++itA)
            {
                DWORD dwJIDB = itB->dwID;
                DWORD dwJIDA = itA->dwID;
                BYTE byLevelB = (BYTE)itB->dwLevel;
                BYTE byLevelA = (BYTE)itA->dwLevel;
                if (byKick)
                {
                    mapKick[dwJIDB] = byKick;
                    mapKick[dwJIDA] = byKick;
                    continue;
                }
                if (i == 0)
                    team.m_byLevel = byLevelB > byLevelA ? byLevelB : byLevelA;
                CUser* pUserB = GetSingleton_CClientManager()->FindUserByJID(dwJIDB);
                CUser* pUserA = GetSingleton_CClientManager()->FindUserByJID(dwJIDA);
                if (pUserB && pUserB->GetCharID())
                {
                    team.m_dwLevelSum += byLevelB;
                    team.AddMember(dwJIDB);
                    RemoveFromParty(pUserB);
                    SendMemberEntered(dwJIDB, team, bySide);
                }
                if (pUserA && pUserA->GetCharID())
                {
                    team.m_dwLevelSum += byLevelA;
                    team.AddMember(dwJIDA);
                    RemoveFromParty(pUserA);
                    SendMemberEntered(dwJIDA, team, bySide == 0);
                }
            }

            // BUG(SM-166): no member (layer not opened, everybody offline) divides by zero.
            team.m_byBracket = GetTeamBracket(team);
            vecTeam.push_back(team);
            SendTeamReady(team);
            dwRemainB -= dwSize;
            dwRemainA -= dwSize;
        }
    }

    for (; dwRemainB; --dwRemainB, ++itB)
        mapKick[itB->dwID] = BATTLEARENA_KICK_NO_PARTNER;
    for (; dwRemainA; --dwRemainA, ++itA)
        mapKick[itA->dwID] = BATTLEARENA_KICK_NO_PARTNER;
    SendKicks(mapKick);
}
