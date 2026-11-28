#pragma once

#include <windows.h>
#include <string>

#include "BSLib/DB/Database.h"
#include "ShardManager/SocialExtern.h"

// Shard DB readers of the character data queries (AQ_CharDataSelector). The loaders fill
// record lists through the registered tables of the shard DB (connection 4) and return a
// result code: 1 ok, 2 bad argument / statement text, 3 more than one match, 4 no match,
// 7 select failed, 13 the item rows do not match the slot rows. On a failed select the list
// is freed.

// Registered tables of the shard DB used here (index in the table list; the training camp
// tables SHARD_TABLE_TRAINING_CAMP* are those of SocialExtern.h).
enum
{
    SHARD_TABLE_INVENTORY           = 2,
    SHARD_TABLE_CHEST               = 3,
    SHARD_TABLE_ITEMS               = 4,
    SHARD_TABLE_SKILL               = 5,
    SHARD_TABLE_SKILL_MASTERY       = 6,
    SHARD_TABLE_INV_COS             = 8,
    SHARD_TABLE_GUILD               = 11,
    SHARD_TABLE_GUILD_MEMBER        = 12,
    SHARD_TABLE_ALLIANCE            = 13,
    SHARD_TABLE_GUILD_CHEST         = 0x20,
    SHARD_TABLE_SIEGE_FORTRESS      = 0x25,
    SHARD_TABLE_SIEGE_FORTRESS_REQUEST = 0x28,
    SHARD_TABLE_SIEGE_FORTRESS_STRUCT = 0x2A,
    SHARD_TABLE_INV_AVATAR          = 0x2E,
    SHARD_TABLE_INV_LINKED_STORAGE  = 0x30,
    SHARD_TABLE_ITEM_BY_DBID        = 0x33,
    SHARD_TABLE_OPEN_MARKET         = 0x38,
};

// Item owners of AQ_DB_SelectItems.
enum
{
    AQ_ITEMS_EQUIPPED       = 0,    // _Inventory slots 0..12 of the character
    AQ_ITEMS_INVENTORY      = 1,    // _Inventory slots 13..111
    AQ_ITEMS_CHEST          = 2,    // _Chest of the account (JID)
    AQ_ITEMS_COS            = 3,    // _InvCOS of a pet (COS ID)
    AQ_ITEMS_GUILD_CHEST    = 4,    // _GuildChest (guild ID)
    AQ_ITEMS_AVATAR         = 5,    // _InventoryForAvatar slots 0..4
    AQ_ITEMS_LINKED_STORAGE = 6,    // _InventoryForLinkedStorage of an item
};

// 0x484c70 / 0x484d40: {CALL _GET_USER_GOLD / _GET_GUILD_GOLD (id, ?)}; FALSE for ID 0, a
// failed call or a negative amount.
BOOL AQ_DB_GetUserGold(__int64* pGold, int nJID);
BOOL AQ_DB_GetGuildGold(__int64* pGold, int nGuildID);

// 0x484e10 / 0x485070 / 0x485150 / 0x485230: {?=CALL <proc> ('name')}; 0 for an empty name, a
// failed call or a negative value.
int AQ_DB_GetCharIDByName(const std::string& strName);
int AQ_DB_GetGuildIDByName(const std::string& strName);
int AQ_DB_GetJIDByCharName(const std::string& strName);
int AQ_DB_GetEngagedCOSID(const std::string& strName);

// 0x485310: the slot rows of the owner into pSlots, then the _Items rows of those slots
// (joined in slot order) into pItems.
BYTE AQ_DB_SelectItems(__int64 qwOwnerID, int nOwnerType, CDBRecordList* pSlots,
                       CDBRecordList* pItems);
// 0x4856f0: the rows of table 0x33 with nItemDBID.
BYTE AQ_DB_LoadByItemDBID(__int64 qwItemDBID, CDBRecordList* pList);
// AQ_Base::LoadOpenMarket (0x4857f0): the open market rows of the account of a character
// (3 when the name fails the injection check).
BYTE AQ_DB_LoadOpenMarket(CDBRecordList* pList, const std::string& strCharName);
// 0x485a10: _Skill and _SkillMastery rows of the character.
BYTE AQ_DB_LoadCharSkills(int nCharID, CDBRecordList* pSkills, CDBRecordList* pMasteries);
// 0x485b50: the training camp of a member, its members and their buffs.
BYTE AQ_DB_LoadCharTrainingCamp(CDBRecordList* pMembers, CDBRecordList* pBuffs,
                                const std::string& strCharName, CDBRecordList* pCamps);
// 0x486190: a guild by name, its members, its alliance and its siege fortress.
BYTE AQ_DB_LoadGuildData(CDBRecordList* pAlliances, CDBRecordList* pFortresses,
                         const std::string& strGuildName, CDBRecordList* pGuilds,
                         CDBRecordList* pMembers);

// 0x485df0: a siege fortress, its structures, the members of its owner guild and the guilds
// that requested the siege. The lists of the last three are only freed and logged when their
// select fails.
BYTE AQ_DB_LoadFortressData(CDBRecordList* pMembers, CDBRecordList* pRequests, int nFortressID,
                            CDBRecordList* pFortresses, CDBRecordList* pStructs);

// First int column of the first record of a list (the ID of the row).
inline int AQ_DB_RecordInt(const CDBRecordList* pList, int nOffset)
{
    return *(int*)((BYTE*)pList->front() + nOffset);
}
