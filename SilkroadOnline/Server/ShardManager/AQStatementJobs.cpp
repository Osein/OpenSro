#include "ShardManager/AQStatementJobs.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

template <class T>
int CAQStatementJobT<T>::RunStatement(AQ_DBQuery& query, void* pRow, int nRowSize)
{
    ServerFramework::CClassTrigger<T> trigger(static_cast<T*>(this));
    trigger.m_pfnTrigger2 = &T::OnQueryRow;
    query.Setup(&trigger, 4);
    if (pRow)
        query.SetRow(pRow, nRowSize);
    // BUG(SM-307): the statement text is passed as the format string.
    return query.ExecuteFormat(m_strQuery.c_str());
}

// -------------------------------------------------------------------------------------------
// AQ_Statistics

bool AQ_Statistics::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_strQuery.clear();
    *pMsg >> m_byQueryTarget;

    // BUG(SM-308): the names and dates of the request go into the statement unchecked.
    char szQuery[0xC00];
    HRESULT hr;
    BYTE byValue = 0;
    std::string strFirst;
    std::string strSecond;
    switch (m_byQueryTarget)
    {
    case 1:
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_GoldAmount");
        break;
    case 2:
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_LevelDistribution %d", byValue);
        break;
    case 3:
        *pMsg >> strFirst;
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_CharLastLogout '%s',%d",
                              strFirst.c_str(), byValue);
        break;
    case 4:
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00,
                              "select top %d CharName16,CurLevel,MaxLevel,ExpOffset from _Char "
                              "with(NOLOCK) order by MaxLevel desc",
                              byValue > 100 ? 100 : byValue);
        break;
    case 5:
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_GoldAmount_Level %d", byValue);
        break;
    case 6:
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_Guild");
        break;
    case 7:
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00,
                              "select top %d CharName16, RemainGold from _Char with(NOLOCK) "
                              "where CharID <> 0 order by RemainGold desc",
                              byValue > 100 ? 100 : byValue);
        break;
    case 8:
        *pMsg >> strFirst;
        *pMsg >> strSecond;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _CASHITEM_STATISTIC '%s', '%s'",
                              strFirst.c_str(), strSecond.c_str());
        break;
    case 9:
        hr = StringCchPrintfA(szQuery, 0xC00,
                              "select RefObjID, count(RefObjID) from _char with (nolock) "
                              "group by RefObjID order by count(RefObjID) desc");
        break;
    case 10:
        // The count is read but the procedure takes no argument.
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _QueryStatics_UserGoldRanking",
                              byValue > 100 ? 100 : byValue);
        break;
    case 11:
        hr = StringCchPrintfA(szQuery, 0xC00,
                              "select * from _TrijobRewards with (nolock) order by JobType desc");
        break;
    case 13:
        *pMsg >> byValue;
        *pMsg >> strFirst;
        *pMsg >> strSecond;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _SMC_GOLD_INCREMENT_BY_TRADE %d, '%s', '%s'",
                              byValue, strFirst.c_str(), strSecond.c_str());
        break;
    case 25:
        if (StringCchPrintfA(szQuery, 0xC00, "exec _TrainingCampRanking") != S_OK)
        {
            LogPrintf(0x2000000, "Not exist procedure _TrainingCampRanking");
            return false;
        }
        hr = S_OK;
        break;
    case 38:
        *pMsg >> strFirst;
        *pMsg >> byValue;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _Query_Statistics_RentItem '%s', %d",
                              strFirst.c_str(), byValue);
        break;
    case 39:
        *pMsg >> strFirst;
        *pMsg >> strSecond;
        hr = StringCchPrintfA(szQuery, 0xC00, "exec _OPEN_MARKET_STATISTIC '%s', '%s'",
                              strFirst.c_str(), strSecond.c_str());
        break;
    default:
        pMsg->SkipRemaining();
        m_byResult = 10;
        return false;
    }

    if (hr != S_OK)
    {
        BSAssertFail();
        return false;
    }
    m_strQuery.assign(szQuery, strlen(szQuery));
    BuildDescription();
    return true;
}

void AQ_Statistics::BuildDescription()
{
    m_strDesc.assign("Querying Statistics", 0x13);
    switch (m_byQueryTarget)
    {
    case 1:     m_strDesc.append(" (GoldAmount)"); break;
    case 2:     m_strDesc.append(" (LvDistribution)"); break;
    case 3:     m_strDesc.append(" (LastLogout)"); break;
    case 4:     m_strDesc.append(" (LvRanking)"); break;
    case 5:     m_strDesc.append(" (LvGoldAmount)"); break;
    case 6:     m_strDesc.append(" (Guild)"); break;
    case 7:     m_strDesc.append(" (CharGoldRanking)"); break;
    case 8:     m_strDesc.append(" (CashItemRanking)"); break;
    case 9:     m_strDesc.append(" (Character Distribution)"); break;
    case 10:    m_strDesc.append(" (AccountGoldRanking)"); break;
    case 11:    m_strDesc.append(" (Current Trijob Rewards)"); break;
    case 13:    m_strDesc.append(" (Gold Increment by Trade)"); break;
    case 25:    m_strDesc.append(" (HonorRanking)"); break;
    case 39:    m_strDesc.append(" (OpenMarketRanking)"); break;
    default:    m_strDesc.append(" (INVALID!!!)", 13); break;
    }
}

bool AQ_Statistics::Execute()
{
    BeginResponse(AQ_OPCODE_STATISTICS);
    AQ_WriteString(m_pResponse, m_strDesc);

    // Row layouts of the console per target.
    BYTE abRow[149];
    int nRowSize;
    switch (m_byQueryTarget)
    {
    case 1:     nRowSize = 16; break;
    case 2:     nRowSize = 14; break;
    case 3:     nRowSize = 42; break;
    case 4:     nRowSize = 75; break;
    case 5:     nRowSize = 22; break;
    case 6:     nRowSize = 68; break;
    case 7:     nRowSize = 72; break;
    case 8:     nRowSize = 8; break;
    case 9:     nRowSize = 8; break;
    case 10:    nRowSize = 136; break;
    case 11:    nRowSize = 9; break;
    case 13:    nRowSize = 56; break;
    case 25:    nRowSize = 69; break;
    case 38:    nRowSize = 149; break;
    case 39:    nRowSize = 8; break;
    default:    nRowSize = 0; break;
    }

    AQ_DBQuery query;
    if (RunStatement(query, nRowSize ? abRow : NULL, nRowSize) < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    if (query.m_bNoRow)
    {
        m_byResult = AQ_RESULT_NOT_FOUND;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

int AQ_Statistics::OnQueryRow(DWORD dwQuery, DWORD)
{
    return AQ_WriteQueryRow(this, m_pResponse, dwQuery);
}

void AQ_Statistics::WriteRowHeader(CMassiveMsg* pMsg)
{
    BYTE byRow = 1;
    pMsg->Write(&byRow, 1);
    pMsg->Write(&m_byQueryTarget, 1);
}

// -------------------------------------------------------------------------------------------
// AQ_TrijobRanking

bool AQ_TrijobRanking::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_strQuery.clear();
    *pMsg >> m_byQueryTarget;
    *pMsg >> m_bySubTarget;

    BYTE byJob = m_byQueryTarget;
    if (byJob != 1 && byJob != 3 && byJob != 2)
    {
        m_byResult = 10;
        return false;
    }
    if (m_bySubTarget >= 2)
    {
        m_byResult = 10;
        return false;
    }

    std::string strJob;
    std::string strKind;
    switch (m_byQueryTarget)
    {
    case 1: strJob.assign("Trader", 6); break;
    case 2: strJob.assign("Robber", 6); break;
    case 3: strJob.assign("Hunter", 6); break;
    }
    if (m_bySubTarget == 0)
        strKind.assign("Activity", 8);
    else if (m_bySubTarget == 1)
        strKind.assign("Contribution", 0xC);

    std::string strColumns;
    strColumns.assign("Rank, NickName, JobLevel, ", 0x1A);
    if (m_bySubTarget == 0)
        strColumns.append("JobExp", 6);
    else if (m_bySubTarget == 1)
        strColumns.append("Contribution", 0xC);

    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00, "select %s from Tab_RefRanking_%s%s order by rank desc",
                         strColumns.c_str(), strJob.c_str(), strKind.c_str()) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    m_strQuery.assign(szQuery, strlen(szQuery));
    BuildDescription();
    return true;
}

void AQ_TrijobRanking::BuildDescription()
{
    m_strDesc.assign("Querying Trijob Ranking", 0x17);
    switch (m_byQueryTarget)
    {
    case 1:     m_strDesc.append(" (Trader)", 9); break;
    case 2:     m_strDesc.append(" (Robber)", 9); break;
    case 3:     m_strDesc.append(" (Hunter)", 9); break;
    default:    m_strDesc.append(" (INVALID!!!)", 0xD); break;
    }
    switch (m_bySubTarget)
    {
    case 0:     m_strDesc.append(" (Activity)", 0xB); break;
    case 1:     m_strDesc.append(" (Contribution)", 0xF); break;
    default:    m_strDesc.append(" (INVALID!!!)", 0xD); break;
    }
}

bool AQ_TrijobRanking::Execute()
{
    BeginResponse(AQ_OPCODE_TRIJOB_RANKING);
    AQ_WriteString(m_pResponse, m_strDesc);

    BYTE abRow[23];
    AQ_DBQuery query;
    if (RunStatement(query, abRow, sizeof(abRow)) < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    if (query.m_bNoRow)
    {
        m_byResult = AQ_RESULT_NOT_FOUND;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

int AQ_TrijobRanking::OnQueryRow(DWORD dwQuery, DWORD)
{
    return AQ_WriteQueryRow(this, m_pResponse, dwQuery);
}

void AQ_TrijobRanking::WriteRowHeader(CMassiveMsg* pMsg)
{
    BYTE byRow = 1;
    pMsg->Write(&byRow, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    pMsg->Write(&m_bySubTarget, 1);
}

// -------------------------------------------------------------------------------------------
// AQ_UserDataSelector

void AQ_UserDataSelector::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_strDesc.clear();
    m_byQueryTarget = 0xFF;
}

bool AQ_UserDataSelector::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;
    m_strDesc.clear();

    std::string strName;
    *pMsg >> m_byQueryTarget;
    *pMsg >> strName;

    // BUG(SM-308): the name goes into the statement unchecked.
    char szQuery[0xC00];
    HRESULT hr;
    switch (m_byQueryTarget)
    {
    case 1:
        hr = StringCchPrintfA(szQuery, 0xC00, "_QueryUserCharByUserID '%s'", strName.c_str());
        break;
    case 2:
        hr = StringCchPrintfA(szQuery, 0xC00, "_QueryUserCharByCharName '%s'", strName.c_str());
        break;
    case 3:
        hr = StringCchPrintfA(szQuery, 0xC00, "_QueryUserCharByNickName '%s'", strName.c_str());
        break;
    default:
        pMsg->SkipRemaining();
        m_byResult = 10;
        return false;
    }
    if (hr != S_OK)
    {
        BSAssertFail();
        return false;
    }
    m_strQuery.assign(szQuery, strlen(szQuery));

    char szDesc[0xFF];
    if (m_byQueryTarget == 1)
        hr = StringCchPrintfA(szDesc, 0xFF, "Querying Character List of User [UserID: %s]",
                              strName.c_str());
    else if (m_byQueryTarget == 2)
        hr = StringCchPrintfA(szDesc, 0xFF, "Querying Character List of User [CharName: %s]",
                              strName.c_str());
    else
        hr = StringCchPrintfA(szDesc, 0xFF, "Querying User Info by [NickName: %s]",
                              strName.c_str());
    if (hr != S_OK)
    {
        BSAssertFail();
        return false;
    }
    m_strDesc.assign(szDesc, strlen(szDesc));
    return true;
}

bool AQ_UserDataSelector::Execute()
{
    BeginResponse(AQ_OPCODE_USER_DATA_SELECT);
    AQ_WriteString(m_pResponse, m_strDesc);

    BYTE abRow[192];
    AQ_DBQuery query;
    if (RunStatement(query, abRow, sizeof(abRow)) < 0 || query.m_bNoRow == 1)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    BYTE byEnd = 0;
    m_pResponse->Write(&byEnd, 1);
    return true;
}

int AQ_UserDataSelector::OnQueryRow(DWORD dwQuery, DWORD)
{
    return AQ_WriteQueryRow(this, m_pResponse, dwQuery);
}

void AQ_UserDataSelector::WriteRowHeader(CMassiveMsg* pMsg)
{
    BYTE byRow = 1;
    pMsg->Write(&byRow, 1);
    pMsg->Write(&m_byQueryTarget, 1);
    // The shard ID of the body of this server (g_pSelfBody 0x8588B4).
    m_pResponse->Write(&g_pSelfBody->pShard->wID, 2);
}
