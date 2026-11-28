#include "ShardManager/AQLogSelector.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/RefDataObj.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/AQDBLoaders.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

void AQ_WriteText(CMassiveMsg* pMsg, const char* psz)
{
    WORD wLen = (WORD)lstrlenA(psz);
    pMsg->Write(&wLen, 2);
    if (wLen)
        pMsg->Write(psz, wLen);
}

void AQ_WriteTimeText(CMassiveMsg* pMsg, const TIMESTAMP_STRUCT& time)
{
    char szTime[0x80];
    if (StringCchPrintfA(szTime, 0x80, "%d-%d-%d %d:%d:%d.%d", time.year, time.month, time.day,
                         time.hour, time.minute, time.second, time.fraction / 1000000) != S_OK)
    {
        BSAssertFail();
    }
    AQ_WriteText(pMsg, szTime);
}

void AQ_LogSelector::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_bySubType = 0;
    m_dwTop = 0;
    m_byLogType = 0xFF;
    m_strCharName.clear();
    m_strFrom.clear();
    m_strTo.clear();
    m_strFilterText.clear();
}

bool AQ_LogSelector::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    try
    {
        *pMsg >> m_byLogType;
        *pMsg >> m_bySubType;
        *pMsg >> m_strCharName;
        *pMsg >> m_strFrom;
        *pMsg >> m_strTo;
        *pMsg >> m_strFilterText;
        if (m_byLogType == AQ_LOG_ITEM_SERIAL)
            *pMsg >> m_dwTop;
    }
    catch (...)
    {
        LogPrintf(0x1000000, "Log Request Message is invalid");
        m_byResult = AQ_RESULT_INJECTION;
        return false;
    }
    BuildDescription();
    return true;
}

BOOL AQ_LogSelector::IsCharIdNotRequired() const
{
    return (m_byLogType == AQ_LOG_CHAR && m_bySubType == 9)
        || (m_byLogType == AQ_LOG_ITEM && m_bySubType == 17)
        || m_byLogType == AQ_LOG_ITEM_SERIAL;
}

bool AQ_LogSelector::Execute()
{
    BeginResponse(AQ_OPCODE_LOG_SELECT);
    AQ_WriteString(m_pResponse, m_strDesc);

    int nCharID = 0;
    if (!IsCharIdNotRequired())
    {
        nCharID = AQ_DB_GetCharIDByName(m_strCharName);
        if (!nCharID)
        {
            m_byResult = AQ_RESULT_NO_CHAR;
            return false;
        }
    }

    std::string strFilter;
    char szQuery[0xC00];
    CDBTable* pTable;
    switch (m_byLogType)
    {
    case AQ_LOG_CHAR:
        if (!g_pShardDB)
            BSAssertFail();
        pTable = ShardDB_GetLogTable(SHARD_TABLE_LOG_CHAR);
        if (m_bySubType == 9)
            nCharID = 0;
        if (StringCchPrintfA(szQuery, 0xC00,
                "SELECT TOP %d EventTime, EventID, Data1, Data2, EventPos, strDesc FROM "
                "_LOGEVENTCHAR WITH (NOLOCK) \t\t\t\t\t\t\t  WHERE CharID = %d AND EventTime >= "
                "'%s' AND EventTime <= '%s' ",
                50, nCharID, m_strFrom.c_str(), m_strTo.c_str()) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        BuildCharEventFilter(&strFilter);
        break;

    case AQ_LOG_ITEM:
        if (!g_pShardDB)
            BSAssertFail();
        pTable = ShardDB_GetLogTable(SHARD_TABLE_LOG_ITEM);
        if (m_bySubType == 17)
            nCharID = 0;
        if (StringCchPrintfA(szQuery, 0xC00,
                "SELECT TOP %d EventTime,ItemRefID,dwData,TargetStorage,Operation,Slot_From,"
                "Slot_To, EventPos, strDesc, Serial64, Gold \t\t\t\t\t\t\t\t  FROM _LOGEVENTITEM "
                "WITH (NOLOCK) WHERE CharID = %d AND EventTime >= '%s' AND EventTime <= '%s' ",
                50, nCharID, m_strFrom.c_str(), m_strTo.c_str()) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        BuildItemEventFilter(&strFilter);
        break;

    case AQ_LOG_QUEST:
    {
        if (!g_pShardDB)
            BSAssertFail();
        pTable = ShardDB_GetLogTable(SHARD_TABLE_LOG_CHAR);
        char szFilter[0xC00];
        if (StringCchPrintfA(szQuery, 0xC00,
                "SELECT TOP %d EventTime, EventID, Data1, Data2, EventPos, strDesc FROM "
                "_LOGEVENTCHAR WITH (NOLOCK) \t\t\t\t\t\t\t  WHERE CharID = %d AND EventTime >= "
                "'%s' AND EventTime <= '%s' ",
                50, nCharID, m_strFrom.c_str(), m_strTo.c_str()) != S_OK
            || StringCchPrintfA(szFilter, 0xC00,
                "AND EventID >= %d AND EventID <= %d ORDER BY EVENTTIME", 200, 204) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        strFilter = szFilter;
        break;
    }

    case AQ_LOG_ITEM_SERIAL:
    {
        char szTop[0x80] = { 0 };
        if (StringCchPrintfA(szTop, 0x80, "TOP %d", m_dwTop) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        char szTime[0x80] = { 0 };
        if (StringCchPrintfA(szTime, 0x80, " AND EventTime >= '%s' AND EventTime <= '%s'",
                             m_strFrom.c_str(), m_strTo.c_str()) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        // GetSingleton_CShardDB
        if (!g_pShardDB)
            BSAssertFail();
        pTable = ShardDB_GetLogTable(SHARD_TABLE_LOG_ITEM);
        // Without a row limit the time range is left out too.
        const char* pszTop = m_dwTop ? szTop : "";
        const char* pszTime = m_dwTop ? szTime : "";
        if (StringCchPrintfA(szQuery, 0xC00,
                "SELECT %s EventTime,ItemRefID,dwData,TargetStorage,Operation,Slot_From,Slot_To, "
                "EventPos, strDesc, Serial64, Gold \t\t\t\tFROM _LOGEVENTITEM WITH (NOLOCK) WHERE "
                "Serial64 = %s %s ORDER by EVENTTIME",
                pszTop, m_strFilterText.c_str(), pszTime) != S_OK)
        {
            BSAssertFail();
            return false;
        }
        break;
    }

    default:
        m_byResult = AQ_RESULT_UNKNOWN_LOG;
        LogPrintf(0x1000000, "Unknown LogType is Requested!! [%d]", m_byLogType);
        return false;
    }

    if (!pTable)
    {
        BSAssertFail();
        m_byResult = AQ_RESULT_FAILED;
        return false;
    }

    // BUG(SM-313): the selected records are never returned to their table.
    CDBRecordList listRows;
    std::string strQuery(szQuery);
    if (strFilter.size())
        strQuery.append(strFilter, 0, std::string::npos);
    if (!pTable->SelectCustom(strQuery, 3, &listRows, 1))
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }

    CMassiveMsg* pMsg = m_pResponse;
    WORD wCount = (WORD)listRows.size();
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byLogType, 1);
    pMsg->Write(&m_bySubType, 1);
    pMsg->Write(&wCount, 2);
    WriteLogRows(m_pResponse, &listRows);
    return true;
}

void AQ_LogSelector::BuildCharEventFilter(std::string* pstrFilter)
{
    char szFilter[0x200];
    szFilter[0] = 0;
    HRESULT hr = S_OK;
    switch (m_bySubType)
    {
    case 0:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d) ORDER BY EVENTTIME",
                              1, 2, 3);
        break;
    case 1:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND EventID IN (%d,%d,%d,%d,%d) ORDER BY EVENTTIME",
                              4, 5, 6, 9, 11);
        break;
    case 2:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d) ORDER BY EVENTTIME",
                              19, 20, 21);
        break;
    case 3:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d) ORDER BY EVENTTIME",
                              22, 23);
        break;
    case 4:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d) ORDER BY EVENTTIME",
                              24, 25);
        break;
    case 6:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d) ORDER BY EVENTTIME",
                              90, 91, 92);
        break;
    case 7:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND EventID >= %d AND EventID <= %d ORDER BY EVENTTIME", 60, 85);
        break;
    case 8:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND EventID >= %d AND EventID <= %d ORDER BY EVENTTIME", 26, 29);
        break;
    case 9:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND CharID = 0 AND EventID = %d ORDER BY EVENTTIME", 199);
        break;
    case 13:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND EventID >= %d AND EventID <= %d ORDER BY EVENTTIME", 102, 115);
        break;
    case 14:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND EventID IN (%d,%d,%d,%d,%d,%d,%d) ORDER BY EVENTTIME",
                              230, 231, 232, 233, 235, 236, 237);
        break;
    case 17:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID = %d ORDER BY EVENTTIME", 239);
        break;
    case 18:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID = %d ORDER BY EVENTTIME", 240);
        break;
    case 19:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID = %d ORDER BY EVENTTIME", 243);
        break;
    default:
        break;
    }
    if (hr != S_OK)
        BSAssertFail();

    if (lstrlenA(szFilter))
        pstrFilter->assign(szFilter, strlen(szFilter));
    else
        pstrFilter->assign(" ORDER BY EVENTTIME", 0x13);
}

namespace
{

// The item row ID of a code name, 0 when there is none.
int FindItemIdByCodeName(const std::string& strCodeName)
{
    RefObjCommonRow* pObj = CReferenceData::GetInstance()->FindRefObjByCodeName(strCodeName);
    return pObj ? pObj->ID : 0;
}

} // namespace

void AQ_LogSelector::BuildItemEventFilter(std::string* pstrFilter)
{
    char szFilter[0x200];
    szFilter[0] = 0;
    HRESULT hr = S_OK;
    switch (m_bySubType)
    {
    case 0:
        hr = StringCchPrintfA(szFilter, 0x200,
                "AND ((Operation >= %d AND Operation <= %d) OR Operation IN (%d,%d)) AND "
                "TargetStorage = %d ORDER BY EVENTTIME",
                10, 15, 40, 41, 0);
        break;
    case 1:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d,%d,%d) ORDER BY EVENTTIME",
                              30, 31, 34);
        break;
    case 2:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d,%d) ORDER BY EVENTTIME",
                              32, 33);
        break;
    case 3:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation >= %d AND Operation <= %d ORDER BY EVENTTIME", 70, 78);
        break;
    case 4:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation >= %d AND Operation <= %d ORDER BY EVENTTIME", 50, 60);
        break;
    case 5:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation = %d ORDER BY EVENTTIME", 80);
        break;
    case 6:
        hr = StringCchPrintfA(szFilter, 0x200,
                "AND ((Operation >= %d AND Operation <= %d AND TargetStorage = %d) OR "
                "(Operation >= %d AND Operation <= %d)) ORDER BY EVENTTIME",
                10, 15, 3, 20, 23);
        break;
    case 7:
        hr = StringCchPrintfA(szFilter, 0x200, "AND TargetStorage = %d ORDER BY EVENTTIME", 2);
        break;
    case 8:
        hr = StringCchPrintfA(szFilter, 0x200,
                "AND Operation IN (%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d) ORDER BY EVENTTIME",
                90, 91, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139);
        break;
    case 9:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation >= %d AND Operation <= %d ORDER BY EVENTTIME",
                              100, 102);
        break;
    case 12:
        hr = StringCchPrintfA(szFilter, 0x200,
                "AND ((Operation >= %d AND Operation <= %d AND TargetStorage = %d) OR "
                "(Operation >= %d AND Operation <= %d)) ORDER BY EVENTTIME",
                10, 15, 4, 119, 118);
        break;
    case 13:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation >= %d AND Operation <= %d ORDER BY EVENTTIME",
                              141, 144);
        break;
    case 15:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation IN (%d,%d,%d,%d,%d,%d) ORDER BY EVENTTIME",
                              163, 164, 165, 166, 167, 168);
        break;
    case 16:
    {
        // An empty or unknown code name leaves the filter empty (no assert).
        if (!m_strFilterText.size())
            break;
        int nRefItemID = FindItemIdByCodeName(std::string(m_strFilterText.c_str()));
        if (!nRefItemID)
            break;
        hr = StringCchPrintfA(szFilter, 0x200, "AND ItemRefID = %d ORDER BY EVENTTIME",
                              nRefItemID);
        break;
    }
    case 17:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation = %d ORDER BY EVENTTIME", 99);
        break;
    case 18:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d,%d) ORDER BY EVENTTIME",
                              112, 113);
        break;
    case 19:
    {
        RefObjCommonRow* pGoldTime = CReferenceData::GetInstance()->FindRefObjByCodeName(
            std::string("ITEM_MALL_PREMIUM_GOLDTIME"));
        if (!pGoldTime)
        {
            BSAssertFail();
            break;
        }
        int nGoldTimeID = pGoldTime->ID;
        RefObjCommonRow* pGoltam = CReferenceData::GetInstance()->FindRefObjByCodeName(
            std::string("ITEM_PRE_GOLTAM_4W"));
        if (!pGoltam)
        {
            BSAssertFail();
            break;
        }
        hr = StringCchPrintfA(szFilter, 0x200,
                "and  ( Operation IN (%d, %d, %d)  or ( Operation IN (%d, %d, %d, %d, %d, %d, "
                "%d) AND ItemRefID IN (%d, %d)) ) ORDER BY EVENTTIME",
                121, 122, 123, 100, 101, 102, 103, 104, 110, 111, nGoldTimeID, pGoltam->ID);
        break;
    }
    case 20:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d) ORDER BY EVENTTIME", 42);
        break;
    case 21:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation = %d ORDER BY EVENTTIME", 114);
        break;
    case 22:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation = %d ORDER BY EVENTTIME", 35);
        break;
    case 23:
        // The original passes a sixth, unused argument (5).
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d, %d, %d) ORDER BY EVENTTIME",
                              146, 147, 148, 5);
        break;
    case 24:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d, %d, %d) ORDER BY EVENTTIME",
                              125, 126, 127);
        break;
    case 25:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d, %d, %d) ORDER BY EVENTTIME",
                              153, 151, 152);
        break;
    case 26:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d) ORDER BY EVENTTIME", 159);
        break;
    case 27:
        hr = StringCchPrintfA(szFilter, 0x200, "AND Operation IN (%d,%d) ORDER BY EVENTTIME",
                              169, 170);
        break;
    case 33:
        hr = StringCchPrintfA(szFilter, 0x200,
                              "AND Operation IN (%d, %d, %d, %d,%d) ORDER BY EVENTTIME",
                              154, 155, 156, 157, 158);
        break;
    default:
        break;
    }
    if (hr != S_OK)
        BSAssertFail();

    if (lstrlenA(szFilter))
        pstrFilter->assign(szFilter, strlen(szFilter));
    else
        pstrFilter->assign(" ORDER BY EVENTTIME", 0x13);
}

void AQ_LogSelector::WriteLogRows(CMassiveMsg* pMsg, CDBRecordList* pList)
{
    CDBRecord* pLast = NULL;
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        pLast = *it;
        switch (m_byLogType)
        {
        case AQ_LOG_CHAR:
        case AQ_LOG_QUEST:
        {
            CInstanceLogChar* pRow = (CInstanceLogChar*)pLast;
            pMsg->Write(&pRow->EventTime, 16);
            pMsg->Write(&pRow->EventID, 1);
            pMsg->Write(&pRow->Data1, 4);
            pMsg->Write(&pRow->Data2, 4);
            AQ_WriteString(pMsg, pRow->EventPos);
            AQ_WriteString(pMsg, pRow->strDesc);
            break;
        }
        case AQ_LOG_ITEM:
        case AQ_LOG_ITEM_SERIAL:
        {
            CInstanceLogItem* pRow = (CInstanceLogItem*)pLast;
            pMsg->Write(&pRow->Serial64, 8);
            pMsg->Write(&pRow->EventTime, 16);
            pMsg->Write(&pRow->ItemRefID, 4);
            pMsg->Write(&pRow->dwData, 4);
            pMsg->Write(&pRow->TargetStorage, 1);
            pMsg->Write(&pRow->Operation, 1);
            pMsg->Write(&pRow->Slot_From, 1);
            pMsg->Write(&pRow->Slot_To, 1);
            AQ_WriteString(pMsg, pRow->EventPos);
            AQ_WriteString(pMsg, pRow->strDesc);
            pMsg->Write(&pRow->Gold, 8);
            break;
        }
        default:
            BSAssertFail();
            return;
        }
    }

    // Both row classes keep EventTime at +18.
    if (pList->size() && pLast)
        AQ_WriteTimeText(pMsg, ((CInstanceLogChar*)pLast)->EventTime);
}

void AQ_LogSelector::BuildDescription()
{
    m_strDesc.assign("Querying Log", 0xC);
    const char* pszSub = NULL;
    switch (m_byLogType)
    {
    case AQ_LOG_CHAR:
        m_strDesc.append(" [Character]", 0xC);
        switch (m_bySubType)
        {
        case 0:     pszSub = " (Create and Delete)"; break;
        case 1:     pszSub = " (Login and Logout)"; break;
        case 2:     pszSub = " (Dead or Alive)"; break;
        case 3:     pszSub = " (Level or Exp)"; break;
        case 4:     pszSub = " (Stat Change)"; break;
        case 5:     pszSub = " (Item Exchange)"; break;
        case 6:     pszSub = " (Tri-Job)"; break;
        case 7:     pszSub = " (Guild)"; break;
        case 8:     pszSub = " (Skill)"; break;
        case 9:     pszSub = " (SMC Op.)"; break;
        case 13:    pszSub = " (Training Camp)"; break;
        case 14:    pszSub = " (SiegeFortress)"; break;
        case 17:    pszSub = " (Battle Arena)"; break;
        case 18:    pszSub = " (Event Skill Exp)"; break;
        case 0xFF:  pszSub = " (ALL)"; break;
        default:    pszSub = " (INVALID!!!)"; break;
        }
        m_strDesc.append(pszSub);
        break;

    case AQ_LOG_ITEM:
        m_strDesc.append(" [Item]", 7);
        switch (m_bySubType)
        {
        case 0:     pszSub = " (Inventory)"; break;
        case 1:     pszSub = " (Drop and Pick)"; break;
        case 2:     pszSub = " (Shop)"; break;
        case 3:     pszSub = " (Flea Market)"; break;
        case 4:     pszSub = " (Exchange)"; break;
        case 5:     pszSub = " (Gold)"; break;
        case 6:     pszSub = " (Chest)"; break;
        case 7:     pszSub = " (COS)"; break;
        case 8:     pszSub = " (Alchemy)"; break;
        case 9:     pszSub = " (Premium Exp)"; break;
        case 12:    pszSub = " (Guild Chest)"; break;
        case 13:    pszSub = " (Flea Market Network)"; break;
        case 15:    pszSub = " (Open Market)"; break;
        case 16:    pszSub = " (CodeName)"; break;
        case 17:    pszSub = " (SMC Op.)"; break;
        case 21:    pszSub = " (COS Pick)"; break;
        case 22:    pszSub = " (Item Mall)"; break;
        case 23:    pszSub = " (Avatar Inventory)"; break;
        case 25:    pszSub = " (Avatar Special Item)"; break;
        case 26:    pszSub = " (Socket)"; break;
        case 27:    pszSub = " (Open Market Ticket)"; break;
        case 33:    pszSub = " (Magic Cube)"; break;
        case 0xFF:  pszSub = " (ALL)"; break;
        default:    pszSub = " (INVALID!!!)"; break;
        }
        m_strDesc.append(pszSub);
        break;

    case AQ_LOG_QUEST:
        m_strDesc.append(" [Quest]", 8);
        break;

    case AQ_LOG_ITEM_SERIAL:
        m_strDesc.append(" [Item Serial History]", 0x16);
        break;

    default:
        // The whole text is replaced.
        m_strDesc.assign(" [INVALID!!!]", 0xD);
        break;
    }

    // Util_StringConcatPrefix (0x46f450) and Util_StringAppendCloseBracket (0x46f520)
    std::string strName = std::string(" : [ CharName ") + m_strCharName;
    std::string strPart = strName + " ]";
    m_strDesc.append(strPart, 0, std::string::npos);
}
