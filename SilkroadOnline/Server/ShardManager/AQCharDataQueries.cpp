// AQ_CharDataSelector: the queries that read shard records (information, items, skills).

#include "ShardManager/AQCharDataSelector.h"

#include <strsafe.h>

#include <map>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/AQDBLoaders.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

// Provided by the GlobalManager jobs (GM_FormatTimestamp, SR_ShardManager 0x46fc50).
void GM_FormatTimestamp(std::string* pstrOut, const TIMESTAMP_STRUCT* pTime);

enum
{
    SHARD_TABLE_CHAR        = 1,
    SHARD_TABLE_CHAR_TRIJOB = 0x1F,
};

namespace
{

void WriteText(CMassiveMsg* pMsg, const char* psz)
{
    WORD wLen = (WORD)lstrlenA(psz);
    pMsg->Write(&wLen, 2);
    if (wLen)
        pMsg->Write(psz, wLen);
}

CDBTable* GetTable(DWORD nIndex)
{
    if (!g_pShardDB)
        BSAssertFail();
    return ShardDB_GetTable(g_pShardDB, nIndex);
}

} // namespace

void AQ_CharDataSelector_WriteCharInfo(CMassiveMsg* pMsg, CInstancePC* pPC)
{
    const char* pszObject = RefData_GetObjectCodeName(pPC->RefObjID);
    std::string strObject(pszObject, strlen(pszObject));
    const char* pszRegion = RefData_GetRegionName(pPC->LatestRegion);
    std::string strRegion(pszRegion, strlen(pszRegion));
    std::string strLogout;
    GM_FormatTimestamp(&strLogout, &pPC->LastLogout);

    pMsg->Write(&pPC->Deleted, 1);
    WriteText(pMsg, strObject.c_str());
    WriteText(pMsg, pPC->NickName16.c_str());
    pMsg->Write(&pPC->CurLevel, 1);
    pMsg->Write(&pPC->MaxLevel, 1);
    pMsg->Write(&pPC->ExpOffset, 8);
    pMsg->Write(&pPC->SExpOffset, 4);
    pMsg->Write(&pPC->Strength, 2);
    pMsg->Write(&pPC->Intellect, 2);
    pMsg->Write(&pPC->RemainGold, 8);
    pMsg->Write(&pPC->RemainSkillPoint, 4);
    pMsg->Write(&pPC->RemainStatPoint, 2);
    pMsg->Write(&pPC->RemainHwanCount, 1);
    pMsg->Write(&pPC->HP, 4);
    pMsg->Write(&pPC->MP, 4);
    WriteText(pMsg, strRegion.c_str());
    pMsg->Write(&pPC->LatestRegion, 2);
    pMsg->Write(&pPC->PosX, 4);
    pMsg->Write(&pPC->PosY, 4);
    pMsg->Write(&pPC->PosZ, 4);
    pMsg->Write(&pPC->DailyPK, 1);
    pMsg->Write(&pPC->TotalPK, 2);
    pMsg->Write(&pPC->PKPenaltyPoint, 4);
    WriteText(pMsg, strLogout.c_str());
}

bool AQ_CharDataSelector::QueryCharInfo()
{
    CDBTable* pCharTable = GetTable(SHARD_TABLE_CHAR);
    if (!pCharTable)
        return false;

    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharName16 = '%s'", m_strName.c_str()) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    // BUG(SM-310): the selected records are never freed on success.
    CDBRecordList listChar;
    if (!pCharTable->SelectWhere(szClause, 4, &listChar, 1) || !listChar.size())
    {
        m_byResult = AQ_RESULT_NO_ROW;
        DB_FreeRecordList(&listChar, pCharTable);
        return false;
    }
    if (listChar.size() != 1)
        BSAssertFail();

    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    CInstancePC* pPC = (CInstancePC*)listChar.front();
    AQ_CharDataSelector_WriteCharInfo(m_pResponse, pPC);

    // An empty text, then the guild name.
    std::string strEmpty;
    m_pResponse->WriteString(strEmpty.c_str());
    std::string strGuild;
    if (pPC->GuildID)
        GuildManager_FindGuildName(pPC->GuildID, strGuild);
    m_pResponse->WriteString(strGuild.c_str());

    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharID = %d", pPC->CharID) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    CDBRecordList listTrijob;
    CDBTable* pTrijobTable = GetTable(SHARD_TABLE_CHAR_TRIJOB);
    if (!pTrijobTable)
        return false;
    if (!pTrijobTable->SelectWhere(szClause, 1, &listTrijob, 0) || !listTrijob.size())
    {
        m_byResult = AQ_RESULT_NO_ROW;
        DB_FreeRecordList(&listTrijob, pTrijobTable);
        return false;
    }
    if (listTrijob.size() != 1)
        BSAssertFail();
    CInstanceCharTrijob* pTrijob = (CInstanceCharTrijob*)listTrijob.front();
    pMsg = m_pResponse;
    pMsg->Write(&pTrijob->JobType, 1);
    pMsg->Write(&pTrijob->Level, 1);
    pMsg->Write(&pTrijob->Exp, 4);
    pMsg->Write(&pTrijob->Contribution, 4);
    pMsg->Write(&pTrijob->Reward, 4);
    return true;
}

void AQ_CharDataSelector_WriteItemList(CDBRecordList* pItems, CMassiveMsg* pMsg,
                                       CDBRecordList* pSlots)
{
    if (pSlots->size() != pItems->size())
        BSAssertFail();
    if (!pSlots->size())
        return;

    std::string strUnused;
    std::string strDesc;
    CDBRecordList::iterator itSlot = pSlots->begin();
    for (CDBRecordList::iterator itItem = pItems->begin(); itItem != pItems->end();
         ++itItem, ++itSlot)
    {
        CInstanceSlot* pSlot = (CInstanceSlot*)*itSlot;
        CInstanceItem* pItem = (CInstanceItem*)*itItem;
        Item_FormatDescription(pItem, &strDesc);
        __int64 qwID = pItem->ID64;
        BYTE bySlot = pSlot->GetSlot();
        pMsg->Write(&bySlot, 1);
        pMsg->Write(&pItem->RefItemID, 4);
        pMsg->Write(&qwID, 8);
        pMsg->Write(&pItem->Serial64, 8);
        pMsg->Write(&pItem->Data, 4);
        WriteText(pMsg, strDesc.c_str());
    }
}

bool AQ_CharDataSelector::QueryItems()
{
    BYTE byTarget = m_byQueryTarget;
    int nOwnerID = 0;
    int nOwnerType;
    CDBTable* pSlotTable;
    switch (byTarget)
    {
    case 2:
        nOwnerType = AQ_ITEMS_EQUIPPED;
        pSlotTable = GetTable(SHARD_TABLE_INVENTORY);
        nOwnerID = AQ_DB_GetCharIDByName(m_strName);
        break;
    case 3:
        nOwnerType = AQ_ITEMS_INVENTORY;
        pSlotTable = GetTable(SHARD_TABLE_INVENTORY);
        nOwnerID = AQ_DB_GetCharIDByName(m_strName);
        break;
    case 4:
        nOwnerType = AQ_ITEMS_COS;
        pSlotTable = GetTable(SHARD_TABLE_INV_COS);
        nOwnerID = AQ_DB_GetEngagedCOSID(m_strName);
        break;
    case 7:
        nOwnerType = AQ_ITEMS_CHEST;
        pSlotTable = GetTable(SHARD_TABLE_CHEST);
        nOwnerID = m_strName.size() ? AQ_DB_GetJIDByCharName(m_strName) : (int)m_dwTargetID;
        break;
    case 11:
        nOwnerType = AQ_ITEMS_GUILD_CHEST;
        pSlotTable = GetTable(SHARD_TABLE_GUILD_CHEST);
        nOwnerID = m_strName.size() ? AQ_DB_GetGuildIDByName(m_strName) : (int)m_dwTargetID;
        break;
    case 14:
        nOwnerType = AQ_ITEMS_AVATAR;
        pSlotTable = GetTable(SHARD_TABLE_INV_AVATAR);
        nOwnerID = AQ_DB_GetCharIDByName(m_strName);
        break;
    case 16:
        nOwnerType = AQ_ITEMS_LINKED_STORAGE;
        pSlotTable = GetTable(SHARD_TABLE_INV_LINKED_STORAGE);
        break;
    default:
        m_byResult = AQ_RESULT_UNKNOWN_ITEM_TARGET;
        LogPrintf(0x1000000, "Unknown QueryTarget is Request! [%d]", byTarget);
        return false;
    }

    CDBRecordList listSlots;
    CDBRecordList listItems;
    BYTE byResult;
    if (m_byQueryTarget == 16)
    {
        if (!m_qwItemSerial)
        {
            m_byResult = AQ_RESULT_NOT_FOUND;
            return false;
        }
        byResult = AQ_DB_SelectItems(m_qwItemSerial, nOwnerType, &listSlots, &listItems);
    }
    else
    {
        if (!nOwnerID)
        {
            m_byResult = AQ_RESULT_NOT_FOUND;
            return false;
        }
        byResult = AQ_DB_SelectItems((unsigned int)nOwnerID, nOwnerType, &listSlots, &listItems);
    }
    m_byResult = byResult;
    if (byResult != 1)
        return false;

    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    if (m_byQueryTarget == 7)
    {
        __int64 qwGold = 0;
        AQ_DB_GetUserGold(&qwGold, nOwnerID);
        m_pResponse->Write(&qwGold, 8);
    }
    else if (m_byQueryTarget == 11)
    {
        __int64 qwGold = 0;
        AQ_DB_GetGuildGold(&qwGold, nOwnerID);
        m_pResponse->Write(&qwGold, 8);
    }
    BYTE byCount = (BYTE)listItems.size();
    m_pResponse->Write(&byCount, 1);
    AQ_CharDataSelector_WriteItemList(&listItems, m_pResponse, &listSlots);
    DB_FreeRecordList(&listSlots, pSlotTable);
    DB_FreeRecordList(&listItems, GetTable(SHARD_TABLE_ITEMS));
    return true;
}

void AQ_CharDataSelector_WriteSkills(CMassiveMsg* pMsg, CDBRecordList* pSkills,
                                     CDBRecordList* pMasteries)
{
    // BUG(SM-311): lower_bound without a key check: an unknown ID takes the next entry, an ID
    // past the last one reads through NULL.
    for (CDBRecordList::iterator it = pSkills->begin(); it != pSkills->end(); ++it)
    {
        CInstanceSkill* pSkill = (CInstanceSkill*)*it;
        if (!g_pReferenceData)
            BSAssertFail();
        CReferenceData::RefSkillMap& mapSkill = g_pReferenceData->m_mapSkill;
        CReferenceData::RefSkillMap::iterator itRef = mapSkill.lower_bound(pSkill->SkillID);
        RefSkill* pRef = itRef == mapSkill.end() ? NULL : itRef->second;
        pMsg->Write(&pSkill->SkillID, 4);
        pMsg->Write(&pRef->Basic_Level, 1);
    }
    for (CDBRecordList::iterator it = pMasteries->begin(); it != pMasteries->end(); ++it)
    {
        CInstanceSkillMastery* pMastery = (CInstanceSkillMastery*)*it;
        if (!g_pReferenceData)
            BSAssertFail();
        CReferenceData::RefSkillMasteryMap& mapMastery = g_pReferenceData->m_mapSkillMastery;
        CReferenceData::RefSkillMasteryMap::iterator itRef =
            mapMastery.lower_bound(pMastery->MasteryID);
        const void* pRef = itRef == mapMastery.end() ? NULL : itRef->second;
        pMsg->Write(pRef, 4);
        pMsg->Write(&pMastery->Level, 1);
    }
}

bool AQ_CharDataSelector::QuerySkills()
{
    CDBTable* pSkillTable = GetTable(SHARD_TABLE_SKILL);
    CDBTable* pMasteryTable = GetTable(SHARD_TABLE_SKILL_MASTERY);
    int nCharID = AQ_DB_GetCharIDByName(m_strName);

    CDBRecordList listSkills;
    CDBRecordList listMasteries;
    BYTE byResult = AQ_DB_LoadCharSkills(nCharID, &listSkills, &listMasteries);
    m_byResult = byResult;
    if (byResult != 1)
        return false;

    CMassiveMsg* pMsg = m_pResponse;
    BYTE byMasteries = (BYTE)listMasteries.size();
    WORD wSkills = (WORD)listSkills.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&wSkills, 2);
    pMsg->Write(&byMasteries, 1);
    AQ_CharDataSelector_WriteSkills(m_pResponse, &listSkills, &listMasteries);
    DB_FreeRecordList(&listSkills, pSkillTable);
    DB_FreeRecordList(&listMasteries, pMasteryTable);
    return true;
}

// -------------------------------------------------------------------------------------------
// Quests, item detail, rent info, collection book and open market (targets 5, 18, 20 .. 23)

enum
{
    SHARD_TABLE_CHAR_QUEST          = 10,
    SHARD_TABLE_BINDING_OPTION      = 0x33,
    SHARD_TABLE_RENT_ITEM_INFO      = 0x34,
    SHARD_TABLE_COLLECTION_BOOK     = 0x37,
};

bool AQ_CharDataSelector::QueryQuests()
{
    CDBTable* pTable = GetTable(SHARD_TABLE_CHAR_QUEST);
    if (!pTable)
        return false;
    int nCharID = AQ_DB_GetCharIDByName(m_strName);
    if (!nCharID)
    {
        m_byResult = AQ_RESULT_NO_CHAR;
        return false;
    }
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharID = %d", nCharID) != S_OK)
        BSAssertFail();

    CDBRecordList listQuests;
    if (!pTable->SelectWhere(szClause, 4, &listQuests, 1) || !listQuests.size())
    {
        m_byResult = AQ_RESULT_NO_ROW;
        DB_FreeRecordList(&listQuests, pTable);
        return false;
    }
    CMassiveMsg* pMsg = m_pResponse;
    WORD wCount = (WORD)listQuests.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&wCount, 2);
    for (CDBRecordList::iterator it = listQuests.begin(); it != listQuests.end(); ++it)
    {
        CInstanceQuest* pQuest = (CInstanceQuest*)*it;
        std::string strStart;
        std::string strEnd;
        pMsg = m_pResponse;
        pMsg->Write(&pQuest->QuestID, 4);
        pMsg->Write(&pQuest->Status, 1);
        pMsg->Write(&pQuest->AchievementCount, 2);
        GM_FormatTimestamp(&strStart, &pQuest->StartTime);
        GM_FormatTimestamp(&strEnd, &pQuest->EndTime);
        m_pResponse->WriteString(strStart.c_str());
        m_pResponse->WriteString(strEnd.c_str());
    }
    DB_FreeRecordList(&listQuests, pTable);
    return true;
}

void AQ_CharDataSelector_WriteItemDetailRows(CMassiveMsg* pMsg, CDBRecordList* pList)
{
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CInstanceBindingOptionWithItem* pRow = (CInstanceBindingOptionWithItem*)*it;
        pMsg->Write(&pRow->bOptType, 1);
        pMsg->Write(&pRow->nSlot, 1);
        pMsg->Write(&pRow->nOptID, 4);
        pMsg->Write(&pRow->nOptLvl, 1);
        pMsg->Write(&pRow->nOptValue, 4);
        pMsg->Write(&pRow->nParam1, 4);
        pMsg->Write(&pRow->nParam2, 4);
    }
}

bool AQ_CharDataSelector::QueryItemDetail()
{
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_BINDING_OPTION);
    CDBRecordList listRows;
    m_byResult = AQ_DB_LoadByItemDBID(m_qwItemSerial, &listRows);
    if (m_byResult != AQ_RESULT_OK)
        return false;
    CMassiveMsg* pMsg = m_pResponse;
    WORD wCount = (WORD)listRows.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&wCount, 2);
    AQ_CharDataSelector_WriteItemDetailRows(m_pResponse, &listRows);
    DB_FreeRecordList(&listRows, pTable);
    return true;
}

BYTE AQ_CharDataSelector::LoadByItemDBID(CDBRecordList* pList)
{
    if (!m_qwItemSerial)
        return 2;
    CDBTable* pTable = GetTable(SHARD_TABLE_RENT_ITEM_INFO);
    char szClause[0xC00];
    memset(szClause, 0, sizeof(szClause));
    // The serial is formatted as its low DWORD.
    if (StringCchPrintfA(szClause, 0xC00, "where nItemDBID = %d", (int)m_qwItemSerial) != S_OK)
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

bool AQ_CharDataSelector::QueryItemExtra()
{
    // BUG(SM-317): the selected _RentItemInfo records are never returned to their table.
    CDBRecordList listRows;
    m_byResult = LoadByItemDBID(&listRows);
    if (m_byResult != AQ_RESULT_OK)
        return false;
    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    if (listRows.size())
    {
        CInstanceRentItemInfo* pRow = (CInstanceRentItemInfo*)listRows.front();
        pMsg = m_pResponse;
        pMsg->Write(&pRow->nRentType, 4);
        pMsg->Write(&pRow->nCanDelete, 2);
        pMsg->Write(&pRow->nCanRecharge, 2);
        pMsg->Write(&pRow->PeriodBeginTime, 16);
        pMsg->Write(&pRow->PeriodEndTime, 16);
        pMsg->Write(&pRow->MeterRateTime, 16);
    }
    else
    {
        DWORD dwNone = 0;
        m_pResponse->Write(&dwNone, 4);
    }
    return true;
}

bool AQ_CharDataSelector::QueryCollectionBook()
{
    CDBTable* pTable = GetTable(SHARD_TABLE_COLLECTION_BOOK);
    if (!pTable)
        return false;
    int nCharID = AQ_DB_GetCharIDByName(m_strName);
    if (!nCharID)
    {
        m_byResult = AQ_RESULT_NO_CHAR;
        return false;
    }
    char szClause[0xC00];
    char szTheme[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharID = %d", nCharID) != S_OK)
        BSAssertFail();
    if (StringCchPrintfA(szTheme, 0xC00, " AND ThemeID = %u", m_dw98) != S_OK)
        BSAssertFail();
    if (m_dw98 && StringCchCatA(szClause, 0xC00, szTheme) != S_OK)
        BSAssertFail();

    CDBRecordList listRows;
    if (!pTable->SelectWhere(szClause, 4, &listRows, 1))
    {
        m_byResult = AQ_RESULT_NO_ROW;
        DB_FreeRecordList(&listRows, pTable);
        return false;
    }
    CMassiveMsg* pMsg = m_pResponse;
    DWORD dwCount = (DWORD)listRows.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&dwCount, 4);
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
    {
        CInstanceCollectionBook* pRow = (CInstanceCollectionBook*)*it;
        std::string strDate;
        pMsg = m_pResponse;
        pMsg->Write(&pRow->ThemeID, 4);
        pMsg->Write(&pRow->SlotIndex, 4);
        GM_FormatTimestamp(&strDate, &pRow->RegDate);
        WriteText(m_pResponse, strDate.c_str());
    }
    DB_FreeRecordList(&listRows, pTable);
    return true;
}

void AQ_CharDataSelector_WriteOpenMarketRows(CMassiveMsg* pMsg, CDBRecordList* pList)
{
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        CInstanceOpenMarket* pRow = (CInstanceOpenMarket*)*it;
        pMsg->Write(&pRow->JID, 4);
        pMsg->Write(&pRow->PersnalID, 4);
        WORD wLen = (WORD)pRow->CharName16.size();
        pMsg->Write(&wLen, 2);
        if (wLen)
            pMsg->Write(pRow->CharName16.c_str(), wLen);
        pMsg->Write(&pRow->Status, 1);
        pMsg->Write(&pRow->RefItemID, 4);
        pMsg->Write(&pRow->TidGroupID, 4);
        pMsg->Write(&pRow->ItemClass, 1);
        pMsg->Write(&pRow->ItemID, 8);
        pMsg->Write(&pRow->SellCnt, 4);
        pMsg->Write(&pRow->RegDate, 16);
        pMsg->Write(&pRow->EndDate, 16);
        pMsg->Write(&pRow->Price, 8);
        pMsg->Write(&pRow->Deposit, 8);
        pMsg->Write(&pRow->UseCash, 4);
        pMsg->Write(&pRow->Serial64, 8);
    }
}

bool AQ_CharDataSelector::QueryOpenMarketByChar()
{
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_OPEN_MARKET);
    CDBRecordList listRows;
    m_byResult = AQ_DB_LoadOpenMarket(&listRows, m_strName);
    if (m_byResult != AQ_RESULT_OK)
        return false;
    CMassiveMsg* pMsg = m_pResponse;
    DWORD dwCount = (DWORD)listRows.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&dwCount, 4);
    AQ_CharDataSelector_WriteOpenMarketRows(m_pResponse, &listRows);
    DB_FreeRecordList(&listRows, pTable);
    return true;
}

bool AQ_CharDataSelector::QueryOpenMarketByDate()
{
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetTable(g_pShardDB, SHARD_TABLE_OPEN_MARKET);
    CDBRecordList listRows;
    char szQuery[0xC00];
    memset(szQuery, 0, sizeof(szQuery));
    HRESULT hr;
    if (m_byA0)
    {
        hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT OM.* FROM [dbo].[_OpenMarket] OM WITH(NOLOCK) JOIN [dbo].[_RefObjCommon] "
                "COM WITH(NOLOCK) \t\t\t\t\t\t\tON OM.RefItemID=COM.ID WHERE OM.Status=%d AND "
                "OM.TidGroupID=%d AND OM.ItemClass=%d AND (OM.RegDate >= '%s' AND OM.RegDate < "
                "'%s')",
                m_byA1, m_dw9C, m_byA0, m_strC0.c_str(), m_strDC.c_str());
    }
    else
    {
        hr = StringCchPrintfA(szQuery, 0xC00,
                "SELECT OM.* FROM [dbo].[_OpenMarket] OM WITH(NOLOCK) JOIN [dbo].[_RefObjCommon] "
                "COM WITH(NOLOCK) \t\t\t   ON OM.RefItemID=COM.ID WHERE OM.Status=%d AND "
                "OM.TidGroupID=%d AND (OM.RegDate >= '%s' AND OM.RegDate < '%s')",
                m_byA1, m_dw9C, m_strC0.c_str(), m_strDC.c_str());
    }
    if (hr != S_OK)
    {
        BSAssertFail();
        return false;
    }
    std::string strQuery;
    strQuery.assign(szQuery, strlen(szQuery));
    if (!pTable->SelectCustom(strQuery, 4, &listRows, 0))
    {
        DB_FreeRecordList(&listRows, pTable);
        return false;
    }
    CMassiveMsg* pMsg = m_pResponse;
    DWORD dwCount = (DWORD)listRows.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&dwCount, 4);
    AQ_CharDataSelector_WriteOpenMarketRows(m_pResponse, &listRows);
    DB_FreeRecordList(&listRows, pTable);
    return true;
}

// -------------------------------------------------------------------------------------------
// Item description (Item_FormatDescription 0x469240)

namespace
{

// The "%s, <name> +<value>" options: the text is appended to the one built so far and a
// failed StringCchPrintfA asserts. The codes are the Param1 of the _RefMagicOpt rows.
struct MAGIC_OPTION_TEXT
{
    DWORD       dwCode;
    const char* pszFormat;
};

const MAGIC_OPTION_TEXT s_aMagicOptionText[] =
{
    { 0x6572,     "%s, er +%d%%" },
    { 0x6275,     "%s, anti-burn +%d%%" },
    { 0x6573,     "%s, anti-eshock +%d%%" },
    { 0x667A,     "%s, anti-frostbite +%d%%" },
    { 0x6870,     "%s, hp +%d" },
    { 0x6872,     "%s, hr +%d%%" },
    { 0x7A62,     "%s, anti-zombie +%d%%" },
    { 0x6D70,     "%s, mp +%d" },
    { 0x7073,     "%s, anti-toxin +%d%%" },
    { 0x627269,   "%s, block rate +%d" },
    { 0x6D7061,   "%s, Ava mp +%d" },
    { 0x687061,   "%s, Ava hp +%d" },
    { 0x637269,   "%s, critical +%d" },
    { 0x657261,   "%s, Ava er +%d%%" },
    { 0x687261,   "%s, Ava hr +%d%%" },
    { 0x61737472, "%s, astr +%d" },
    { 0x64617261, "%s, Ava dara +%d%%" },
    { 0x6D707267, "%s, Ava mprg +%d%%" },
    { 0x68706D70, "%s, regen hpmp +%d%%" },
    { 0x64757275, "%s, duru +%d%%" },
    { 0x64727561, "%s, Ava daru +%d%%" },
    { 0x64757261, "%s, flaw -%d%%" },
    { 0x6576626C, "%s, evbl +%d%%" },
    { 0x65766372, "%s, evcr +%d%%" },
    { 0x696E7461, "%s, Ava int +%d" },
    { 0x68707267, "%s, Ava hprg +%d%%" },
    { 0x6D646961, "%s, Ava mdia +%d%%" },
    { 0x72666569, "%s, resist fear +%d%%" },
    { 0x72646969, "%s, resist disease +%d%%" },
    { 0x72637369, "%s, resist csmp +%d%%" },
    { 0x72737469, "%s, resist stun +%d%%" },
    { 0x72736C69, "%s, resist sleep +%d%%" },
    { 0x73747261, "%s, Ava str +%d" },
};

enum
{
    MAGIC_OPTION_STR            = 0x737472,
    MAGIC_OPTION_INT            = 0x696E74,
    MAGIC_OPTION_UNREPARABLE    = 0x6E726570,
    MAGIC_OPTION_REINFORCE      = 0x7265696E,
    MAGIC_OPTION_BLESS_APE      = 0x617065,
    MAGIC_OPTION_BLESS_ATHA     = 0x61746861,
    MAGIC_OPTION_BLESS_REP      = 0x726570,
    MAGIC_OPTION_BLESS_LUCK     = 0x6C75636B,
    MAGIC_OPTION_BLESS_SOLID    = 0x736F6C69,
};

const int ITEM_TEXT_SIZE = 0x400;

// The bless options (no assert on a failed format). The unknown bless text is only reached
// for the five codes above, so it is never written.
void FormatBless(char* pszText, const mfunc_desc* pDesc, int nValue)
{
    // The original reads the DWORD at +0x14 of the descriptor (Param4 of a range).
    DWORD dwRate = static_cast<const mfunc_single_range*>(pDesc)->m_dwMax;
    switch (pDesc->m_dwCode)
    {
    case MAGIC_OPTION_BLESS_ATHA:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "atha(%d%%): %d", dwRate, nValue);
        break;
    case MAGIC_OPTION_BLESS_APE:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "ape(%d%%): %d,", dwRate, nValue);
        break;
    case MAGIC_OPTION_BLESS_REP:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "rep: %d,", nValue - 1);
        break;
    case MAGIC_OPTION_BLESS_LUCK:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "luck(%d%%): %d", dwRate, nValue);
        break;
    case MAGIC_OPTION_BLESS_SOLID:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "solid(%d%%): %d", dwRate, nValue);
        break;
    default:
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "unknown bless[%d]", pDesc->m_dwCode);
        break;
    }
}

// One magic parameter (low DWORD: option ID, high DWORD: value) into pszText. Unknown
// options and codes leave the text unchanged.
// BUG(SM-350): the bless, unreparable and reinforce texts replace the text built so far
// (the leading "[" and the earlier options) instead of being appended.
void FormatMagicOption(char* pszText, __int64 qwParam)
{
    int nValue = (int)(qwParam >> 32);
    DWORD dwMLevel = 0;
    if (!g_pReferenceData)
        BSAssertFail();
    const mfunc_desc* pDesc =
        g_pReferenceData->m_MagicOpt.FindMagicOption((WORD)qwParam, &dwMLevel);
    if (!pDesc)
        return;

    DWORD dwCode = pDesc->m_dwCode;
    for (size_t i = 0; i < _countof(s_aMagicOptionText); ++i)
    {
        if (s_aMagicOptionText[i].dwCode == dwCode)
        {
            if (StringCchPrintfA(pszText, ITEM_TEXT_SIZE, s_aMagicOptionText[i].pszFormat,
                                 pszText, nValue) != S_OK)
                BSAssertFail();
            return;
        }
    }
    switch (dwCode)
    {
    case MAGIC_OPTION_STR:
        // Without a value the magic level is shown.
        StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "%sstr +%d,", pszText,
                         nValue ? (DWORD)nValue : dwMLevel);
        break;
    case MAGIC_OPTION_INT:
        if (nValue)
            StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "%sint +%d", pszText, nValue);
        else
            StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "%sint +%d,", pszText, dwMLevel);
        break;
    case MAGIC_OPTION_UNREPARABLE:
        if (StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "unreparable") != S_OK)
            BSAssertFail();
        break;
    case MAGIC_OPTION_REINFORCE:
        // The DWORD at +0x10 of the descriptor (Param3).
        if (StringCchPrintfA(pszText, ITEM_TEXT_SIZE, "reinforce +%d%%",
                             static_cast<const mfunc_single_range*>(pDesc)->m_dwMin) != S_OK)
            BSAssertFail();
        break;
    case MAGIC_OPTION_BLESS_APE:
    case MAGIC_OPTION_BLESS_ATHA:
    case MAGIC_OPTION_BLESS_REP:
    case MAGIC_OPTION_BLESS_LUCK:
    case MAGIC_OPTION_BLESS_SOLID:
        FormatBless(pszText, pDesc, nValue);
        break;
    }
}

} // namespace

void Item_FormatDescription(CInstanceItem* pItem, std::string* pstrOut)
{
    pstrOut->clear();
    if (!g_pReferenceData)
        BSAssertFail();
    RefObjCommonRow* pRef = g_pReferenceData->FindRefObj(pItem->RefItemID);
    // Equipment only (item, TID2 1).
    if (!pRef || !RefTypeID_Is(pRef->TypeID, 0, 3, 1))
        return;

    std::string strGender;
    std::string strOptLevel;
    std::string strVariance;
    std::string strMagic;
    char szText[ITEM_TEXT_SIZE];

    BYTE byGender = static_cast<RefObjItem*>(pRef)->ReqGender;
    if (byGender == 1)
        strGender.assign("[M] ", 4);
    else if (byGender == 0)
        strGender = "[F] ";

    if (pItem->OptLevel)
    {
        szText[0] = 0;
        if (StringCchPrintfA(szText, ITEM_TEXT_SIZE, "[OptLvl: +%d] ", pItem->OptLevel) != S_OK)
            BSAssertFail();
        strOptLevel.assign(szText, strlen(szText));
    }
    if (pItem->Variance)
    {
        szText[0] = 0;
        if (StringCchPrintfA(szText, ITEM_TEXT_SIZE, "[Var: %I64d] ", pItem->Variance) != S_OK)
            BSAssertFail();
        strVariance.assign(szText, strlen(szText));
    }
    if (pItem->MagParamNum)
    {
        szText[0] = 0;
        if (StringCchPrintfA(szText, ITEM_TEXT_SIZE, "[") != S_OK)
            BSAssertFail();
        for (int i = 0; i < pItem->MagParamNum; ++i)
            FormatMagicOption(szText, pItem->MagParam[i]);
        if (StringCchPrintfA(szText, ITEM_TEXT_SIZE, "%s]", szText) != S_OK)
            BSAssertFail();
        strMagic.assign(szText, strlen(szText));
    }

    pstrOut->append(strGender, 0, std::string::npos);
    pstrOut->append(strOptLevel, 0, std::string::npos);
    pstrOut->append(strVariance, 0, std::string::npos);
    pstrOut->append(strMagic, 0, std::string::npos);
}

// -------------------------------------------------------------------------------------------
// Guild (target 8) and training camp (target 12)

void AQ_CharDataSelector_WriteGuildInfo(CDBRecordList* pGuilds, CDBRecordList* pFortresses,
                                        CMassiveMsg* pMsg, CDBRecordList* pMembers,
                                        CDBRecordList* pAlliances)
{
    int nGuildID = 0;
    std::string strDate;
    if (!pGuilds->empty())
    {
        CInstanceGuild* pGuild = (CInstanceGuild*)pGuilds->front();
        GM_FormatTimestamp(&strDate, &pGuild->FoundationDate);
        nGuildID = pGuild->ID;
        pMsg->Write(&nGuildID, 4);
        WriteText(pMsg, pGuild->Name.c_str());
        pMsg->Write(&pGuild->Lvl, 1);
        pMsg->Write(&pGuild->GatheredSP, 4);
        WriteText(pMsg, strDate.c_str());
        WriteText(pMsg, pGuild->MasterCommentTitle.c_str());
        WriteText(pMsg, pGuild->MasterComment.c_str());
        // The siege fortress of the guild, 0 for none.
        int nNoFortress = 0;
        const int* pnFortressID = &nNoFortress;
        if (!pFortresses->empty())
            pnFortressID = &((CInstanceSiegeFortress*)pFortresses->front())->FortressID;
        pMsg->Write(pnFortressID, 4);
        pMsg->Write(&pGuild->CurCrestRev, 4);
        pMsg->Write(&pGuild->LastCrestRev, 4);
        pMsg->Write(&pGuild->MercenaryAttr, 1);
    }

    DWORD dwMembers = (DWORD)pMembers->size();
    pMsg->Write(&dwMembers, 4);
    for (CDBRecordList::iterator it = pMembers->begin(); it != pMembers->end(); ++it)
    {
        CInstanceGuildMember* pMember = (CInstanceGuildMember*)*it;
        GM_FormatTimestamp(&strDate, &pMember->JoinDate);
        WriteText(pMsg, pMember->CharName.c_str());
        pMsg->Write(&pMember->CharLevel, 1);
        pMsg->Write(&pMember->GP_Donation, 4);
        WriteText(pMsg, strDate.c_str());
        pMsg->Write(&pMember->MemberClass, 1);
        pMsg->Write(&pMember->Permission, 4);
        // The nickname with its string length.
        WORD wLen = (WORD)pMember->Nickname.size();
        pMsg->Write(&wLen, 2);
        if (wLen)
            pMsg->Write(pMember->Nickname.c_str(), wLen);
    }

    if (pAlliances->empty())
        return;
    // The other guilds of the alliance, read one by one from _Guild.
    CDBRecordList listAllies;
    char szClause[0xC00];
    memset(szClause, 0, sizeof(szClause));
    CInstanceAlliedClan* pAlliance = (CInstanceAlliedClan*)pAlliances->front();
    CDBTable* pGuildTable = GetTable(SHARD_TABLE_GUILD);
    DWORD dwAllies = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (pAlliance->Ally[i] && pAlliance->Ally[i] != nGuildID)
            ++dwAllies;
    }
    pMsg->Write(&dwAllies, 4);
    for (int i = 0; i < 8; ++i)
    {
        if (!pAlliance->Ally[i] || pAlliance->Ally[i] == nGuildID)
            continue;
        if (StringCchPrintfA(szClause, 0xC00, "WHERE ID = %d", pAlliance->Ally[i]) != S_OK)
            BSAssertFail();
        // The result of the select is not checked.
        pGuildTable->SelectWhere(szClause, 4, &listAllies, 1);
        for (CDBRecordList::iterator it = listAllies.begin(); it != listAllies.end(); ++it)
        {
            CInstanceGuild* pAlly = (CInstanceGuild*)*it;
            GM_FormatTimestamp(&strDate, &pAlly->FoundationDate);
            WriteText(pMsg, pAlly->Name.c_str());
            pMsg->Write(&pAlly->Lvl, 1);
            WriteText(pMsg, strDate.c_str());
            pMsg->Write(&pAlly->GatheredSP, 4);
        }
        DB_FreeRecordList(&listAllies, pGuildTable);
    }
}

bool AQ_CharDataSelector::QueryGuild()
{
    // BUG(SM-351): the selected records are never returned to their tables.
    CDBRecordList listGuilds;
    CDBRecordList listMembers;
    CDBRecordList listAlliances;
    CDBRecordList listFortresses;
    m_byResult = AQ_DB_LoadGuildData(&listAlliances, &listFortresses, m_strName, &listGuilds,
                                     &listMembers);
    if (m_byResult != AQ_RESULT_OK)
        return false;
    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    AQ_CharDataSelector_WriteGuildInfo(&listGuilds, &listFortresses, m_pResponse, &listMembers,
                                       &listAlliances);
    return true;
}

void AQ_CharDataSelector_WriteTrainingCampInfo(CDBRecordList* pBuffs, CDBRecordList* pCamps,
                                               CMassiveMsg* pMsg, CDBRecordList* pMembers)
{
    if (pCamps->empty())
        return;
    CInstanceTrainingCamp* pCamp = (CInstanceTrainingCamp*)pCamps->front();
    std::string strCreation;
    std::string strEvaluation;
    GM_FormatTimestamp(&strCreation, &pCamp->CreationDate);
    GM_FormatTimestamp(&strEvaluation, &pCamp->LatestEvaluationDate);
    pMsg->Write(&pCamp->ID, 4);
    WriteText(pMsg, strCreation.c_str());
    pMsg->Write(&pCamp->Rank, 1);
    pMsg->Write(&pCamp->GraduateCount, 4);
    pMsg->Write(&pCamp->EvaluationPoint, 4);
    WriteText(pMsg, strEvaluation.c_str());
    WriteText(pMsg, pCamp->CommentTitle.c_str());
    WriteText(pMsg, pCamp->Comment.c_str());

    // The member names by character ID, for the buffs.
    DWORD dwMembers = (DWORD)pMembers->size();
    pMsg->Write(&dwMembers, 4);
    std::string strJoin;
    std::map<DWORD, std::string> mapNames;
    for (CDBRecordList::iterator it = pMembers->begin(); it != pMembers->end(); ++it)
    {
        CInstanceTrainingCampMember* pMember = (CInstanceTrainingCampMember*)*it;
        GM_FormatTimestamp(&strJoin, &pMember->JoinDate);
        WriteText(pMsg, pMember->CharName.c_str());
        WriteText(pMsg, strJoin.c_str());
        pMsg->Write(&pMember->MemberClass, 1);
        pMsg->Write(&pMember->CharJoinedLevel, 1);
        pMsg->Write(&pMember->CharCurLevel, 1);
        pMsg->Write(&pMember->CharMaxLevel, 1);
        pMsg->Write(&pMember->HonorPoint, 4);
        mapNames.insert(std::make_pair((DWORD)pMember->CharID, pMember->CharName));
    }

    DWORD dwBuffs = (DWORD)pBuffs->size();
    pMsg->Write(&dwBuffs, 4);
    std::string strRecipient;
    std::string strDonor;
    std::string strStart;
    for (CDBRecordList::iterator it = pBuffs->begin(); it != pBuffs->end(); ++it)
    {
        CInstanceTrainingCampBuffStatus* pBuff = (CInstanceTrainingCampBuffStatus*)*it;
        GM_FormatTimestamp(&strStart, &pBuff->StartingTime);
        if (!pBuff->RecipientCharID)
            BSAssertFail();
        // A recipient that is no member asserts, then the checked iterator throws.
        std::map<DWORD, std::string>::iterator itName =
            mapNames.find((DWORD)pBuff->RecipientCharID);
        if (itName == mapNames.end())
            BSAssertFail();
        strRecipient = itName->second;
        if (pBuff->DonorCharID)
        {
            itName = mapNames.find((DWORD)pBuff->DonorCharID);
            if (itName == mapNames.end())
            {
                BSAssertFail();
                strDonor.assign("", (size_t)0);
            }
            else
            {
                strDonor = itName->second;
            }
        }
        else
        {
            strDonor.assign("", (size_t)0);
        }
        WriteText(pMsg, strRecipient.c_str());
        WriteText(pMsg, strDonor.c_str());
        WriteText(pMsg, strStart.c_str());
        pMsg->Write(&pBuff->RemainBuffPoint, 4);
        pMsg->Write(&pBuff->BuffType, 1);
    }
}

bool AQ_CharDataSelector::QueryTrainingCamp()
{
    // BUG(SM-351): the selected records are never returned to their tables.
    CDBRecordList listCamps;
    CDBRecordList listMembers;
    CDBRecordList listBuffs;
    m_byResult = AQ_DB_LoadCharTrainingCamp(&listMembers, &listBuffs, m_strName, &listCamps);
    if (m_byResult != AQ_RESULT_OK)
        return false;
    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    AQ_CharDataSelector_WriteTrainingCampInfo(&listBuffs, &listCamps, m_pResponse, &listMembers);
    return true;
}

// -------------------------------------------------------------------------------------------
// Pets (targets 9 and 10)

enum
{
    SHARD_TABLE_CHAR_COS = 7,
};

BOOL AQ_CharDataSelector::LoadPetItem(CDBRecordList* pItems, CDBRecordList* pCOS, WORD* pwCount)
{
    size_t nOldCount = pCOS->size();
    CDBRecordList listRow;
    CDBTable* pCOSTable = GetTable(SHARD_TABLE_CHAR_COS);
    if (!pCOSTable)
    {
        BSAssertFail();
        // BUG(SM-353): the pet list is freed through the missing (NULL) table.
        DB_FreeRecordList(pCOS, NULL);
        return FALSE;
    }

    char szClause[0xC00];
    for (CDBRecordList::iterator it = pItems->begin(); it != pItems->end(); ++it)
    {
        CInstanceItem* pItem = (CInstanceItem*)*it;
        pItem->ResolveRefObj();
        RefObjCommonRow* pRef = pItem->m_pRefObj;
        if (!pRef)
            BSAssertFail();
        // Pet summon items (item 3/2/1) carry the COS ID in Data.
        if (!RefTypeID_Is(pRef->TypeID, 0, 3, 2, 1) || !pItem->Data)
            continue;
        if (StringCchPrintfA(szClause, 0xC00, "WHERE ID = %d", pItem->Data) != S_OK)
        {
            BSAssertFail();
            DB_FreeRecordList(pCOS, pCOSTable);
            return FALSE;
        }
        if (!pCOSTable->SelectWhere(szClause, 4, &listRow, 0))
        {
            BSAssertFail();
            DB_FreeRecordList(pCOS, pCOSTable);
            DB_FreeRecordList(&listRow, pCOSTable);
            return FALSE;
        }
        if (listRow.size() != 1)
            BSAssertFail();
        if (!listRow.size())
        {
            DB_FreeRecordList(pCOS, pCOSTable);
            DB_FreeRecordList(&listRow, pCOSTable);
            return FALSE;
        }
        pCOS->push_back(listRow.front());
        listRow.clear();
    }
    *pwCount = (WORD)(pCOS->size() - nOldCount);
    return TRUE;
}

bool AQ_CharDataSelector::QueryPetDetail()
{
    int nCharID = AQ_DB_GetCharIDByName(m_strName);
    int nJID = nCharID ? AQ_DB_GetJIDByCharName(m_strName) : 0;
    if (!nJID)
    {
        m_byResult = AQ_RESULT_NOT_FOUND;
        return false;
    }

    CMassiveMsg* pMsg;
    BYTE byOK = 1;
    if (m_byQueryTarget == 10)
    {
        // The items in the inventory of the pet m_dwTargetID.
        int nCOSID = (int)m_dwTargetID;
        CDBTable* pSlotTable = GetTable(SHARD_TABLE_INV_COS);
        if (!pSlotTable)
            BSAssertFail();
        char szQuery[0xC00];
        StringCchPrintfA(szQuery, 0xC00,
                "SELECT SLOT FROM _INVCOS WHERE COSID=%d AND ItemID<>0 AND ItemID IS NOT NULL "
                "ORDER BY Slot ASC",
                nCOSID);
        std::string strSlotQuery(szQuery);
        CDBRecordList listSlots;
        if (!pSlotTable->SelectCustom(strSlotQuery, 4, &listSlots, 0))
        {
            LogPrintf(0x1000000, "Pet Detail Information Inventory ItemSlot Query Failed!!");
            DB_FreeRecordList(&listSlots, pSlotTable);
            return false;
        }
        CDBRecordList listItems;
        CDBTable* pItemTable = GetTable(SHARD_TABLE_ITEMS);
        if (!pItemTable)
            BSAssertFail();
        if (listSlots.size())
        {
            StringCchPrintfA(szQuery, 0xC00,
                    "SELECT IT.* FROM _InvCOS INV JOIN _ITEMS IT ON INV.ItemID=IT.ID64 "
                    "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tWHERE INV.COSID=%d AND INV.ItemID IS NOT NULL "
                    "AND INV.ItemID<>0 ORDER BY INV.Slot ASC",
                    nCOSID);
            std::string strItemQuery(szQuery);
            if (!pItemTable->SelectCustom(strItemQuery, 1, &listItems, 0))
            {
                LogPrintf(0x1000000, "Pet Detail Information Item Query Failed!!");
                DB_FreeRecordList(&listSlots, pSlotTable);
                if (listItems.size())
                    DB_FreeRecordList(&listItems, pItemTable);
                return false;
            }
        }
        pMsg = m_pResponse;
        pMsg->Write(&byOK, 1);
        pMsg->Write(&m_byQueryTarget, 1);
        BYTE byCount = (BYTE)listItems.size();
        m_pResponse->Write(&byCount, 1);
        AQ_CharDataSelector_WriteItemList(&listItems, m_pResponse, &listSlots);
        DB_FreeRecordList(&listSlots, pSlotTable);
        DB_FreeRecordList(&listItems, pItemTable);
        return true;
    }
    if (m_byQueryTarget != 9)
        return true;

    // The latest pet of the character, then the pets of the summon items in its inventory and
    // in the storage of the account.
    // BUG(SM-352): when an item query fails, the pet records (and, for the storage query, the
    // inventory items) are never returned to their tables.
    CDBTable* pCOSTable = GetTable(SHARD_TABLE_CHAR_COS);
    if (!pCOSTable)
    {
        BSAssertFail();
        return false;
    }
    CDBRecordList listCOS;
    char szQuery[0xC00];
    StringCchPrintfA(szQuery, 0xC00,
            "SELECT TOP 1 * FROM _CHARCOS WHERE OWNERCHARID = %d ORDER BY ID DESC", nCharID);
    std::string strQuery(szQuery);
    if (!pCOSTable->SelectCustom(strQuery, 4, &listCOS, 0))
    {
        DB_FreeRecordList(&listCOS, pCOSTable);
        return false;
    }
    if (listCOS.size() > 1)
        BSAssertFail();
    WORD wCOSCount = (WORD)listCOS.size();
    WORD wInventoryCount = 0;
    WORD wChestCount = 0;

    CDBRecordList listInventory;
    CDBRecordList listChest;
    CDBTable* pItemTable = GetTable(SHARD_TABLE_ITEMS);
    if (StringCchPrintfA(szQuery, 0xC00,
            "SELECT IT.* FROM _Inventory INV JOIN _ITEMS IT ON INV.ItemID=IT.ID64 "
            "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tWHERE INV.CharID=%d AND INV.ItemID<>0 ORDER BY "
            "INV.Slot ASC",
            nCharID) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    strQuery = szQuery;
    if (!pItemTable->SelectCustom(strQuery, 4, &listInventory, 0))
    {
        if (listInventory.size())
            DB_FreeRecordList(&listInventory, pItemTable);
        return false;
    }
    if (StringCchPrintfA(szQuery, 0xC00,
            "SELECT IT.* FROM _Chest C JOIN _ITEMS IT ON C.ItemID=IT.ID64 "
            "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tWHERE C.UserJID=%d AND C.ItemID IS NOT NULL AND "
            "C.ItemID<>0 ORDER BY C.Slot ASC",
            nJID) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    strQuery = szQuery;
    if (!pItemTable->SelectCustom(strQuery, 4, &listChest, 0))
    {
        if (listChest.size())
            DB_FreeRecordList(&listChest, pItemTable);
        return false;
    }
    if (!LoadPetItem(&listInventory, &listCOS, &wInventoryCount) ||
        !LoadPetItem(&listChest, &listCOS, &wChestCount))
    {
        DB_FreeRecordList(&listCOS, pCOSTable);
        DB_FreeRecordList(&listInventory, pItemTable);
        DB_FreeRecordList(&listChest, pItemTable);
        return false;
    }

    pMsg = m_pResponse;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg = m_pResponse;
    pMsg->Write(&wCOSCount, 2);
    pMsg->Write(&wInventoryCount, 2);
    pMsg->Write(&wChestCount, 2);
    // Per pet {ID, RefCharID, name, state, level, experience, HGP, rent end time}.
    std::string strRentEnd;
    for (CDBRecordList::iterator it = listCOS.begin(); it != listCOS.end(); ++it)
    {
        CInstanceCOS* pPet = (CInstanceCOS*)*it;
        pPet->ResolveRefObj();
        pMsg = m_pResponse;
        pMsg->Write(&pPet->ID, 4);
        pMsg->Write(&pPet->RefCharID, 4);
        GM_FormatTimestamp(&strRentEnd, &pPet->RentEndTime);
        pMsg = m_pResponse;
        pMsg->WriteString(pPet->CharName.c_str());
        pMsg->Write(&pPet->State, 1);
        pMsg->Write(&pPet->Lvl, 1);
        pMsg->Write(&pPet->ExpOffset, 8);
        pMsg->Write(&pPet->HGP, 2);
        pMsg->WriteString(strRentEnd.c_str());
    }
    DB_FreeRecordList(&listCOS, pCOSTable);
    DB_FreeRecordList(&listInventory, pItemTable);
    DB_FreeRecordList(&listChest, pItemTable);
    return true;
}
