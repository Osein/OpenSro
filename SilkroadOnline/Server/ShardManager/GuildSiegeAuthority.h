#pragma once

#include <winsock2.h>
#include <windows.h>

#include <list>
#include <map>
#include <vector>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"

class CMsg;
class CUser;

// The siege authority of the guild members (the right of a member in the fortress war): set by
// the guild master through the game server (0x78F7 subtype 46), reset when the guild takes a
// fortress (CSiegeFortressMgr::OnMsg_385F). The game servers learn the changes through 0x38F5
// subtypes 46 and 47.

// Siege authorities (InstanceGuildMember::SiegeAuthority)
enum
{
    GUILD_SIEGE_AUTH_NONE       = 0,
    GUILD_SIEGE_AUTH_COMMANDER  = 1,    // one member
    GUILD_SIEGE_AUTH_DEPUTY     = 2,    // up to 4 members
    GUILD_SIEGE_AUTH_SOLDIER    = 0x20, // up to 10 members
};

// Results of Guild_SetMemberSiegeAuthority besides GUILD_OK (1) and GUILD_ERR_FAILED (2).
enum
{
    GUILD_ERR_SIEGE_AUTH_NOT_MEMBER     = 3,        // unreachable, see SM-114
    GUILD_ERR_SIEGE_AUTH_COMMANDER      = 0x4C58,
    GUILD_ERR_SIEGE_AUTH_FULL           = 0x4C59,
    GUILD_ERR_SIEGE_AUTH_HAS_ONE        = 0x4C5A,
    GUILD_ERR_SIEGE_AUTH_HAS_NONE       = 0x4C5C,
};

// ---------------------------------------------------------------------------------------------
// Provided by the guild group (ServerCommon\GuildCommon.h, ShardManager\Guild.h of the guild
// branch): only the members used here, with the layout of the originals. Replaced by those
// headers when the branches meet.

class CGuildMember
{
public:
    virtual ~CGuildMember() {}

    DWORD                   m_dwState;      // +04
    BYTE                    m_bAlive;       // +08
    CInstanceGuildMember*   m_pRecord;      // +0C
    CUser*                  m_pUser;        // +10
};

C_ASSERT(FIELD_OFFSET(CGuildMember, m_pRecord) == 0x0C);
C_ASSERT(sizeof(CGuildMember) == 0x14);

typedef std::map<DWORD, CGuildMember*> GuildMemberMap;          // CharID -> member

class CGuildCommon
{
public:
    virtual ~CGuildCommon() {}

    // Guild_GetID_45E4F0 (0x45e4f0)
    DWORD GetID() const
    {
        return m_pRecord->ID;
    }
    // CGuildCommon::FindMember (0x4408a0): NULL for CharID 0 or an unknown member.
    CGuildMember* FindMember(DWORD dwCharID);

public:
    CInstanceGuild*                 m_pRecord;          // +04
    CInstanceGuildMember*           m_pMasterRecord;    // +08
    CGuildMember*                   m_pLockMember;      // +0C
    std::list<void*>                m_listGPHistory;    // +10
    GuildMemberMap                  m_mapMember;        // +1C
    DWORD                           m_dwLockCharID;     // +28
    std::vector<void*>              m_vecCandidate;     // +2C
    std::map<DWORD, DWORD>          m_mapHostile;       // +3C
    DWORD                           m_dwMaxLevel;       // +48
    DWORD                           m_dwAvgLevel;       // +4C
};

C_ASSERT(FIELD_OFFSET(CGuildCommon, m_mapMember) == 0x1C);
C_ASSERT(sizeof(CGuildCommon) == 0x50);

class CGuild : public CGuildCommon
{
};

class CGuildMgrCommon
{
public:
    virtual ~CGuildMgrCommon() {}

    // CGuildMgrCommon::FindGuild (0x40bf10): NULL for ID 0 or an unknown guild.
    CGuildCommon* FindGuild(DWORD dwGuildID);
};

class CGuildManager : public CGuildMgrCommon
{
public:
    CGuild* FindGuildT(DWORD dwGuildID)
    {
        return static_cast<CGuild*>(FindGuild(dwGuildID));
    }
    // CGuildManager::SendErrorB8F7 (0x438010): a new 0xB8F7 {DWORD dwRequestID}.
    static CMsg* AllocAck(DWORD dwRequestID);

    // 0x43afc0 (OnGuildRequest subtype 46): DWORD request ID, DWORD guild ID, DWORD CharID,
    // BYTE authority; answers 0xB8F7 {1} or {2, WORD error}, then 0x38F5 subtype 46.
    void OnSetSiegeAuthority(CMsg* pMsg);
};

// GetSingleton_CGuildManager (0x40bab0)
CGuildManager* GetSingleton_CGuildManager();

// ---------------------------------------------------------------------------------------------
// The siege authority functions (free functions so the guild classes need no new members)

// GuildMember_GetSiegeAuthority (0x43fc30): 0xFF without record.
BYTE GuildMember_GetSiegeAuthority(const CGuildMember* pMember);
// GuildMember_SetSiegeAuthority (0x43fc50): the record column (update group 10).
void GuildMember_SetSiegeAuthority(CGuildMember* pMember, BYTE byAuthority);
// Guild_GetMemberSiegeAuthority (0x440e70): asserts when the member is missing.
BYTE GuildCommon_GetMemberSiegeAuthority(CGuildCommon* pGuild, DWORD dwCharID);
// CGuildCommon::CountMembersWithSiegeAuthority (0x440e90)
DWORD GuildCommon_CountMembersWithSiegeAuthority(CGuildCommon* pGuild, BYTE byAuthority);
// CGuild::ResetSiegeAuthority (0x437b00): _Guild_ResetSiegeAuthority, then the authorities
// above 2 are cleared in the records (not saved again).
void Guild_ResetSiegeAuthority(CGuild* pGuild);
// CGuild::SetMemberSiegeAuthority (0x437cb0): checks the change, sets the record and saves it.
WORD Guild_SetMemberSiegeAuthority(CGuild* pGuild, DWORD dwCharID, BYTE byAuthority);
// CGuildManager::ResetSiegeAuthority (0x43c5a0, CSiegeFortressMgr::OnMsg_385F) on
// GetSingleton_CGuildManager: the reset and 0x38F5 subtype 47.
void GuildManager_ResetSiegeAuthority(DWORD dwGuildID);
// CGuildManager::SendSiegeAuthorityResult (0x43dc70) / SendSiegeAuthorityReset (0x43dd00).
void GuildManager_SendSiegeAuthorityResult(DWORD dwGuildID, DWORD dwCharID, BYTE byAuthority);
void GuildManager_SendSiegeAuthorityReset(DWORD dwGuildID);
