#include "ShardManager/GuildSiegeAuthority.h"

#include <strsafe.h>

#include "BSLib/DB/Database.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardScheduleExtern.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    SHARD_TABLE_GUILD_MEMBER = 0x0C,

    GUILD_MSG_ACK_WORLD1 = 0xB8F7,
    GUILD_MSG_NOTIFY_WORLD1 = 0x38F5,
    GUILD_NOTIFY_SIEGE_AUTHORITY = 46,
    GUILD_NOTIFY_SIEGE_AUTHORITY_RESET = 47,

    GUILD_OK_WORLD1 = 1,
    GUILD_ERR_FAILED_WORLD1 = 2,
    GUILD_ERR_GUILD_NOT_FOUND_WORLD1 = 0x4C0D,
    GUILD_ERR_MEMBER_NOT_FOUND_WORLD1 = 0x4C1D,

    GUILD_MEMBER_GROUP_SIEGE_AUTHORITY = 10,
};

BYTE GuildMember_GetSiegeAuthority(const CGuildMember* pMember)
{
    if (!pMember->m_pRecord)
        return 0xFF;
    return pMember->m_pRecord->SiegeAuthority;
}

void GuildMember_SetSiegeAuthority(CGuildMember* pMember, BYTE byAuthority)
{
    CInstanceGuildMember* pRecord = pMember->m_pRecord;
    if (pRecord)
        pRecord->SetColumn(&InstanceGuildMemberData::SiegeAuthority, byAuthority);
}

BYTE GuildCommon_GetMemberSiegeAuthority(CGuildCommon* pGuild, DWORD dwCharID)
{
    CGuildMember* pMember = pGuild->FindMember(dwCharID);
    if (!pMember)
        BSAssertFail();
    return GuildMember_GetSiegeAuthority(pMember);
}

DWORD GuildCommon_CountMembersWithSiegeAuthority(CGuildCommon* pGuild, BYTE byAuthority)
{
    DWORD dwCount = 0;
    for (GuildMemberMap::iterator it = pGuild->m_mapMember.begin();
         it != pGuild->m_mapMember.end(); ++it)
    {
        if (GuildMember_GetSiegeAuthority(it->second) == byAuthority)
            ++dwCount;
    }
    return dwCount;
}

void Guild_ResetSiegeAuthority(CGuild* pGuild)
{
    char szQuery[0xC00];
    if (!pGuild->m_pRecord ||
        StringCchPrintfA(szQuery, 0xC00, "{?=CALL _Guild_ResetSiegeAuthority (%d)}",
                         pGuild->GetID()) != S_OK)
    {
        BSAssertFail();
        return;
    }
    if (!g_pShardDB)
        BSAssertFail();
    int nReturn = 0;
    CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), 7, szQuery, 3, &nReturn);
    if (nReturn == -1)
    {
        std::string strName(pGuild->m_pRecord->Name);
        LogPrintf(0x2000000, "GUILD[%s] SIEGE AUTHORITY RESET FAILED!", strName.c_str());
        return;
    }

    // The procedure cleared the authorities in the table already: the records follow without
    // being marked for the next update.
    for (GuildMemberMap::iterator it = pGuild->m_mapMember.begin();
         it != pGuild->m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        if (GuildMember_GetSiegeAuthority(pMember) > 2)
        {
            GuildMember_SetSiegeAuthority(pMember, GUILD_SIEGE_AUTH_NONE);
            pMember->m_pRecord->m_qwDirty &= ~(unsigned __int64)0x400;
        }
    }
}

WORD Guild_SetMemberSiegeAuthority(CGuild* pGuild, DWORD dwCharID, BYTE byAuthority)
{
    if (!pGuild->m_pRecord)
        return GUILD_ERR_FAILED_WORLD1;

    BYTE byCurrent = GuildCommon_GetMemberSiegeAuthority(pGuild, dwCharID);
    // BUG(SM-114): the byte is compared with -1 after zero extension, so a missing member is
    // never reported here (the lookup below answers it).
    if ((int)byCurrent == -1)
        return GUILD_ERR_SIEGE_AUTH_NOT_MEMBER;

    if (byAuthority == GUILD_SIEGE_AUTH_NONE)
    {
        if (byCurrent == GUILD_SIEGE_AUTH_COMMANDER)
            return GUILD_ERR_SIEGE_AUTH_COMMANDER;
        if (byCurrent == GUILD_SIEGE_AUTH_NONE)
            return GUILD_ERR_SIEGE_AUTH_HAS_NONE;
    }
    else
    {
        if (byCurrent != GUILD_SIEGE_AUTH_NONE)
            return GUILD_ERR_SIEGE_AUTH_HAS_ONE;
        if (byAuthority == GUILD_SIEGE_AUTH_COMMANDER)
            return GUILD_ERR_SIEGE_AUTH_COMMANDER;
        DWORD dwLimit = 1;
        if (byAuthority == GUILD_SIEGE_AUTH_DEPUTY)
            dwLimit = 4;
        else if (byAuthority == GUILD_SIEGE_AUTH_SOLDIER)
            dwLimit = 10;
        if (GuildCommon_CountMembersWithSiegeAuthority(pGuild, byAuthority) >= dwLimit)
            return GUILD_ERR_SIEGE_AUTH_FULL;
    }

    GuildMemberMap::iterator it = pGuild->m_mapMember.lower_bound(dwCharID);
    if (it == pGuild->m_mapMember.end() || dwCharID < it->first)
        return GUILD_ERR_MEMBER_NOT_FOUND_WORLD1;
    if (!g_pShardDB)
        BSAssertFail();
    if (!g_pShardDB->GetTable(SHARD_TABLE_GUILD_MEMBER))
        return GUILD_ERR_FAILED_WORLD1;

    CGuildMember* pMember = it->second;
    GuildMember_SetSiegeAuthority(pMember, byAuthority);
    std::string strQuery;
    if (!pMember->m_pRecord->BuildUpdate(strQuery, GUILD_MEMBER_GROUP_SIEGE_AUTHORITY))
        return GUILD_ERR_FAILED_WORLD1;
    if (!g_pShardDB)
        BSAssertFail();
    if (CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), 7, strQuery.c_str(),
                                             0, NULL) & ~1)
    {
        return GUILD_ERR_FAILED_WORLD1;
    }
    return GUILD_OK_WORLD1;
}

void GuildManager_SendSiegeAuthorityResult(DWORD dwGuildID, DWORD dwCharID, BYTE byAuthority)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_NOTIFY_WORLD1);
    BYTE bySubType = GUILD_NOTIFY_SIEGE_AUTHORITY;
    pMsg->WriteBytes(&bySubType, 1);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    pMsg->WriteBytes(&byAuthority, 1);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

void GuildManager_SendSiegeAuthorityReset(DWORD dwGuildID)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_NOTIFY_WORLD1);
    BYTE bySubType = GUILD_NOTIFY_SIEGE_AUTHORITY_RESET;
    pMsg->WriteBytes(&bySubType, 1);
    pMsg->WriteBytes(&dwGuildID, 4);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, NULL);
}

void GuildManager_ResetSiegeAuthority(DWORD dwGuildID)
{
    CGuildManager* pMgr = GetSingleton_CGuildManager();
    if (!dwGuildID)
        return;
    CGuild* pGuild = pMgr->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        BSAssertFail();
        return;
    }
    Guild_ResetSiegeAuthority(pGuild);
    GuildManager_SendSiegeAuthorityReset(dwGuildID);
}

void CGuildManager::OnSetSiegeAuthority(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID;
    DWORD dwGuildID;
    DWORD dwCharID;
    BYTE byAuthority;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> byAuthority;

    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND_WORLD1;
    }
    else
    {
        wResult = Guild_SetMemberSiegeAuthority(pGuild, dwCharID, byAuthority);
        if (wResult == GUILD_OK_WORLD1)
        {
            CMsg* pAck = AllocAck(dwRequestID);
            BYTE byOk = 1;
            pAck->WriteBytes(&byOk, 1);
            g_pNetEngine->SendMsg(dwSessionID, pAck);
            g_pNetEngine->ReleaseMsg(pAck);
            GuildManager_SendSiegeAuthorityResult(dwGuildID, dwCharID, byAuthority);
            return;
        }
    }
    CMsg* pAck = AllocAck(dwRequestID);
    BYTE byFailed = 2;
    pAck->WriteBytes(&byFailed, 1);
    pAck->WriteBytes(&wResult, 2);
    g_pNetEngine->SendMsg(dwSessionID, pAck);
    g_pNetEngine->ReleaseMsg(pAck);
}
