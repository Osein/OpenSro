#include "ShardManager/ShardScheduleJob.h"

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/ShardScheduleExtern.h"
#include "ShardManager/ShardWorldExtern.h"
#include "ShardManager/SiegeFortress.h"
#include "ShardManager/TrainingCamp.h"

// ---------------------------------------------------------------------------------------------
// IShard_ScheduleJob

void IShard_ScheduleJob::DeleteInstance()
{
    NewSchedule::IScheduleJob* pInstance = m_pInstance;
    if (pInstance)
    {
        delete pInstance;
        m_pInstance = NULL;
    }
}

void IShard_ScheduleJob::BeginNotify(const NewSchedule::sScheduleInfo* pInfo)
{
    if (!pInfo)
        pInfo = &m_Info;
    m_pNotifyMsg = g_pNetEngine->AllocMsg(FALSE);
    m_pNotifyMsg->SetOpcode(MSG_SHARD_SCHEDULE_NOTIFY);
    ShardSchedule_WriteHead(m_pNotifyMsg, *pInfo);
}

void IShard_ScheduleJob::BroadcastNotify()
{
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToAllGameServers(m_pNotifyMsg);
}

// The code each begin and end handler repeats inline.
void IShard_ScheduleJob::LogSchedule(const char* pszText)
{
    if (pszText)
        LogPrintf(ShardScheduleLogFlags(), pszText);
    LogScheduleRow();
}

void IShard_ScheduleJob::LogSchedule(const char* pszFormat, int nArg)
{
    LogPrintf(ShardScheduleLogFlags(), pszFormat, nArg);
    LogScheduleRow();
}

void IShard_ScheduleJob::LogScheduleRow()
{
    const char* pszState = "Start";
    if (m_Info.bBegin != 1)
        pszState = "End";
    int nServerID = g_pwShardServerID ? *g_pwShardServerID : -1;
    Schedule_InsertLogAsync(std::string("SR_ShardManager"), nServerID, m_Info.strDefine,
                            m_Info.dwScheduleID, std::string(pszState));
}

static void WriteLayerID(CMsg* pMsg, const GameWorldLayer* pLayer)
{
    DWORD dwLayerID = pLayer->m_wLayerID;
    pMsg->WriteBytes(&dwLayerID, 2);
}

// The parameters of the scheduled instance layers: a layer that closes after dwDuration and
// stays dwEmptyRemainTime while empty.
static void SetScheduledLayerParam(sGameWorldCreateParam& param, DWORD dwDuration,
                                   DWORD dwEmptyRemainTime)
{
    param.m_dw0 = 0;
    param.m_b4 = 7;
    param.m_dw8 = 0;
    param.m_b12 = 1;
    param.m_b13 = 0;
    param.m_dwDuration = dwDuration;
    param.m_dwEmptyRemainTime = dwEmptyRemainTime;
    param.m_b24 = 0;
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_ChinsTomb

BOOL CShardSJ_ChinsTomb::OnBegin()                      // 0x65d770
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers())
        return FALSE;
    OpenGameWorlds();
    if (!m_pFloor5 || !m_pFloor6)
        return FALSE;

    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pFloor5);
    WriteLayerID(m_pNotifyMsg, m_pFloor6);
    BroadcastNotify();
    LogSchedule("Scheduleing : Chin's tomb instance is now opened");
    return TRUE;
}

void CShardSJ_ChinsTomb::OnEnd(CDateTime& /*now*/)     // 0x65d900
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers() || !m_pFloor5 || !m_pFloor6)
        return;

    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pFloor5);
    WriteLayerID(m_pNotifyMsg, m_pFloor6);
    BroadcastNotify();
    // ShardSJ_ChinsTomb_ResetState (0x65dda0)
    m_pFloor5 = NULL;
    m_pFloor6 = NULL;
    LogSchedule("Scheduleing : Chin's tomb instance is now closed");
}

void CShardSJ_ChinsTomb::OpenGameWorlds()
{
    RefGameWorld* pRef5 =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_QINSHI_FLOOR_5"));
    RefGameWorld* pRef6 =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_QINSHI_FLOOR_6"));
    if (!pRef5 || !pRef6)
    {
        LogPrintf(0x2000000,
                  "Chin's tomb data is not exists!! check the gust and service for the data");
        BSAssertFail();
        return;
    }

    sGameWorldCreateParam param5;
    WORD wWorldID5 = (WORD)pRef5->ID;
    BYTE byResult = 1;
    SetScheduledLayerParam(param5, 18000000, 21600000);
    CGameWorld* pWorld5 = CGameWorldMgr::GetInstance()->FindWorld(wWorldID5);
    if (!pWorld5)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pFloor5 = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID5, &param5);
    if (!m_pFloor5)
    {
        LogPrintf(0x2000000, "Open Chin's tomb GameWorld (Floor 5) Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened5 = (WORD)pWorld5->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened5, m_pFloor5);

    sGameWorldCreateParam param6;
    WORD wWorldID6 = (WORD)pRef6->ID;
    byResult = 1;
    SetScheduledLayerParam(param6, 18000000, 21600000);
    CGameWorld* pWorld6 = CGameWorldMgr::GetInstance()->FindWorld(wWorldID6);
    if (!pWorld6)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pFloor6 = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID6, &param6);
    if (!m_pFloor6)
    {
        LogPrintf(0x2000000, "Open Chin's tomb GameWorld (Floor 6) Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened6 = (WORD)pWorld6->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened6, m_pFloor6);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_Roc

BOOL CShardSJ_Roc::OnBegin()                            // 0x65e290
{
    if (g_ShardScheduleExecutor.FindJobByName(m_Info.strDefine))
        return FALSE;
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers())
        return FALSE;
    OpenGameWorld();
    if (!m_pLayer)
        return FALSE;

    m_Info.bBegin = 1;
    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pLayer);
    BroadcastNotify();
    LogSchedule("Scheduleing : Roc instance is now opened");
    return TRUE;
}

void CShardSJ_Roc::OnEnd(CDateTime& /*now*/)           // 0x65e400
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers() || !m_pLayer)
        return;

    m_Info.bBegin = 0;
    BeginNotify(NULL);
    WriteLayerID(m_pNotifyMsg, m_pLayer);
    BroadcastNotify();
    LogSchedule("Scheduleing : Roc instance is now closed");
}

void CShardSJ_Roc::OpenGameWorld()
{
    RefGameWorld* pRef =
        CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string("INS_ROC_TOP"));
    if (!pRef)
    {
        LogPrintf(0x2000000, "RocLoad data is not exists!! check the gust and service for the data");
        BSAssertFail();
        return;
    }

    sGameWorldCreateParam param;
    WORD wWorldID = (WORD)pRef->ID;
    BYTE byResult = 1;
    SetScheduledLayerParam(param, (DWORD)pRef->WorldDurationTime,
                           (DWORD)pRef->WorldEmptyRemainTime);
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, "Failed to find GameWorld!!");
        return;
    }
    m_pLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!m_pLayer)
    {
        LogPrintf(0x2000000, "Open Roc GameWorld Failed!! error[%d]", byResult);
        return;
    }
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, m_pLayer);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_EUBusinessEventUpdate, CShardSJ_UniqueSpawn, CShardSJ_MentorHonor

BOOL CShardSJ_EUBusinessEventUpdate::OnBegin()          // 0x65ddb0
{
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : CHANGE_EXP_RATIO_EVENT_START");
    return TRUE;
}

void CShardSJ_EUBusinessEventUpdate::OnEnd(CDateTime& /*now*/)     // 0x65def0
{
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : CHANGE_EXP_RATIO_EVENT_STOP");
}

BOOL CShardSJ_UniqueSpawn::OnBegin()                    // 0x65e030
{
    if (!g_pMainProcess)
        BSAssertFail();
    UniqueSpawn_BroadcastL3Uniques_34BB(0);
    LogSchedule("Scheduleing : Unique spawn has been started");
    return TRUE;
}

void CShardSJ_UniqueSpawn::OnEnd(CDateTime& /*now*/)   // 0x65e180
{
    LogSchedule(NULL);
}

BOOL CShardSJ_MentorHonor::OnBegin()                    // 0x65e720
{
    // The training camp manager (dword_8A8930) is asserted, then not used.
    if (!g_pTrainingCampManager)
        BSAssertFail();
    TrainingCamp_DBUpdateHonorRank();
    LogSchedule("Scheduleing : Mentor Honor Record Update is Start & End ..... !");
    return TRUE;
}

void CShardSJ_MentorHonor::OnEnd(CDateTime& /*now*/)   // 0x65e870
{
    LogSchedule(NULL);
}

// ---------------------------------------------------------------------------------------------
// Constructors: define and service-start flag

SHARD_SCHEDULE_JOB_CTOR(CShardSJ_MentorHonor, "SCHEDULE_DEF_MENTOR_HONOR_UPDATE",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_SiegeRequestPeriod, "SCHEDULE_DEF_SIEGE_REQUEST_PERIOD",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_SiegePeriod, "SCHEDULE_DEF_SIEGE_PERIOD",
                        m_bWorkingStateWhenServiceStart, 1)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_SiegeFortressStatusUpdate,
                        "SCHEDULE_DEF_SIEGE_FORTRESS_DB_UPDATE", m_bWorkingStateWhenServiceStart, 1)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_SiegeTaxPeriod, "SCHEDULE_DEF_SIEGE_TAX_PERIOD",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_Roc, "SCHEDULE_DEF_MEET_ROC", m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_ChinsTomb, "SCHEDULE_DEF_CHINS_TOMB",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShard_FlagWorldParticipation, "SCHEDULE_DEF_FLAG_WORLD_PARTICIPATION",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CSahrd_FlagWorld, "SCHEDULE_DEF_FLAG_WORLD",
                        m_bWorkingStateWhenServiceStart, 1)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_EUBusinessEventUpdate, "SCHEDULE_DEF_EVENT_EUBUSINESS",
                        m_bWorkingStateWhenServiceStart, 0)
SHARD_SCHEDULE_JOB_CTOR(CShardSJ_UniqueSpawn, "SCHEDULE_DEF_EVENT_UNIQUE_MONSTER_SPAWN",
                        m_bWorkingStateWhenServiceStart, 0)

// The battle arena bases: the counters and the flag; the kind (+88) is set by the leaves.
CShard_BattleArenaScheduleParticipation::CShard_BattleArenaScheduleParticipation()   // 0x677de0
{
    m_dwRunning = 0;
    m_dw7C = 0;
    m_dwNoticeTick = 0;
    m_dw8C = 0;
    m_bWorkingStateWhenServiceStart = 0;
}

CShard_BattleArenaSchedule::CShard_BattleArenaSchedule()                             // 0x678250
{
    m_dwRunning = 0;
    m_bEndPlaySent = 0;
    m_dwCheckTick = 0;
    m_bStopResurrectSent = 0;
    m_bWorkingStateWhenServiceStart = 1;
}

SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaRandomParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_RANDOM_PARTICIPATION", m_byKind, 0)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaRandom, "SCHEDULE_DEF_BATTLE_ARENA_RANDOM",
                        m_byKind, 0)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaPartyParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_PARTY_PARTICIPATION", m_byKind, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaParty, "SCHEDULE_DEF_BATTLE_ARENA_PARTY",
                        m_byKind, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaGuildParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_GUILD_PARTICIPATION", m_byKind, 2)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaGuild, "SCHEDULE_DEF_BATTLE_ARENA_GUILD",
                        m_byKind, 2)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaJobParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_JOB_PARTICIPATION", m_byKind, 3)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaJob, "SCHEDULE_DEF_BATTLE_ARENA_JOB", m_byKind, 3)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaMatchPoint, "SCHEDULE_DEF_BATTLE_ARENA_MATCH_POINT",
                        m_byMatchType, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaMatchFlag, "SCHEDULE_DEF_BATTLE_ARENA_MATCH_FLAG",
                        m_byMatchType, 2)

// ---------------------------------------------------------------------------------------------
// Siege jobs

// The siege state of the main process (asserts the main process).
static SHARD_SIEGE_SCHEDULE* GetSiegeSchedule()
{
    if (!g_pMainProcess)
        BSAssertFail();
    return MainProcess_GetSiegeSchedule();
}

// The first period of the define after now; FALSE without one.
static BOOL GetNextPeriod(const char* pszDefine, CDateTime& now,
                          NewSchedule::sSchedulePeriod* pPeriod)
{
    std::list<NewSchedule::sSchedulePeriod> periods;
    g_ShardScheduleManager.GetNextPeriods(periods, pszDefine, 1, now);
    if (periods.empty())
        return FALSE;
    *pPeriod = periods.front();
    return TRUE;
}

BOOL CShardSJ_SiegeRequestPeriod::OnBegin()
{
    SHARD_SIEGE_SCHEDULE* pSiege = GetSiegeSchedule();
    pSiege->bRequestPeriod = 1;
    CDateTime begin(m_Info.dwBegin);
    begin.GetAsSystemTime(pSiege->stPeriodBegin);
    CDateTime end(m_Info.dwEnd);
    end.GetAsSystemTime(pSiege->stPeriodEnd);
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress Request is Start.. !");
    return TRUE;
}

void CShardSJ_SiegeRequestPeriod::OnEnd(CDateTime& now)
{
    GetSiegeSchedule()->bRequestPeriod = 0;
    NewSchedule::sSchedulePeriod next;
    if (!GetNextPeriod("SCHEDULE_DEF_SIEGE_REQUEST_PERIOD", now, &next))
        return;
    CDateTime begin(next.tBegin);
    CDateTime end(next.tEnd);
    SYSTEMTIME stBegin;
    SYSTEMTIME stEnd;
    begin.GetAsSystemTime(stBegin);
    end.GetAsSystemTime(stEnd);
    BeginNotify(NULL);
    m_pNotifyMsg->WriteBytes(&stBegin, sizeof(stBegin));
    m_pNotifyMsg->WriteBytes(&stEnd, sizeof(stEnd));
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress Request is End.. !");
}

BOOL CShardSJ_SiegePeriod::OnBegin()
{
    GetSiegeSchedule()->bSiegeRunning = 1;
    DWORD dwBegin = m_Info.dwBegin;
    DWORD dwEnd = m_Info.dwEnd;
    m_dwBegin = dwBegin;
    m_dwStart = dwBegin + 1800;
    m_dwEnd30Min = dwEnd - 1800;
    m_dwEnd10Min = dwEnd - 600;
    m_dwEnd1Min = dwEnd - 60;
    m_byStage = 0;
    m_dwEnd20Min = dwEnd - 1200;
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress is Start Before 30 Min..... !");
    return TRUE;
}

namespace
{

// The stages of a running siege: the stage time member, the notice byte and the log line.
struct SIEGE_STAGE
{
    DWORD CShardSJ_SiegePeriod::*   pTime;
    BYTE                            byNotice;
    const char*                     pszLog;
};

const SIEGE_STAGE s_aSiegeStage[] =
{
    { &CShardSJ_SiegePeriod::m_dwStart,    2, "Scheduleing : SiegeFortress is Start..... !" },
    { &CShardSJ_SiegePeriod::m_dwEnd30Min, 3,
      "Scheduleing : SiegeFortress is End Before 30 Min..... !" },
    { &CShardSJ_SiegePeriod::m_dwEnd20Min, 4,
      "Scheduleing : SiegeFortress is End Before 20 Min..... !" },
    { &CShardSJ_SiegePeriod::m_dwEnd10Min, 5,
      "Scheduleing : SiegeFortress is End Before 10 Min..... !" },
    { &CShardSJ_SiegePeriod::m_dwEnd1Min,  9,
      "Scheduleing : SiegeFortress is End Before 1 Min..... !" },
};

} // namespace

BOOL CShardSJ_SiegePeriod::OnTick(CDateTime& now)
{
    NewSchedule::sScheduleInfo info = m_Info;
    info.bBegin = 2;
    // Stage n fires once its time has passed and stage n - 1 has fired; several in one tick.
    for (int i = 0; i < _countof(s_aSiegeStage); ++i)
    {
        const SIEGE_STAGE& stage = s_aSiegeStage[i];
        if ((DWORD)now.GetTime() > this->*stage.pTime && m_byStage == i)
        {
            BeginNotify(&info);
            BYTE byNotice = stage.byNotice;
            m_pNotifyMsg->WriteBytes(&byNotice, 1);
            BroadcastNotify();
            m_byStage = (BYTE)(i + 1);
            LogPrintf(ShardScheduleLogFlags(), stage.pszLog);
        }
    }
    return TRUE;
}

void CShardSJ_SiegePeriod::OnEnd(CDateTime& now)
{
    SHARD_SIEGE_SCHEDULE* pSiege = GetSiegeSchedule();
    pSiege->bSiegeRunning = 0;
    NewSchedule::sSchedulePeriod next;
    if (!GetNextPeriod("SCHEDULE_DEF_SIEGE_PERIOD", now, &next))
        return;
    CDateTime begin(next.tBegin + 1800);
    CDateTime end(next.tEnd);
    SYSTEMTIME stBegin;
    SYSTEMTIME stEnd;
    begin.GetAsSystemTime(stBegin);
    end.GetAsSystemTime(stEnd);
    pSiege->stPrevSiegeBegin = pSiege->stSiegeBegin;
    pSiege->stSiegeBegin = stBegin;
    pSiege->stSiegeEnd = stEnd;
    BeginNotify(NULL);
    m_pNotifyMsg->WriteBytes(&stBegin, sizeof(stBegin));
    m_pNotifyMsg->WriteBytes(&stEnd, sizeof(stEnd));
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress is End..... !");
}

BOOL CShardSJ_SiegeFortressStatusUpdate::OnBegin()
{
    if (!g_pSiegeFortressMgr)
        BSAssertFail();
    SiegeFortressMgr_StatusUpdateForWeb(g_pSiegeFortressMgr);
    LogSchedule("Scheduleing : SiegeFortress State Update is Start & End ..... !");
    return TRUE;
}

BOOL CShardSJ_SiegeTaxPeriod::OnBegin()
{
    SHARD_SIEGE_SCHEDULE* pSiege = GetSiegeSchedule();
    pSiege->bTaxPeriod = 1;
    CDateTime begin(m_Info.dwBegin);
    CDateTime end(m_Info.dwEnd);
    begin.GetAsSystemTime(pSiege->stPeriodBegin);
    end.GetAsSystemTime(pSiege->stPeriodEnd);
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress Tax Period is Start..... !");
    return TRUE;
}

void CShardSJ_SiegeTaxPeriod::OnEnd(CDateTime& /*now*/)
{
    GetSiegeSchedule()->bTaxPeriod = 0;
    BeginNotify(NULL);
    BroadcastNotify();
    LogSchedule("Scheduleing : SiegeFortress Tax Period is End..... !");
}
