#include "ShardManager/AQDBLoaders.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardAdminFwd.h"

namespace
{

// The inlined CDBConnPool_GetConnAt of the loaders.
CDBTable* GetTable(DWORD nIndex)
{
    if (!g_pShardDB)
        BSAssertFail();
    return ShardDB_GetTable(g_pShardDB, nIndex);
}

// {CALL proc (id, ?)} with a BIGINT output parameter.
BOOL GetGold(__int64* pGold, int nID, const char* pszFormat)
{
    if (!nID)
        return FALSE;
    __int64 qwValue = 0;
    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00, pszFormat, nID) != S_OK)
    {
        BSAssertFail();
        return FALSE;
    }
    if (!g_pShardDB)
        BSAssertFail();
    if (CDatabase::ExecDirectWithReturnValue(ShardDB_GetSession(g_pShardDB), 4, szQuery, 5,
                                             &qwValue) & ~1)
        return FALSE;
    if (qwValue < 0)
        return FALSE;
    *pGold = qwValue;
    return TRUE;
}

// {?=CALL proc ('name')} with an INT return value.
int GetIDByName(const std::string& strName, const char* pszFormat)
{
    if (strName.empty())
        return 0;
    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00, pszFormat, strName.c_str()) != S_OK)
    {
        BSAssertFail();
        return 0;
    }
    int anValue[2];
    anValue[0] = 0;
    anValue[1] = 0;
    if (!g_pShardDB)
        BSAssertFail();
    if (CDatabase::ExecDirectWithReturnValue(ShardDB_GetSession(g_pShardDB), 4, szQuery, 3,
                                             anValue) & ~1)
        return 0;
    return anValue[0] < 0 ? 0 : anValue[0];
}

} // namespace

BOOL AQ_DB_GetUserGold(__int64* pGold, int nJID)
{
    return GetGold(pGold, nJID, "{CALL _GET_USER_GOLD (%d, ?)}");
}

BOOL AQ_DB_GetGuildGold(__int64* pGold, int nGuildID)
{
    return GetGold(pGold, nGuildID, "{CALL _GET_GUILD_GOLD (%d, ?)}");
}

int AQ_DB_GetCharIDByName(const std::string& strName)
{
    return GetIDByName(strName, "{?=CALL _GETCHARIDBYNAME ('%s')}");
}

int AQ_DB_GetGuildIDByName(const std::string& strName)
{
    return GetIDByName(strName, "{?=CALL _GETGUILDID_BY_GUILDNAME ('%s')}");
}

int AQ_DB_GetJIDByCharName(const std::string& strName)
{
    return GetIDByName(strName, "{?=CALL _GETJID_BY_CHARNAME ('%s')}");
}

int AQ_DB_GetEngagedCOSID(const std::string& strName)
{
    return GetIDByName(strName, "{?=CALL _GETENGAGEDCOSID ('%s')}");
}

BYTE AQ_DB_SelectItems(__int64 qwOwnerID, int nOwnerType, CDBRecordList* pSlots,
                       CDBRecordList* pItems)
{
    if (!qwOwnerID)
        return 2;

    static const char s_szSlotRange[] =
        "WHERE CharID=%I64d AND ItemID<>0 AND Slot >= %d AND Slot < %d ORDER BY Slot ASC";
    char szQuery[0xC00];
    CDBTable* pSlotTable;
    HRESULT hr;
    switch (nOwnerType)
    {
    case AQ_ITEMS_EQUIPPED:
        pSlotTable = GetTable(SHARD_TABLE_INVENTORY);
        hr = StringCchPrintfA(szQuery, 0xC00, s_szSlotRange, qwOwnerID, 0, 13);
        break;
    case AQ_ITEMS_INVENTORY:
        pSlotTable = GetTable(SHARD_TABLE_INVENTORY);
        hr = StringCchPrintfA(szQuery, 0xC00, s_szSlotRange, qwOwnerID, 13, 112);
        break;
    case AQ_ITEMS_CHEST:
        pSlotTable = GetTable(SHARD_TABLE_CHEST);
        hr = StringCchPrintfA(szQuery, 0xC00, "WHERE UserJID=%I64d AND ItemID<>0 AND ItemID "
                              "IS NOT NULL ORDER BY Slot ASC", qwOwnerID);
        break;
    case AQ_ITEMS_COS:
        pSlotTable = GetTable(SHARD_TABLE_INV_COS);
        hr = StringCchPrintfA(szQuery, 0xC00, "WHERE COSID=%I64d AND ItemID<>0 AND ItemID "
                              "IS NOT NULL ORDER BY Slot ASC", qwOwnerID);
        break;
    case AQ_ITEMS_GUILD_CHEST:
        pSlotTable = GetTable(SHARD_TABLE_GUILD_CHEST);
        hr = StringCchPrintfA(szQuery, 0xC00, "WHERE GuildID=%I64d AND ItemID<>0 AND ItemID "
                              "IS NOT NULL ORDER BY Slot ASC", qwOwnerID);
        break;
    case AQ_ITEMS_AVATAR:
        pSlotTable = GetTable(SHARD_TABLE_INV_AVATAR);
        hr = StringCchPrintfA(szQuery, 0xC00, s_szSlotRange, qwOwnerID, 0, 5);
        break;
    case AQ_ITEMS_LINKED_STORAGE:
        pSlotTable = GetTable(SHARD_TABLE_INV_LINKED_STORAGE);
        hr = StringCchPrintfA(szQuery, 0xC00, "WHERE LinkedItemID = %I64d AND ItemID <> 0 AND "
                              "ItemID IS NOT NULL ORDER BY Slot ASC", qwOwnerID);
        break;
    default:
        // The original returns whatever AL holds after the assertion.
        BSAssertFail();
        return 0;
    }
    if (hr != S_OK)
    {
        BSAssertFail();
        return 2;
    }

    if (!pSlotTable->SelectWhere(szQuery, 4, pSlots, 1))
    {
        DB_FreeRecordList(pSlots, pSlotTable);
        return 7;
    }

    CDBTable* pItemTable = NULL;
    if (pSlots->size())
    {
        pItemTable = GetTable(SHARD_TABLE_ITEMS);
        switch (nOwnerType)
        {
        case AQ_ITEMS_EQUIPPED:
        case AQ_ITEMS_INVENTORY:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _Inventory INV JOIN _ITEMS IT WITH (NOLOCK) ON "
                "INV.ItemID=IT.ID64 \t\t\t\t\t\t\t\t\t\t\tWHERE INV.CharID=%I64d AND "
                "INV.ItemID<>0 AND INV.Slot >= %d AND INV.Slot < %d ORDER BY INV.Slot ASC",
                qwOwnerID, nOwnerType == AQ_ITEMS_EQUIPPED ? 0 : 13,
                nOwnerType == AQ_ITEMS_EQUIPPED ? 13 : 112);
            break;
        case AQ_ITEMS_CHEST:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _Chest C JOIN _ITEMS IT WITH (NOLOCK) ON C.ItemID=IT.ID64 "
                "\t\t\t\t\t\t\t\t\t\tWHERE C.UserJID=%I64d AND C.ItemID IS NOT NULL AND "
                "C.ItemID<>0 ORDER BY C.Slot ASC", qwOwnerID);
            break;
        case AQ_ITEMS_COS:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _InvCOS INV JOIN _ITEMS IT WITH (NOLOCK) ON INV.ItemID=IT.ID64 "
                "\t\t\t\t\t\t\t\t\t\t\t\t\t  WHERE INV.COSID=%I64d AND INV.ItemID<>0 ORDER BY "
                "INV.Slot ASC", qwOwnerID);
            break;
        case AQ_ITEMS_GUILD_CHEST:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _GuildChest INV JOIN _ITEMS IT WITH (NOLOCK) ON "
                "INV.ItemID=IT.ID64 \t\t\t\t\t\t\t\t\t\t\t\t\t  WHERE INV.GuildID=%I64d AND "
                "INV.ItemID<>0 ORDER BY INV.Slot ASC", qwOwnerID);
            break;
        case AQ_ITEMS_AVATAR:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _InventoryForAvatar INVAVA JOIN _ITEMS IT WITH (NOLOCK) ON "
                "INVAVA.ItemID=IT.ID64 \t\t\t\t\t\t\t\t\t\t WHERE INVAVA.CharID=%I64d AND "
                "INVAVA.ItemID<>0 AND INVAVA.Slot >= %d AND INVAVA.Slot < %d ORDER BY "
                "INVAVA.Slot ASC", qwOwnerID, 0, 5);
            break;
        default:
            hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT IT.* FROM _INVENTORYFORLINKEDSTORAGE IL JOIN _ITEMS IT WITH (NOLOCK) "
                "ON IL.ItemID = IT.ID64\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tWHERE IL.LinkedItemID = "
                "%I64d AND IL.ItemID IS NOT NULL AND IL.ItemID <> 0 ORDER BY IL.Slot ASC",
                qwOwnerID);
            break;
        }
        if (hr != S_OK)
        {
            BSAssertFail();
            return 2;
        }
        std::string strQuery(szQuery);
        if (!pItemTable->SelectCustom(strQuery, 4, pItems, 1))
        {
            // The slot rows stay in pSlots.
            DB_FreeRecordList(pItems, pItemTable);
            return 7;
        }
    }

    if (pSlots->size() == pItems->size())
        return 1;
    if (pSlotTable)
        DB_FreeRecordList(pSlots, pSlotTable);
    if (pItemTable)
        DB_FreeRecordList(pItems, pItemTable);
    return 13;
}

BYTE AQ_DB_LoadByItemDBID(__int64 qwItemDBID, CDBRecordList* pList)
{
    if (!qwItemDBID)
        return 2;
    CDBTable* pTable = GetTable(SHARD_TABLE_ITEM_BY_DBID);
    char szClause[0xC00];
    memset(szClause, 0, sizeof(szClause));
    if (StringCchPrintfA(szClause, 0xC00, "where nItemDBID = %d", (int)qwItemDBID) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pTable->SelectWhere(szClause, 4, pList, 1))
    {
        DB_FreeRecordList(pList, pTable);
        return 7;
    }
    return 1;
}

BYTE AQ_DB_LoadOpenMarket(CDBRecordList* pList, const std::string& strCharName)
{
    if (strCharName.empty())
        return 2;
    std::string strFound;
    if (!g_pShardDB)
        BSAssertFail();
    if (ShardDB_CheckSqlInjection(g_pShardDB, strCharName, strFound) == 1)
    {
        LogPrintf(0x2000001, "##%s %d## Query injection try by SMC! [Try string: %s] [Found "
                  "string: %s]", "AQ_Base::LoadOpenMarket", 667, strCharName.c_str(),
                  strFound.c_str());
        return 3;
    }
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_OPEN_MARKET);
    char szQuery[0xC00];
    memset(szQuery, 0, sizeof(szQuery));
    if (StringCchPrintfA(szQuery, 0xC00,
            "select * from _OpenMarket with(nolock) where JID = \t\t\t\t\t\t\t\t(select UserJID "
            "from _User AS U with(nolock) JOIN _Char AS C with(nolock) ON U.CharID=C.CharID "
            "\t\t\t\t\t\t\t\twhere C.CharName16='%s') AND status < 0xff",
            strCharName.c_str()) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    std::string strQuery(szQuery);
    if (!pTable->SelectCustom(strQuery, 4, pList, 0))
    {
        DB_FreeRecordList(pList, pTable);
        return 7;
    }
    return 1;
}

BYTE AQ_DB_LoadCharSkills(int nCharID, CDBRecordList* pSkills, CDBRecordList* pMasteries)
{
    if (!nCharID)
        return 2;
    CDBTable* pSkillTable = GetTable(SHARD_TABLE_SKILL);
    CDBTable* pMasteryTable = GetTable(SHARD_TABLE_SKILL_MASTERY);
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "where CharID = %d", nCharID) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pSkillTable->SelectWhere(szClause, 4, pSkills, 1))
    {
        DB_FreeRecordList(pSkills, pSkillTable);
        return 7;
    }
    if (!pMasteryTable->SelectWhere(szClause, 4, pMasteries, 1))
    {
        // BUG(SM-309): the mastery records go back through the skill table.
        DB_FreeRecordList(pMasteries, pSkillTable);
        return 7;
    }
    return 1;
}

BYTE AQ_DB_LoadCharTrainingCamp(CDBRecordList* pMembers, CDBRecordList* pBuffs,
                                const std::string& strCharName, CDBRecordList* pCamps)
{
    if (strCharName.empty())
        return 2;
    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00, "{?=CALL _GetCharTrainingCamp ('%s')}",
                         strCharName.c_str()) != S_OK)
    {
        BSAssertFail();
        return 7;
    }
    int anValue[2];
    anValue[0] = 0;
    anValue[1] = 0;
    if (!g_pShardDB)
        BSAssertFail();
    if (CDatabase::ExecDirectWithReturnValue(ShardDB_GetSession(g_pShardDB), 4, szQuery, 3,
                                             anValue) & ~1)
        return 2;
    int nCampID = anValue[0];
    if (nCampID < 0)
        return 4;

    CDBTable* pCampTable = GetTable(SHARD_TABLE_TRAINING_CAMP);
    CDBTable* pMemberTable = GetTable(SHARD_TABLE_TRAINING_CAMP_MEMBER);
    CDBTable* pBuffTable = GetTable(SHARD_TABLE_TRAINING_CAMP_BUFF);
    char szClause[0xC00];
    memset(szClause, 0, sizeof(szClause));
    if (StringCchPrintfA(szClause, 0xC00, "WHERE [ID] = %d", nCampID) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pCampTable->SelectWhere(szClause, 4, pCamps, 1))
    {
        DB_FreeRecordList(pCamps, pCampTable);
        return 7;
    }
    size_t nCamps = pCamps->size();
    if (nCamps > 1)
    {
        LogPrintf(0x2000000, "There are 2 or more training camps (match id : %d)", nCampID);
        return 3;
    }
    if (!nCamps)
    {
        LogPrintf(0, "There are no training camp. (charname : %s)", strCharName.c_str());
        return 4;
    }

    // ID column of the camp record.
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CampID = %d",
                         AQ_DB_RecordInt(pCamps, 24)) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pMemberTable->SelectWhere(szClause, 4, pMembers, 1))
    {
        DB_FreeRecordList(pMembers, pMemberTable);
        LogPrintf(0, "There are no member in this user camp. ( char name : %s)",
                  strCharName.c_str());
    }
    if (!pBuffTable->SelectWhere(szClause, 4, pBuffs, 1))
    {
        // BUG(SM-309): the buff records go back through the member table.
        DB_FreeRecordList(pBuffs, pMemberTable);
        LogPrintf(0, "There are no member buff status in this user camp. ( char name : %s)",
                  strCharName.c_str());
    }
    return 1;
}

BYTE AQ_DB_LoadGuildData(CDBRecordList* pAlliances, CDBRecordList* pFortresses,
                         const std::string& strGuildName, CDBRecordList* pGuilds,
                         CDBRecordList* pMembers)
{
    if (strGuildName.empty())
        return 2;
    CDBTable* pGuildTable = GetTable(SHARD_TABLE_GUILD);
    CDBTable* pMemberTable = GetTable(SHARD_TABLE_GUILD_MEMBER);
    CDBTable* pAllianceTable = GetTable(SHARD_TABLE_ALLIANCE);
    CDBTable* pFortressTable = GetTable(SHARD_TABLE_SIEGE_FORTRESS);

    const char* pszName = strGuildName.c_str();
    char szClause[0xC00];
    memset(szClause, 0, sizeof(szClause));
    if (StringCchPrintfA(szClause, 0xC00, "WHERE Name = '%s'", pszName) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pGuildTable->SelectWhere(szClause, 4, pGuilds, 1))
    {
        DB_FreeRecordList(pGuilds, pGuildTable);
        return 7;
    }
    size_t nGuilds = pGuilds->size();
    if (nGuilds > 1)
    {
        LogPrintf(0x2000000, "There are 2 or more guilds (match name : %s)", pszName);
        return 3;
    }
    if (!nGuilds)
    {
        LogPrintf(0, "There are no guild. (name : %s)", pszName);
        return 4;
    }

    // ID (+24) and alliance ID (+80) columns of the guild record.
    int nGuildID = AQ_DB_RecordInt(pGuilds, 24);
    if (StringCchPrintfA(szClause, 0xC00, "WHERE GuildID = %d", nGuildID) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pMemberTable->SelectWhere(szClause, 4, pMembers, 1))
    {
        DB_FreeRecordList(pMembers, pMemberTable);
        LogPrintf(0, "There are no member in this guild. ( guild name : %s)", pszName);
    }
    if (StringCchPrintfA(szClause, 0xC00, "WHERE ID = %d", AQ_DB_RecordInt(pGuilds, 80)) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pAllianceTable->SelectWhere(szClause, 4, pAlliances, 1))
    {
        DB_FreeRecordList(pAlliances, pAllianceTable);
        LogPrintf(0, "There are no alliance for this guild. ( guild name : %s)", pszName);
    }
    if (StringCchPrintfA(szClause, 0xC00, "WHERE GuildID = %d", nGuildID) != S_OK)
    {
        BSAssertFail();
        return 2;
    }
    if (!pFortressTable->SelectWhere(szClause, 4, pFortresses, 1))
    {
        DB_FreeRecordList(pFortresses, pFortressTable);
        LogPrintf(0, "This guild don`t have Siege Fortress. ( guild name : %s)", pszName);
    }
    return 1;
}
