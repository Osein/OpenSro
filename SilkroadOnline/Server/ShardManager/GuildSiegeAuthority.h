#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerCommon/GuildCommon.h"
#include "ShardManager/Guild.h"

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
// The siege authority functions (free functions so the guild classes need no new members;
// CGuildManager::OnSetSiegeAuthority of Guild.h is in GuildSiegeAuthority.cpp)

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
