#include "ShardManager/ShardSchedule.h"

#include <string.h>

#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"
#include "ShardManager/ShardScheduleExtern.h"
#include "ShardManager/ShardScheduleJob.h"
#include "ShardManager/ShardTempleGate.h"
#include "ShardManager/ShardWorldExtern.h"
#include "ShardManager/SiegeFortress.h"

using NewSchedule::sScheduleEvent;
using NewSchedule::sScheduleInfo;
using NewSchedule::CScheduleGroup;
using NewSchedule::ISchedule;
using NewSchedule::SCHEDULE_DATA;
using NewSchedule::SCHEDULE_DEFINE_DATA;

// Static objects of the original (dynamic initializers 0x663500 and 0x6645c0).
CShard_ScheduleManager g_ShardScheduleManager;
CShard_ScheduleExecutor g_ShardScheduleExecutor;

// The head of 0x34FE: group, schedule, begin, end, state and define.
void ShardSchedule_WriteHead(CMsg* pMsg, const sScheduleInfo& info)
{
    pMsg->WriteBytes(&info.dwGroupID, 4);
    pMsg->WriteBytes(&info.dwScheduleID, 4);
    pMsg->WriteBytes(&info.dwBegin, 4);
    pMsg->WriteBytes(&info.dwEnd, 4);
    pMsg->WriteBytes(&info.bBegin, 4);
    *pMsg << info.strDefine;
}

// ---------------------------------------------------------------------------------------------
// CShard_ScheduleManager

void CShard_ScheduleManager::RebuildEventList()
{
    BS_ASSERT(g_pSiegeFortressMgr);
    CSiegeFortressMgr::InitSiegeScheduleTimes();
    NewSchedule::CScheduleManager::RebuildEventList();
}

// The bytes of a std::string as the original pushes it into the varargs of LogPrintf.
struct STRING_OBJECT_BYTES
{
    DWORD adw[sizeof(std::string) / sizeof(DWORD)];
};

C_ASSERT(sizeof(STRING_OBJECT_BYTES) == sizeof(std::string));

void CShard_ScheduleManager::OnServiceStartSchedule(sScheduleEvent& event)
{
    CScheduleGroup* pGroup = event.pGroup;
    // BUG(SM-131): the group is used before the NULL check.
    ISchedule* pSchedule = pGroup->FindActiveSchedule(event.dwScheduleID);
    if (!pGroup)
    {
        LogPrintf(0x2000001, "Schedule Manager : Do not Found ScheduleDefine ");
        return;
    }
    if (!pSchedule)
    {
        LogPrintf(0x2000001, "Schedule Manager : Do not Found Schedule - %d", event.dwScheduleID);
        return;
    }
    sScheduleInfo info = event.ToScheduleInfo();
    int nState = CShard_ScheduleExecutor::GetWorkingStateWhenServiceStart(pGroup->GetDefineName());
    if (nState < 0)
    {
        std::string strDefine = pGroup->GetDefineName();
        // BUG(SM-133): the string object itself is passed for %s.
        STRING_OBJECT_BYTES bytes;
        memcpy(&bytes, &strDefine, sizeof(bytes));
        LogPrintf(0x2000001, "Schedule Manager : WorkingStateWhenServiceStart is not exist - %s",
                  bytes);
    }
    if (nState == 0)
        g_ShardScheduleExecutor.Dispatch(info);
    std::string strDefine = pGroup->GetDefineName();
    LogPrintf(ShardScheduleLogFlags(), "Schedule Manager : Now working.. ! -  %s - %d",
              strDefine.c_str(), pSchedule->m_Data.dwScheduleID);
}

void CShard_ScheduleManager::OnScheduleEvent(sScheduleEvent& event)
{
    CScheduleGroup* pGroup = event.pGroup;
    if (!pGroup)
    {
        LogPrintf(0x2000001, "Schedule Manager : - Do not found ScheduleDefine !! ] - %d", 0);
        return;
    }
    sScheduleInfo info = event.ToScheduleInfo();
    bool bGoodsSell = pGroup->GetDefineName() == "SCHEDULE_DEF_SPECIAL_GOODS_SELL";
    if (bGoodsSell)
    {
        BroadcastSchedule(info);
        std::string strDefine = pGroup->GetDefineName();
        LogPrintf(ShardScheduleLogFlags(),
                  "Schedule Manager : Schedule processing (%d) ! - %s - %d",
                  event.bBegin, strDefine.c_str(), event.dwScheduleID);
    }
    else
    {
        g_ShardScheduleExecutor.Dispatch(info);
        std::string strDefine = pGroup->GetDefineName();
        LogPrintf(ShardScheduleLogFlags(),
                  "Schedule Manager : Schedule processing (%d) ! -  %s - %d",
                  event.bBegin, strDefine.c_str(), event.dwScheduleID);
    }
}

void CShard_ScheduleManager::DumpSchedules()
{
    LogPrintf(0x1000000, "-------------------------------");
    std::list<sScheduleEvent>& events = g_ShardScheduleManager.m_listEvent;
    for (std::list<sScheduleEvent>::iterator it = events.begin(); it != events.end(); ++it)
    {
        std::string strState = it->bBegin == 1 ? "START" : "END";
        CDateTime t(it->bBegin == 1 ? it->dwBegin : it->dwEnd);
        std::string strTime = t.Format();
        std::string strDefine = it->pGroup->GetDefineName();
        LogPrintf(0x1000000,
                  "Sschedule Define : %s, Schedule Idx : %d, Event Time : %s, Event State : %s ",
                  strDefine.c_str(), it->dwScheduleID, strTime.c_str(), strState.c_str());
    }
    LogPrintf(0x1000000, "-------------------------------");
}

BOOL CShard_ScheduleManager::IsServerBodyReady()
{
    SERVER_BODY* pBody = Shard_GetServerBodyOfMyself();
    if (!pBody)
    {
        LogPrintf(0x2000001, "Schedule Manager : g_pServerBodyOfMyself is NULL !!");
        return FALSE;
    }
    return pBody->nState >= 5;
}

// The query of LoadFromDB: rows come one at a time into a buffer the callback sets up.
static void PrepareScheduleQuery(CDBQuery& query, ServerFramework::CTrigger* pTrigger,
                                 void* pRow, int nRowSize)
{
    memset(&query, 0, sizeof(query));
    BS_ASSERT(g_pShardDB);
    query.m_pSession = g_pShardDB->GetSession();
    query.m_pTrigger = pTrigger;
    query.m_nSession = 6;
    query.m_dwParam = (DWORD)pRow;
    query.m_pUserData = (void*)(DWORD_PTR)nRowSize;
}

BOOL CShard_ScheduleManager::LoadFromDB()
{
    ServerFramework::CClassTrigger<CShard_ScheduleManager> defineTrigger(&g_ShardScheduleManager);
    defineTrigger.m_pfnTrigger2 = &CShard_ScheduleManager::OnScheduleDefineRow;
    SCHEDULE_DEFINE_DATA defineRow;
    memset(&defineRow, 0, sizeof(defineRow));
    CDBQuery query;
    PrepareScheduleQuery(query, &defineTrigger, &defineRow, sizeof(defineRow));
    if (query.ExecuteFormat("SELECT * FROM _RefScheduleDefine") < 0)
        return FALSE;

    ServerFramework::CClassTrigger<CShard_ScheduleManager> rowTrigger(&g_ShardScheduleManager);
    rowTrigger.m_pfnTrigger2 = &CShard_ScheduleManager::OnScheduleRow;
    SCHEDULE_DATA row;
    memset(&row, 0, sizeof(row));
    PrepareScheduleQuery(query, &rowTrigger, &row, sizeof(row));
    if (query.ExecuteFormat("SELECT * FROM _Schedule") < 0)
        return FALSE;
    return TRUE;
}

// Before a row: the buffer of the query.
static void ResetRowBuffer(CDBQuery* pQuery)
{
    int nSize = (int)(DWORD_PTR)pQuery->m_pUserData;
    pQuery->m_pBuffer = (BYTE*)pQuery->m_dwParam;
    pQuery->m_nRowSize = nSize;
    pQuery->m_nBufferSize = nSize;
    memset(pQuery->m_pBuffer, 0, nSize);
}

int CShard_ScheduleManager::OnScheduleDefineRow(DWORD dwQuery, DWORD /*dwUnused*/)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
        AddGroup((const SCHEDULE_DEFINE_DATA*)pQuery->m_pBuffer);
    else
        ResetRowBuffer(pQuery);
    return 1;
}

int CShard_ScheduleManager::OnScheduleRow(DWORD dwQuery, DWORD /*dwUnused*/)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (pQuery->m_bRowFetched)
    {
        SCHEDULE_DATA row;
        CopyScheduleRow((const SCHEDULE_DATA*)pQuery->m_pBuffer, &row);
        AddSchedule(&row);
        LogPrintf(0x1000000, "[ Schedule Data Inserting ... ] %d - %d", row.dwDefineID,
                  row.dwScheduleID);
    }
    else
    {
        ResetRowBuffer(pQuery);
    }
    return 1;
}

void CShard_ScheduleManager::BroadcastSchedule(const sScheduleInfo& info)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_SCHEDULE_NOTIFY);
    ShardSchedule_WriteHead(pMsg, info);
    BS_ASSERT(g_pMainProcess);
    CMainProcess::BroadcastToAllGameServers(pMsg);
}

// ---------------------------------------------------------------------------------------------
// CShard_ScheduleExecutor

// One prototype of RegisterDefaultJobs: a NULL job (failed allocation) fails the registration
// as well.
static void RegisterDefaultJob(NewSchedule::IScheduleJob* pJob, const char* pszClass)
{
    if (!g_ShardScheduleExecutor.RegisterJob(pJob))
        LogPrintf(0x2000001, "ScheduleExecutor : %s Default ScheduleJob Register Failed !!!",
                  pszClass);
}

BOOL CShard_ScheduleExecutor::RegisterDefaultJobs()
{
    RegisterDefaultJob(new CShardSJ_MentorHonor, "CShardSJ_MentorHonor");
    RegisterDefaultJob(new CShardSJ_SiegeRequestPeriod, "CShardSJ_SiegeRequestPeriod");
    RegisterDefaultJob(new CShardSJ_SiegePeriod, "CShardSJ_SiegePeriod");
    RegisterDefaultJob(new CShardSJ_SiegeFortressStatusUpdate,
                       "CShardSJ_SiegeFortressStatusUpdate");
    RegisterDefaultJob(new CShardSJ_SiegeTaxPeriod, "CShardSJ_SiegeTaxPeriod");
    RegisterDefaultJob(new CShardSJ_Roc, "CShardSJ_Roc");
    RegisterDefaultJob(new CShardSJ_ChinsTomb, "CShardSJ_ChinsTomb");
    RegisterDefaultJob(new CShardSJ_TempleOfSelkisAndNeithGate,
                       "CShardSJ_TempleOfSelkisAndNeithGate");
    RegisterDefaultJob(new CShardSJ_TempleOfAnubisAndIsisGate,
                       "CShardSJ_TempleOfAnubisAndIsisGate");
    RegisterDefaultJob(new CShardSJ_TempleOfHaroerisGate, "CShardSJ_TempleOfHaroerisGate");
    RegisterDefaultJob(new CShard_FlagWorldParticipation, "CShard_FlagWorldParticipation");
    RegisterDefaultJob(new CSahrd_FlagWorld, "CSahrd_FlagWorld");
    RegisterDefaultJob(new CShard_BattleArenaRandomParticipation,
                       "CShard_BattleArenaRandomParticipation");
    RegisterDefaultJob(new CShard_BattleArenaRandom, "CShard_BattleArenaRandom");
    RegisterDefaultJob(new CShard_BattleArenaPartyParticipation,
                       "CShard_BattleArenaPartyParticipation");
    RegisterDefaultJob(new CShard_BattleArenaParty, "CShard_BattleArenaParty");
    RegisterDefaultJob(new CShard_BattleArenaGuildParticipation,
                       "CShard_BattleArenaGuildParticipation");
    RegisterDefaultJob(new CShard_BattleArenaGuild, "CShard_BattleArenaGuild");
    RegisterDefaultJob(new CShard_BattleArenaJobParticipation,
                       "CShard_BattleArenaJobParticipation");
    RegisterDefaultJob(new CShard_BattleArenaJob, "CShard_BattleArenaJob");
    RegisterDefaultJob(new CShard_BattleArenaMatchPoint, "CShard_BattleArenaMatchPoint");
    RegisterDefaultJob(new CShard_BattleArenaMatchFlag, "CShard_BattleArenaMatchFlag");
    RegisterDefaultJob(new CShardSJ_EUBusinessEventUpdate, "CShardSJ_EUBusinessEventUpdate");
    RegisterDefaultJob(new CShardSJ_UniqueSpawn, "CShardSJ_UniqueSpawn");
    return TRUE;
}

int CShard_ScheduleExecutor::GetWorkingStateWhenServiceStart(std::string strDefine)
{
    NewSchedule::IScheduleJob* pJob = g_ShardScheduleExecutor.FindRegisteredJob(strDefine);
    if (!pJob)
    {
        LogPrintf(0x2000001,
                  "ScheduleExecutor : Don't Exist ScheduleJob - %s in "
                  "GetWorkingStateWhenServiceStart()",
                  strDefine.c_str());
        return -1;
    }
    return static_cast<IShard_ScheduleJob*>(pJob)->m_bWorkingStateWhenServiceStart;
}

void CShard_ScheduleExecutor::Dispatch(sScheduleInfo& info)
{
    switch (info.bBegin)
    {
    case 0:
        EndScheduleJob(info, 0, 0);
        break;
    case 1:
        BeginScheduleJob(info, 0, 0);
        break;
    default:
        BSAssertFail();
        break;
    }
}

BOOL CShard_ScheduleExecutor::OnScheduleMsg(CMsg* pMsg, DWORD /*dwParam*/)
{
    sScheduleInfo info;
    NewSchedule::ZeroScheduleStruct(info);
    if (pMsg)
    {
        *pMsg >> info.dwGroupID;
        *pMsg >> info.dwScheduleID;
        *pMsg >> info.dwBegin;
        *pMsg >> info.dwEnd;
        *pMsg >> info.bBegin;
        *pMsg >> info.strDefine;
    }
    switch (info.bBegin)
    {
    case 0:
        return EndScheduleJob(info, 0, 0);
    case 1:
        return BeginScheduleJob(info, 0, 0);
    case 2:
        return TickScheduleJob(info, 0, 0);
    default:
        BSAssertFail();
        return TRUE;
    }
}

BOOL CShard_ScheduleExecutor::BeginScheduleJob(sScheduleInfo& info, DWORD dw1, DWORD dw2)
{
    NewSchedule::IScheduleJob* pPrototype = FindRegisteredJob(info.strDefine);
    if (!pPrototype)
    {
        LogPrintf(0x2000001, "ScheduleExecutor : Don't Exist ScheduleJob - %s",
                  info.strDefine.c_str());
        return FALSE;
    }
    IShard_ScheduleJob* pJob = static_cast<IShard_ScheduleJob*>(pPrototype->CreateInstance());
    if (!pJob)
    {
        LogPrintf(0x2000001, "ScheduleExecutor : Instance Schedule Job Allocation Failed - %s",
                  info.strDefine.c_str());
        return FALSE;
    }
    pJob->SetScheduleInfo(info);
    pJob->OnBeginParam(dw1, dw2);
    if (AddJob(pJob, info) != 1)
        LogPrintf(0x2000001, "ScheduleExecutor : Instance Schedule Job Insert Failed - %s",
                  info.strDefine.c_str());
    return TRUE;
}

BOOL CShard_ScheduleExecutor::EndScheduleJob(sScheduleInfo& info, DWORD dw1, DWORD dw2)
{
    IShard_ScheduleJob* pJob = static_cast<IShard_ScheduleJob*>(FindJob(info.dwScheduleID));
    if (!pJob)
    {
        LogPrintf(0x2000001,
                  "ScheduleExecutor : Find Failed In Schedule Exe - End(STATE) %s(DEFINE) "
                  "%d(SCHIDX)",
                  info.strDefine.c_str(), info.dwScheduleID);
        return FALSE;
    }
    pJob->OnEndParam(dw1, dw2);
    EndJob(info.dwScheduleID, info);
    // BUG(SM-145): the running copy deletes its own m_pInstance, which is NULL (the prototype
    // holds the copy), so the copy is never freed.
    pJob->DeleteInstance();
    return TRUE;
}

BOOL CShard_ScheduleExecutor::TickScheduleJob(sScheduleInfo& info, DWORD dw1, DWORD dw2)
{
    IShard_ScheduleJob* pJob = static_cast<IShard_ScheduleJob*>(FindJob(info.dwScheduleID));
    if (!pJob)
    {
        // The end text in the tick as well.
        LogPrintf(0x2000001,
                  "ScheduleExecutor : Find Failed In Schedule Exe - End(STATE) %s(DEFINE) "
                  "%d(SCHIDX)",
                  info.strDefine.c_str(), info.dwScheduleID);
        return FALSE;
    }
    pJob->OnTickParam(dw1, dw2);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Rows and parameters

SCHEDULE_DATA* CopyScheduleRow(const SCHEDULE_DATA* pSrc, SCHEDULE_DATA* pDst)
{
    memset(pDst, 0, sizeof(*pDst));
    pDst->dwDefineID = pSrc->dwDefineID;
    pDst->dwScheduleID = pSrc->dwScheduleID;
    pDst->nType = pSrc->nType;
    pDst->nInterval = pSrc->nInterval;
    pDst->byDayOfWeek = pSrc->byDayOfWeek;
    pDst->byDay = pSrc->byDay;
    pDst->byWeek = pSrc->byWeek;
    pDst->byMonth = pSrc->byMonth;
    pDst->byHour = pSrc->byHour;
    pDst->byMinute = pSrc->byMinute;
    pDst->bySecond = pSrc->bySecond;
    pDst->nDuration = pSrc->nDuration;
    pDst->nRepeatInterval = pSrc->nRepeatInterval;
    pDst->nRepeatLength = pSrc->nRepeatLength;
    strcpy_s(pDst->szName, sizeof(pDst->szName), pSrc->szName);
    strcpy_s(pDst->szParam, sizeof(pDst->szParam), pSrc->szParam);
    pDst->stBegin.wYear = pSrc->stBegin.wYear;
    pDst->stBegin.wMonth = pSrc->stBegin.wMonth;
    pDst->stBegin.wDay = pSrc->stBegin.wDay;
    pDst->stBegin.wHour = pSrc->stBegin.wHour;
    pDst->stBegin.wMinute = pSrc->stBegin.wMinute;
    pDst->stBegin.wSecond = pSrc->stBegin.wSecond;
    pDst->stEnd.wYear = pSrc->stEnd.wYear;
    pDst->stEnd.wMonth = pSrc->stEnd.wMonth;
    pDst->stEnd.wDay = pSrc->stEnd.wDay;
    pDst->stEnd.wHour = pSrc->stEnd.wHour;
    pDst->stEnd.wMinute = pSrc->stEnd.wMinute;
    pDst->stEnd.wSecond = pSrc->stEnd.wSecond;
    return pDst;
}

BOOL ScheduleParam_SplitBySlash(const std::string& strParam, std::vector<std::string>* pTokens)
{
    String_Split(strParam, "/", pTokens);
    return TRUE;
}

BOOL ScheduleParamParser_Parse(const std::string& strParam, ScheduleParamList* pParams)
{
    std::vector<std::string> tokens;
    ScheduleParam_SplitBySlash(strParam, &tokens);
    std::pair<std::string, std::string> param;
    for (int i = 0; i < (int)tokens.size(); ++i)
    {
        std::vector<std::string> parts;
        String_Split(tokens.at(i), "=", &parts);
        if (parts.size() < 2)
        {
            LogPrintf(0x2000001, "ScheduleParmParser : Param rule is failed !! - %s",
                      strParam.c_str());
            return FALSE;
        }
        param.first = parts.at(0);
        param.second = parts.at(1);
        pParams->push_back(param);
    }
    return TRUE;
}
