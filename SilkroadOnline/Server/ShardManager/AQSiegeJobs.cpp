#include "ShardManager/AQSiegeJobs.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/AQDBLoaders.h"
#include "ShardManager/AQLogSelector.h"
#include "ShardManager/Guild.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

enum
{
    AQ_SIEGE_TARGET_FORTRESS    = 1,
};

// -------------------------------------------------------------------------------------------
// AQ_SiegeDataSelector

void AQ_SiegeDataSelector::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_strDesc.clear();
    m_byQueryTarget = 0xFF;
}

bool AQ_SiegeDataSelector::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_strDesc.clear();
    try
    {
        *pMsg >> m_byQueryTarget;
        if (m_byQueryTarget == AQ_SIEGE_TARGET_FORTRESS)
            *pMsg >> m_dwFortressID;
    }
    catch (...)
    {
        LogPrintf(0x1000000, "SiegeData Request Message is invalid!!");
        m_byResult = AQ_RESULT_INJECTION;
        return false;
    }
    m_strDesc.assign("Siege Data", 0xA);
    if (m_byQueryTarget == AQ_SIEGE_TARGET_FORTRESS)
        m_strDesc.append(" (Fortress)", 0xB);
    else
        m_strDesc.append(" (INVALID!!!)", 0xD);
    return true;
}

bool AQ_SiegeDataSelector::Execute()
{
    BeginResponse(AQ_OPCODE_SIEGE_DATA_SELECT);
    AQ_WriteString(m_pResponse, m_strDesc);
    if (!g_pShardDB)
    {
        m_byResult = AQ_RESULT_FAILED;
        return false;
    }
    if (m_byQueryTarget != AQ_SIEGE_TARGET_FORTRESS)
    {
        m_byResult = AQ_RESULT_UNKNOWN_TARGET;
        LogPrintf(0x1000000, "Unknown QueryTarget is Request!!! [%d]", m_byQueryTarget);
        return false;
    }

    // BUG(SM-313): the selected records are never returned to their tables.
    CDBRecordList listFortress;
    CDBRecordList listStructs;
    CDBRecordList listMembers;
    CDBRecordList listRequests;
    m_byResult = AQ_DB_LoadFortressData(&listMembers, &listRequests, (int)m_dwFortressID,
                                        &listFortress, &listStructs);
    if (m_byResult != AQ_RESULT_OK)
        return false;

    CMassiveMsg* pMsg = m_pResponse;
    BYTE byOK = 1;
    pMsg->Write(&byOK, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    WriteFortressData(&listFortress, &listRequests, &listStructs, &listMembers, m_pResponse);
    return true;
}

void AQ_SiegeDataSelector::WriteFortressData(CDBRecordList* pFortresses,
                                             CDBRecordList* pRequests, CDBRecordList* pStructs,
                                             CDBRecordList* pMembers, CMassiveMsg* pMsg)
{
    CInstanceSiegeFortress* pFortress = NULL;
    if (!pFortresses->empty())
    {
        pFortress = (CInstanceSiegeFortress*)pFortresses->front();
        std::string strGuild;
        if (pFortress->GuildID)
        {
            CGuildCommon* pGuild = GetSingleton_CGuildManager()->FindGuild(pFortress->GuildID);
            if (pGuild)
                pGuild->GetName(strGuild);
        }
        AQ_WriteText(pMsg, strGuild.size() ? strGuild.c_str() : "Not occupied yet");
        pMsg->Write(&pFortress->TaxRatio, 2);
        pMsg->Write(&pFortress->Tax, 8);
        AQ_WriteText(pMsg, pFortress->Introduction.c_str());
        pMsg->Write(&pFortress->IntroductionModificationPermission, 1);
    }
    else
    {
        // No intro text and permission in this case.
        AQ_WriteText(pMsg, "(ERROR: !!!PATCH_WARNING!!!)");
        short nTaxRatio = 0;
        __int64 nTax = 0;
        pMsg->Write(&nTaxRatio, 2);
        pMsg->Write(&nTax, 8);
    }

    // The members with siege authority: count, then {name, authority}.
    DWORD dwCount = 0;
    CDBRecordList::iterator it;
    for (it = pMembers->begin(); it != pMembers->end(); ++it)
    {
        if (((CInstanceGuildMember*)*it)->SiegeAuthority > 0)
            ++dwCount;
    }
    pMsg->Write(&dwCount, 4);
    for (it = pMembers->begin(); it != pMembers->end(); ++it)
    {
        CInstanceGuildMember* pMember = (CInstanceGuildMember*)*it;
        if (pMember->SiegeAuthority > 0)
        {
            WORD wLen = (WORD)pMember->CharName.size();
            pMsg->Write(&wLen, 2);
            if (wLen)
                pMsg->Write(pMember->CharName.c_str(), wLen);
            pMsg->Write(&pMember->SiegeAuthority, 1);
        }
    }

    dwCount = (DWORD)pStructs->size();
    pMsg->Write(&dwCount, 4);
    for (it = pStructs->begin(); it != pStructs->end(); ++it)
        pMsg->Write(&((CInstanceSiegeFortressStruct*)*it)->RefObjID, 4);

    if (pFortress)
    {
        pMsg->Write(&pFortress->NPCHired, 1);
    }
    else
    {
        BYTE byNone = 0;
        pMsg->Write(&byNone, 1);
    }

    // BUG(SM-316): a request of a guild that is not loaded is left out, the count is not.
    dwCount = (DWORD)pRequests->size();
    pMsg->Write(&dwCount, 4);
    for (it = pRequests->begin(); it != pRequests->end(); ++it)
    {
        CInstanceSiegeFortressRequest* pRequest = (CInstanceSiegeFortressRequest*)*it;
        // The guild manager is asserted before the GuildID check here.
        CGuildManager* pGuildMgr = GetSingleton_CGuildManager();
        if (!pRequest->GuildID)
            continue;
        CGuildCommon* pGuild = pGuildMgr->FindGuild(pRequest->GuildID);
        if (!pGuild)
            continue;
        std::string strGuild;
        pGuild->GetName(strGuild);
        pMsg->Write(&pRequest->RequestType, 1);
        AQ_WriteText(pMsg, strGuild.c_str());
    }
}

// -------------------------------------------------------------------------------------------
// AQ_SiegeLogSelector

bool AQ_SiegeLogSelector::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    try
    {
        *pMsg >> m_dwFortressID;
        *pMsg >> m_byEventType;
        *pMsg >> m_strFrom;
        *pMsg >> m_strTo;
    }
    catch (...)
    {
        LogPrintf(0x1000000, "Siege Log Request Message is invalid!!");
        m_byResult = AQ_RESULT_INJECTION;
        return false;
    }
    BuildDescription();
    return true;
}

bool AQ_SiegeLogSelector::Execute()
{
    BeginResponse(AQ_OPCODE_SIEGE_LOG_SELECT);
    AQ_WriteString(m_pResponse, m_strDesc);

    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = ShardDB_GetLogTable(SHARD_TABLE_LOG_SIEGE_FORTRESS);

    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00,
            "SELECT TOP %d FortressID, EventTime, EventID, CharID, Data1, Data2, strDesc FROM "
            "_LOGEVENTSIEGEFORTRESS WITH (NOLOCK) \t\t\t\t\t\t\t\t  WHERE FortressID = %d AND "
            "EventTime >= '%s' AND EventTime <= '%s' ",
            50, m_dwFortressID, m_strFrom.c_str(), m_strTo.c_str()) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    std::string strFilter;
    BuildEventFilter(&strFilter);
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
    pMsg->Write(&m_byEventType, 1);
    pMsg->Write(&wCount, 2);
    WriteLogRows(m_pResponse, &listRows);
    return true;
}

void AQ_SiegeLogSelector::WriteLogRows(CMassiveMsg* pMsg, CDBRecordList* pList)
{
    CInstanceLogSiegeFortress* pLast = NULL;
    for (CDBRecordList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        pLast = (CInstanceLogSiegeFortress*)*it;
        pMsg->Write(&pLast->FortressID, 4);
        pMsg->Write(&pLast->EventTime, 16);
        pMsg->Write(&pLast->EventID, 1);
        pMsg->Write(&pLast->CharID, 4);
        pMsg->Write(&pLast->Data1, 4);
        pMsg->Write(&pLast->Data2, 4);
        WORD wLen = (WORD)pLast->strDesc.size();
        pMsg->Write(&wLen, 2);
        if (wLen)
            pMsg->Write(pLast->strDesc.c_str(), wLen);
    }
    if (pList->size() && pLast)
        AQ_WriteTimeText(pMsg, pLast->EventTime);
}

void AQ_SiegeLogSelector::BuildDescription()
{
    m_strDesc.append(" [SiegeFortress]", 0x10);
    const char* pszType;
    switch (m_byEventType)
    {
    case 0:     pszType = " (Start and End)"; break;
    case 1:     pszType = " (Result Defense and Offense)"; break;
    case 2:     pszType = " (Temp Result Change Owner)"; break;
    case 3:     pszType = " (Struct)"; break;
    case 4:     pszType = " (Request)"; break;
    case 5:     pszType = " (Forge Item)"; break;
    case 6:     pszType = " (Object)"; break;
    case 7:     pszType = " (Success and Failed add Reward)"; break;
    case 9:     pszType = " (Bless Buff)"; break;
    case 10:    pszType = " (Create and Close Dungeon)"; break;
    case 11:    pszType = " (Entry Dungeon)"; break;
    case 12:    pszType = " (Success and Failed Quest Reward)"; break;
    case 250:   pszType = " (All)"; break;
    default:    pszType = " (INVALID!!!)"; break;
    }
    m_strDesc.append(pszType);
}

void AQ_SiegeLogSelector::BuildEventFilter(std::string* pstrFilter)
{
    char szFilter[0x200];
    szFilter[0] = 0;
    HRESULT hr = S_OK;
    switch (m_byEventType)
    {
    case 0:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d) ORDER BY EVENTTIME", 1, 2);
        break;
    case 1:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID = %d ORDER BY EVENTTIME", 3);
        break;
    case 2:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID = %d ORDER BY EVENTTIME", 4);
        break;
    case 3:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d,%d) ORDER BY EVENTTIME",
                              5, 6, 7, 8);
        break;
    case 4:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d) ORDER BY EVENTTIME", 9, 16);
        break;
    case 5:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d) ORDER BY EVENTTIME",
                              17, 18, 19);
        break;
    case 6:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d,%d) ORDER BY EVENTTIME",
                              20, 21, 22);
        break;
    case 7:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EventID IN (%d,%d) ORDER BY EVENTTIME",
                              23, 24);
        break;
    case 9:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EVENTID = %d ORDER BY EVENTTIME", 10);
        break;
    case 10:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EVENTID IN (%d,%d) ORDER BY EVENTTIME",
                              12, 13);
        break;
    case 11:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EVENTID = %d ORDER BY EVENTTIME", 14);
        break;
    case 12:
        hr = StringCchPrintfA(szFilter, 0x200, "AND EVENTID IN(%d,%d) ORDER BY EVENTTIME", 26, 27);
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
