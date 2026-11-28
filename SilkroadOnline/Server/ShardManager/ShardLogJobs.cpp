#include "ShardManager/ShardLogJobs.h"

#include <strsafe.h>
#include <time.h>

#include "ShardManager/AQScheduleJobs.h"
#include "ShardManager/AQServerEvent.h"
#include "ShardManager/ServerEvent.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

// The texts of _LogServerEvent are built in 0xC00 bytes.
enum
{
    SHARD_LOG_TEXT_SIZE = 0xC00,
};

// The end of every log function: the row goes into the job, the job to the DB task. FALSE (the
// job back to its pool) when the row cannot be built.
static BOOL ServerEventLog_Post(AQ_ServerEventLog* pJob, const char* pszDesc, BYTE byType,
                                CServerEvent* pEvent)
{
    std::string strDesc(pszDesc);
    if (!DB_Insert_LogServerEvent(strDesc, byType, pJob, pEvent->GetID()))
    {
        pJob->ReturnToPool();
        return FALSE;
    }
    MainProcess_PostDBJob(pJob);
    return TRUE;
}

// BUG(SM-113): the log functions keep the job they took from the pool when they fail before the
// row is built (missing reference or user, text too long).

BOOL ServerEventLog_LogDetecting(CServerEvent* pEvent)
{
    AQ_ServerEventLog* pJob = AQ_ServerEventLog::Alloc();
    RefServerEvent* pRef;
    if (!pJob || (pRef = pEvent->m_pRef) == NULL)
    {
        BSAssertFail();
        return FALSE;
    }

    std::string strKind;
    if (pRef->b4C == 1)
        strKind.assign("KillMonster", 11);
    else if (pRef->b4C == 2)
        strKind.assign("EventCompletion", 15);

    int nProgressCount = pEvent->GetProgressCount();
    int nAchievementCondition = pEvent->GetAchievementCondition();
    int nCompletionValue = pEvent->GetCompletionValue();
    char szDesc[SHARD_LOG_TEXT_SIZE];
    if (StringCchPrintfA(szDesc, SHARD_LOG_TEXT_SIZE,
                         "Detecting[%d]_%s CompletionValue[%d] AchievementCondition[%d] "
                         "ProgressCount[%d]",
                         pRef->dw0C, strKind.c_str(), nCompletionValue, nAchievementCondition,
                         nProgressCount) != S_OK)
    {
        BSAssertFail();
        return FALSE;
    }
    return ServerEventLog_Post(pJob, szDesc, SERVER_EVENT_LOG_DETECTING, pEvent);
}

BOOL ServerEventLog_LogCompletion(CServerEvent* pEvent)
{
    AQ_ServerEventLog* pJob = AQ_ServerEventLog::Alloc();
    if (!pJob)
    {
        BSAssertFail();
        return FALSE;
    }

    int nProgressCount = pEvent->GetProgressCount();
    int nAchievementCondition = pEvent->GetAchievementCondition();
    int nCompletionValue = pEvent->GetCompletionValue();
    char szDesc[SHARD_LOG_TEXT_SIZE];
    if (StringCchPrintfA(szDesc, SHARD_LOG_TEXT_SIZE,
                         "CompletionValue[%d] AchievementCondition[%d] ProgressCount[%d]",
                         nCompletionValue, nAchievementCondition, nProgressCount) != S_OK)
    {
        BSAssertFail();
        return FALSE;
    }
    return ServerEventLog_Post(pJob, szDesc, SERVER_EVENT_LOG_COMPLETION, pEvent);
}

BOOL ServerEventLog_LogReward(DWORD dwJID, CServerEvent* pEvent)
{
    AQ_ServerEventLog* pJob = AQ_ServerEventLog::Alloc();
    if (!pJob)
    {
        BSAssertFail();
        return FALSE;
    }

    char szDesc[SHARD_LOG_TEXT_SIZE];
    HRESULT hr;
    if (dwJID == 0)
    {
        RefServerEvent* pRef = pEvent->m_pRef;
        if (!pRef)
        {
            BSAssertFail();
            return FALSE;
        }
        hr = StringCchPrintfA(szDesc, SHARD_LOG_TEXT_SIZE, "By ServerEvent[%d]", pRef->dw0C);
    }
    else
    {
        CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
        if (!pUser)
        {
            BSAssertFail();
            return FALSE;
        }
        // The game world context of the user (CUser + 0x1F0): region X / Z bytes, position.
        const GameWorldContext* pContext = User_GetGameWorldPos(pUser);
        hr = StringCchPrintfA(szDesc, SHARD_LOG_TEXT_SIZE,
                              "CharName[%s] RegionXZ[%d,%d] Position[%0.2f, %0.2f, %0.2f]",
                              pUser->GetCharName(), LOBYTE(pContext->wRegionID),
                              HIBYTE(pContext->wRegionID), (double)pContext->fX,
                              (double)pContext->fY, (double)pContext->fZ);
    }
    if (hr != S_OK)
    {
        BSAssertFail();
        return FALSE;
    }
    return ServerEventLog_Post(pJob, szDesc, SERVER_EVENT_LOG_REWARD, pEvent);
}

void Schedule_InsertLogAsync(std::string strModule, int nServerID, std::string strDefine,
                             DWORD dwScheduleID, std::string strState)
{
    AQ_ScheduleInsertLog* pJob = AQ_ScheduleInsertLog::Alloc();
    if (!pJob)
    {
        BSAssertFail();
        return;
    }
    CDateTime dtNow(_time32(NULL));
    if (!DB_Insert_LogSchedule(pJob, strModule, nServerID, strDefine, dwScheduleID, strState,
                               dtNow))
    {
        pJob->ReturnToPool();
        return;
    }
    MainProcess_PostDBJob(pJob);
}
