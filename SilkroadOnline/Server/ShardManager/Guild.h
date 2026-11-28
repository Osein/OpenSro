#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "BSNet/Msg.h"
#include "ServerCommon/GuildCommon.h"
#include "ShardManager/GuildExtern.h"

// The shard manager's guild: CGuild (the DB side of the guild operations), the alliance DB
// functions and CGuildManager (requests 0x78F7 / notifications 0x78FE of the game servers,
// answers 0xB8F7, notifications 0x38F5 to the game servers).
//
// SM_social1 ports the functions with an address comment in Guild.cpp; the other CGuildManager
// members declared here (the remaining handlers, the DB helpers and the Send* notifications)
// belong to the second guild group.

enum
{
    GUILD_MSG_REQUEST       = 0x78F7,   // game server -> shard: BYTE subtype, ...
    GUILD_MSG_ACK           = 0xB8F7,   // shard -> game server: DWORD request ID, BYTE result, ...
    GUILD_MSG_GS_NOTIFY     = 0x78FE,   // game server -> shard: BYTE subtype, ...
    GUILD_MSG_NOTIFY        = 0x38F5,   // shard -> game servers: BYTE subtype, ...
};

// Results of the guild operations (WORD; 1 is success, the others are the error codes the game
// server shows).
enum
{
    GUILD_OK                        = 1,
    GUILD_ERR_FAILED                = 2,
    GUILD_ERR_NO_RECORD             = 3,
    GUILD_ERR_ARGUMENT              = 5,
    GUILD_ERR_GUILD_NOT_FOUND       = 0x4C0D,   // 19469
    GUILD_ERR_MEMBER_NOT_FOUND      = 0x4C1D,   // 19485
    GUILD_ERR_NOT_MASTER            = 0x4C1E,   // 19486
    GUILD_ERR_MASTER_CANNOT_LEAVE   = 0x4C1F,   // 19487
    GUILD_ERR_MAX_LEVEL             = 0x4C21,   // 19489
    GUILD_ERR_NOTICE_TITLE          = 0x4C22,   // 19490
    GUILD_ERR_NOTICE_BODY           = 0x4C23,   // 19491
    GUILD_ERR_TARGET_NOT_MASTER     = 0x4C24,   // 19492
    GUILD_ERR_TARGET_NOT_FOUND      = 0x4C25,   // 19493
    GUILD_ERR_ALREADY_IN_UNION      = 0x4C28,   // 19496
    GUILD_ERR_UNION_FULL            = 0x4C29,   // 19497
    GUILD_ERR_UNION_NOT_FOUND       = 0x4C2C,   // 19500
    GUILD_ERR_DONATE_SILENT         = 0x4C2D,   // 19501 (OnDonateSP does not log it)
    GUILD_ERR_NOT_IN_UNION          = 0x4C2F,   // 19503
    GUILD_ERR_KICK_UNION_NOT_FOUND  = 0x4C30,   // 19504
    GUILD_ERR_NOT_ENOUGH_GP         = 0x4C32,   // 19506
    GUILD_ERR_VOTE_RUNNING          = 0x4C37,   // 19511
    GUILD_ERR_LOCK_HELD_BY_SELF     = 0x4C47,   // 19527
    GUILD_ERR_LOCK_HELD_BY_OTHER    = 0x4C48,   // 19528
    GUILD_ERR_LOCK_NOT_HELD         = 0x4C49,   // 19529
    GUILD_ERR_LEVEL_TOO_LOW         = 0x4C4A,   // 19530
    GUILD_ERR_ABILITY_SET           = 0x4C54,   // 19540
};

// dword_781A98: the GP a level-up from level n costs (index 1..4; index 0 is the last entry of
// the member limit table before it, index 5 the zero after it).
extern const int g_anGuildLevelUpGP[6];

// The (CharID, permission) list of a permission update.
typedef std::vector<std::pair<DWORD, DWORD> > GuildPermissionVector;

// The guild war request of the game server (GuildWarRequest_ReadFromMsg, second guild group).
struct GUILD_WAR_REQUEST
{
    BYTE    b00;                    // +0
    BYTE    bVictoryPointIndex;     // +1
    DWORD   dwDuration;             // +4  packed: hours 10-14, days 15-19, minutes 20-25,
                                    //     seconds 26-31; 0x7FFFFFFF and above: no end
    DWORD   dwLodgedGold;           // +8  half of the stake
};

C_ASSERT(sizeof(GUILD_WAR_REQUEST) == 12);

// GuildWarRecord::Update (0x435a10): a new war of dwGuild1 against dwGuild2.
void GuildWarRecord_Update(CInstanceGuildWar* pWar, DWORD dwGuild1,
                           const GUILD_WAR_REQUEST& request, DWORD dwGuild2);
// GuildWarRecord::SetSlotValue (0x4359d0): PointGain[nIndex] (asserts for an index above 1).
void GuildWarRecord_SetPointGain(CInstanceGuildWar* pWar, DWORD nIndex, int nValue);
// GuildMember_WriteInfo (0x435c10, misnamed): ID, packed end time (0 when unset), victory
// point index, lodged gold, the two guilds and their points.
void GuildWarRecord_WriteToMsg(const CInstanceGuildWar* pWar, CMsg* pMsg);

// CCharacter_GetTrainingCampID (0x42c270): the camp of the character, 0 without one.
DWORD CCharacter_GetTrainingCampID(CCharacter* pCharacter);

class CGuild : public CGuildCommon
{
public:
    // CGuild::ctor (0x435d40) / CGuild::dtor (0x435dc0)
    CGuild() {}
    virtual ~CGuild() {}                                                    // 0

    // CGuild::RefreshMemberStatus (0x435fc0): type 1 stores the member's level (DB procedure
    // and record), type 2 logs the member in (dwValue 0, pUser) or out (1, releasing the
    // lock). The change goes to the game servers (0x38F5 subtype 6) in every case.
    int RefreshMemberStatus(DWORD dwCharID, BYTE bType, DWORD dwValue, CUser* pUser);
    // CGuild::OnMemberMsgRelay (0x435e10): BYTE, two strings -> 0x38F5 subtype 7.
    void OnMemberMsgRelay(CMsg* pMsg);
    // CGuild::DBDeleteMember (0x4361a0): _GUILD_DELMEMBER unless bReason is 3.
    WORD DBDeleteMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason);
    // CGuild::DBDeleteGuild (0x436320): _GUILD_DELETEGUILD, then the members are freed.
    WORD DBDeleteGuild(DWORD dwCharID);
    // CGuild::DonateGP (0x436470)
    WORD DonateGP(int nAmount, DWORD dwCharID);
    // CGuild::LevelUp (0x436600)
    WORD LevelUp();
    // CGuild::UpdateNotice (0x436730)
    WORD UpdateNotice(const std::string& strTitle, const std::string& strNotice);
    // CGuild::SetAllianceID_DBUpdate (0x4368a0): saved only with bSave 1.
    WORD SetAllianceID(CAlliance* pAlliance, BYTE bSave);
    // CGuild::LeaveUnion (0x4369f0): *pbResult becomes 3 when the alliance has to be
    // disbanded (the guild is its master or the last but one).
    WORD LeaveUnion(BYTE* pbResult);
    // CGuild::ClearAllianceID (0x436b90): record only.
    WORD ClearAllianceID();
    // CGuild::DelegateMaster (0x436bd0)
    WORD DelegateMaster(CInstanceGuildMember* pNewMaster, CInstanceGuildMember* pOldMaster);
    // CGuild::UpdateMemberPermissions (0x436ce0)
    WORD UpdateMemberPermissions(const GuildPermissionVector& vecPermission);
    // CGuild::DBIsMasterCandidates (0x436ea0): the candidates of a master vote.
    WORD DBIsMasterCandidates(CVote* pVote);
    // GuildRecord::AddGold (0x437040): Booty += nGold (record only).
    void AddBooty(int nGold);
    // CGuildManager::DBUpdateGuildWarRecord (0x437080, misnamed): Booty = 0 and saved.
    WORD DBClearBooty();
    // CGuild::Rename (0x437210)
    WORD Rename(const std::string& strName);
    // CGuild::MemberRename (0x4372c0)
    WORD MemberRename(DWORD dwCharID, const std::string& strName);
    // CGuild::SetMemberOnline (0x4373e0, misnamed): bType 1 takes the guild lock (members
    // with permission 8, guilds of level 2 and above), 2 releases it.
    WORD SetMemberOnline(DWORD dwCharID, BYTE bType);
    // CGuild::DBUpdateMember (0x437510): the member's nickname.
    WORD DBUpdateMember(DWORD dwCharID, const std::string& strNickname);
    // CGuild::SetCrestRev_DBUpdate (0x437680)
    WORD SetCrestRev(int nCrestRev);
    // CGuild::OffsetGP (0x437800)
    WORD OffsetGP(int nOffset, BOOL bSave);
    // CGuild::SetMercenaryAttr (0x437950): *pdwGold receives the gold cost.
    WORD SetMercenaryAttr(BYTE bAbility, DWORD* pdwGold, DWORD* pdwGP);
    // CGuild::BroadcastToOnlineMembers (0x437e70)
    void BroadcastToOnlineMembers(transaction* pTransaction);

    // CGuild::GetMercenaryAbilityCost (0x440940, second guild group)
    static void GetMercenaryAbilityCost(DWORD* pdwGP, BYTE bAbility, DWORD* pdwGold);
};

C_ASSERT(sizeof(CGuild) == 0x50);

class CGuildManager : public CGuildMgrCommon
{
public:
    // CGuildManager::CreateSingleton (0x437f10) / CGuildManager::dtor (0x437fb0)
    CGuildManager();
    virtual ~CGuildManager();                                               // 0

    virtual BOOL Load();                                                    // 1 (0x438050)
    // CGuildManager::vf03 (0x438060): reload (exits the process when it fails) and tell the
    // game servers (0x38F5 subtype 32). Always TRUE.
    virtual BOOL Reset();                                                   // 3
    // CGuildManager::vf04 (0x4380c0)
    virtual CGuildCommon* CreateGuild(CInstanceGuild* pRecord);             // 4
    // CGuildManager::vf05 / vf06 (0x43c260 / 0x43c2c0): the hostile entries of both guilds;
    // FALSE at the first guild that is missing or fails.
    virtual BOOL AddHostileGuilds(CInstanceGuildWar* pWar);                 // 5
    virtual BOOL RemoveHostileGuilds(CInstanceGuildWar* pWar);              // 6
    // CGuildManager::vf07 (0x43c320): _GuildWar_AccountCombatResult, the points and kill
    // counters, 0x38F5 subtype 29, and the end of the war when the end time has passed (more
    // points win) or the killer's side reached the victory points.
    virtual WORD OnCombatResult(CInstanceGuildWar* pWar, CGuildMember* pKiller,
                                CGuildMember* pVictim, int nPoint,
                                const std::string& strKillerName,
                                const std::string& strVictimName);         // 7

    CGuild* FindGuildT(DWORD dwGuildID)
    {
        return static_cast<CGuild*>(FindGuild(dwGuildID));
    }

    // CGuildManager::SendErrorB8F7 (0x438010, misnamed): a new 0xB8F7 {DWORD dwRequestID};
    // the caller appends the result and sends it.
    static CMsg* AllocAck(DWORD dwRequestID);
    // CGuildManager::DeleteGuild (0x4381c0)
    WORD DeleteGuild(DWORD dwGuildID, DWORD dwCharID);
    // CGuildManager::OnGuildRequest (0x438260): 0x78F7 by subtype.
    void OnGuildRequest(CMsg* pMsg);
    // CGuildManager::CheckGuildWarTimeouts (0x438430)
    void CheckGuildWarTimeouts();
    // CGuildManager::BroadcastGuildWarState (0x4385e0): 0x38F5 subtype 31 with the time.
    static int BroadcastGuildWarState();
    // CGuildManager::OnGuildNotify (0x438660): 0x78FE by subtype.
    void OnGuildNotify(CMsg* pMsg);
    // CGuildManager::OnRefreshMemberStatus (0x4386c0) / OnMemberRelay (0x438770)
    void OnRefreshMemberStatus(CMsg* pMsg);
    void OnMemberRelay(CMsg* pMsg);

    // Request handlers (subtype in brackets).
    void OnDisbandGuild(CMsg* pMsg);            // 1  0x4387a0
    void OnLeaveGuild(CMsg* pMsg);              // 3  0x438980
    void OnDonateSP(CMsg* pMsg);                // 8  0x438ad0
    void OnLevelUpGuild(CMsg* pMsg);            // 9  0x438bb0
    void OnUpdateNotice(CMsg* pMsg);            // 10 0x438d80
    void OnUnionInvite(CMsg* pMsg);             // 11 0x438f70
    void OnUnionLeave(CMsg* pMsg);              // 16 0x4391d0
    void OnUnionKick(CMsg* pMsg);               // 17 0x439350
    void OnDelegateMaster(CMsg* pMsg);          // 19 0x439550

    // Request handlers of SM_social2. Every handler with a request ID answers 0xB8F7 {1} (some
    // with data) or {2, WORD error}; OnOffsetGP and OnOffsetGPAndLevel do not answer.
    void OnCreateGuild(CMsg* pMsg);             // 0  0x43b530
    void OnJoinGuild(CMsg* pMsg);               // 2  0x43b320
    void OnUpdatePermissions(CMsg* pMsg);       // 20 0x4396f0
    void OnMasterVoteStart(CMsg* pMsg);         // 21 0x4399b0
    void OnGuildWarRecordUpdate(CMsg* pMsg);    // 30 0x439e70
    void OnMemberOnlineState(CMsg* pMsg);       // 36 0x439ff0
    void OnMemberInfoUpdate(CMsg* pMsg);        // 37 0x43a160
    void OnGuildCrestRequest38(CMsg* pMsg);     // 38 0x43a330
    void OnGuildCrestUpdate(CMsg* pMsg);        // 39 0x43a4e0
    void OnUnionCrestRequest40(CMsg* pMsg);     // 40 0x43a630
    void OnUnionCrestUpdate(CMsg* pMsg);        // 41 0x43a7e0
    void OnDonateSP42(CMsg* pMsg);              // 42 0x43a930
    void OnGiveMercenaryAbility(CMsg* pMsg);    // 43 0x43aac0
    void OnUpdateGuildSP(CMsg* pMsg);           // 44 0x43ac80
    void OnOffsetGP(CMsg* pMsg);                // 45 0x43adc0
    void OnSetSiegeAuthority(CMsg* pMsg);       // 46 0x43afc0 (SM_world_1)
    void OnGuildWarBegin(CMsg* pMsg);           // 50 0x439b00
    void OnOffsetGPAndLevel(CMsg* pMsg);        // 53 0x43b160
    void OnGuildWarCombatResult(CMsg* pMsg);    // notify 29 0x439c00

    // CGuildManager::OnGuildWarHostileRequest (0x43cb50, CMainProcess::OnMsg_7C00 0x7112): the
    // guild master's request to end a war; a trs_guild_job carries it. 1, or the error.
    WORD OnGuildWarHostileRequest(DWORD dwJID, WORD wOpcode, CMsg* pMsg);
    // CGuildManager::OnUnjoinHostileGuild (0x43ccf0, trs_guild_job::vf04): ends the war with
    // dwGuildID as the winner.
    void OnUnjoinHostileGuild(DWORD dwWarID, DWORD dwGuildID);
    // CGuildManager::SendCrestError (0x43ae80) / OnCrestRevUpdate (0x43aef0): the crest
    // uploader's answer (CMainProcess::OnMsg_380E); bGuild 1 guild, else alliance.
    static void SendCrestError(DWORD dwRequestID, DWORD dwSessionID);
    static void OnCrestRevUpdate(BYTE bGuild, DWORD dwRequestID, DWORD dwSessionID, DWORD dwID,
                                 int nCrestRev);
    // CGuildManager::BroadcastToGuild (0x43de80): CCallUserMgr's call to the online members.
    void BroadcastToGuild(DWORD dwGuildID, transaction* pTransaction);

    // DB helpers.
    // CGuildManager::RemoveMember (0x43b2d0): CGuild::DBDeleteMember, then 0x38F5 subtype 3.
    WORD RemoveMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason);
    // CGuildManager::IsValidGuildName (0x43b810, also CMainProcess::OnMsg_7450): 2..12
    // characters, the name filter, not in use.
    static WORD IsValidGuildName(const char* pszName);
    // CGuildManager::DBIsExistingGuildName (0x43b900): FALSE when the query fails.
    static BOOL DBIsExistingGuildName(const char* pszName);
    // CGuildManager::DBCreateGuild (0x43b9d0)
    static WORD DBCreateGuild(DWORD dwCharID, DWORD* pdwGuildID, const char* pszName);
    // CGuildManager::DBAddMember (0x43ca00): member class 10.
    static WORD DBAddMember(DWORD dwGuildID, DWORD dwCharID);
    // CGuildManager::DBCreateUnion (0x43bb60)
    WORD DBCreateUnion(CGuild* pMaster, CGuild* pMember, DWORD* pdwAllianceID);
    // CGuildManager::DisbandUnion (0x43be50): _AllianceRemove (result ignored) and removal.
    WORD DisbandUnion(DWORD dwAllianceID);
    // CGuildManager::DBBeginGuildWar (0x43be90): *ppWar receives the new war.
    WORD DBBeginGuildWar(DWORD dwGuildID1, DWORD dwGuildID2, const GUILD_WAR_REQUEST& request,
                         CInstanceGuildWar** ppWar);
    // CGuildManager::DBEndGuildWar (0x43c130): the stake goes to the winner's booty.
    WORD DBEndGuildWar(DWORD dwWinnerGuildID, DWORD dwWarID);
    // CGuildManager::DBAddGPHistory (0x43c5d0): _AddGPHistory, the new row to the front.
    WORD DBAddGPHistory(DWORD dwGuildID, DWORD dwJID, int nGP, BYTE bReason);
    // CGuildManager::DBDeleteGPHistory (0x43c910): the number of rows deleted (and popped).
    static BYTE DBDeleteGPHistory(CGuild* pGuild);

    // Notifications 0x38F5 to the game servers (subtype in brackets).
    static void SendCreateResult(DWORD dwGuildID);                                      // 0
    static void SendDisbandResult(DWORD dwGuildID);                                     // 1
    static void SendJoinResult(DWORD dwGuildID, const CInstanceGuildMember* pRecord);   // 2
    static void SendLeaveResult(DWORD dwGuildID, DWORD dwCharID, BYTE bReason);        // 3
    static void SendDonateSPResult(DWORD dwGuildID, DWORD dwCharID, int nAmount);      // 8
    static void SendLevelUpResult(DWORD dwGuildID, BYTE bLevel, int nGatheredSP);      // 9
    static void SendNoticeUpdated(DWORD dwGuildID, const std::string& strTitle,
                                  const std::string& strNotice);                       // 10
    static void SendUnionCreated(DWORD dwAllianceID, DWORD dwMasterGuildID,
                                 DWORD dwMemberGuildID);                               // 11
    static void SendUnionGuildAdded(DWORD dwAllianceID, DWORD dwGuildID, BYTE bSlot);  // 13
    static void SendUnionGuildRemoved(DWORD dwAllianceID, DWORD dwGuildID, BYTE bReason);  // 18
    static void SendUnionDisbanded(DWORD dwAllianceID);                                 // 18
    static void SendMasterDelegated(DWORD dwGuildID, DWORD dwNewMasterCharID,
                                    DWORD dwOldMasterCharID);                          // 19
    static void SendPermissionsUpdated(const GuildPermissionVector& vecPermission,
                                       DWORD dwGuildID);                               // 20
    static void SendGuildWarBegun(const CInstanceGuildWar* pWar);                       // 25
    static void SendHostileResult(DWORD dwWarID, DWORD dwGuildID);                      // 26
    static void SendGuildWarEnded(DWORD dwWarID, DWORD dwWinnerGuildID);                // 28
    static void SendGuildWarCombatResult(const std::string& strVictimName,
                                         CGuildMember* pKiller, DWORD dwWarID, int nPoint,
                                         CGuildMember* pVictim,
                                         const std::string& strKillerName);            // 29
    static void SendGuildWarRecordUpdated(DWORD dwGuildID, int nBooty);                 // 30
    static void SendGuildRenamed(const std::string& strName, DWORD dwGuildID);          // 33
    static void SendMemberRenamed(const std::string& strName, DWORD dwGuildID,
                                  DWORD dwCharID);                                     // 34
    static void SendMemberOnlineState(DWORD dwGuildID, DWORD dwCharID, BYTE bState);   // 36
    static void SendMemberInfoUpdated(const std::string& strNickname, DWORD dwGuildID,
                                      DWORD dwCharID);                                 // 37
    static void SendGuildCrestResult(DWORD dwGuildID, int nCrestRev);                   // 38/39
    static void SendUnionCrestResult(DWORD dwAllianceID, int nCrestRev);                // 40/41
    static void SendMercenaryAbilityResult(DWORD dwGuildID, BYTE bAttr, int nGatheredSP);  // 43
    static void SendGPUpdated(DWORD dwGuildID, int nGatheredSP);                        // 44
    void SendGuildInfo53(DWORD dwGuildID);                                               // 53
    void SendGPHistory54(DWORD dwGuildID, BYTE bCount);                                 // 54
    // CVoteManager::SendVoteStarted (0x43d4a0, misnamed): 0x3908 {1, vote}.
    static void SendVoteStarted(CVote* pVote);
};

// GuildWarRequest_ReadFromMsg (0x43f5a0): BYTE, DWORD duration, BYTE victory point index,
// DWORD gold.
void GuildWarRequest_ReadFromMsg(GUILD_WAR_REQUEST& request, CMsg* pMsg);

// dword_781AB0: the points that win a guild war, by victory point index (0: none).
extern const DWORD g_adwGuildWarVictoryPoint[8];

C_ASSERT(sizeof(CGuildManager) == 0x28);

// 0x8A8924 (the object itself is the static at 0x89A398)
extern CGuildManager* g_pGuildManager;

// GetSingleton_CGuildManager (0x40bab0): g_pGuildManager, asserted.
inline CGuildManager* GetSingleton_CGuildManager()
{
    if (!g_pGuildManager)
        BSAssertFail();
    return g_pGuildManager;
}
