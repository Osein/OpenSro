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
