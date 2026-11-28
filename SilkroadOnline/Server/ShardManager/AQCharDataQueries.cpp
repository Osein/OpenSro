// AQ_CharDataSelector: the queries that read shard records (information, items, skills).

#include "ShardManager/AQCharDataSelector.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
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
