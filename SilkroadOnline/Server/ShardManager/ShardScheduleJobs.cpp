#include "ShardManager/ShardScheduleJobs.h"

#include <list>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardScheduleExtern.h"

using NewSchedule::sScheduleInfo;
using NewSchedule::sSchedulePeriod;

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

SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaRandomParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_RANDOM_PARTICIPATION", m_byArenaType, 0)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaRandom, "SCHEDULE_DEF_BATTLE_ARENA_RANDOM",
                        m_byArenaType, 0)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaPartyParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_PARTY_PARTICIPATION", m_byArenaType, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaParty, "SCHEDULE_DEF_BATTLE_ARENA_PARTY",
                        m_byArenaType, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaGuildParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_GUILD_PARTICIPATION", m_byArenaType, 2)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaGuild, "SCHEDULE_DEF_BATTLE_ARENA_GUILD",
                        m_byArenaType, 2)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaJobParticipation,
                        "SCHEDULE_DEF_BATTLE_ARENA_JOB_PARTICIPATION", m_byArenaType, 3)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaJob, "SCHEDULE_DEF_BATTLE_ARENA_JOB", m_byArenaType, 3)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaMatchPoint, "SCHEDULE_DEF_BATTLE_ARENA_MATCH_POINT",
                        m_byMatchType, 1)
SHARD_SCHEDULE_JOB_CTOR(CShard_BattleArenaMatchFlag, "SCHEDULE_DEF_BATTLE_ARENA_MATCH_FLAG",
                        m_byMatchType, 2)

// ---------------------------------------------------------------------------------------------
// CShardSJ_SiegePeriod

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
    sScheduleInfo info = m_Info;
    info.bBegin = 2;
    // Stage n fires once its time has passed and stage n - 1 has fired; several in one tick.
    for (int i = 0; i < _countof(s_aSiegeStage); ++i)
    {
        const SIEGE_STAGE& stage = s_aSiegeStage[i];
        if ((DWORD)now.GetTime() > this->*stage.pTime && m_byStage == i)
        {
            BeginNotify(&info);
            BYTE byNotice = stage.byNotice;
            m_pMsg->WriteBytes(&byNotice, 1);
            BroadcastToGameServers();
            m_byStage = (BYTE)(i + 1);
            LogPrintf(ShardScheduleLogFlags(), stage.pszLog);
        }
    }
    return TRUE;
}

BOOL CShardSJ_SiegePeriod::OnEnd(CDateTime& now)
{
    BS_ASSERT(g_pShardMainProcess);
    SHARD_SIEGE_SCHEDULE* pSiege = MainProcess_GetSiegeSchedule();
    pSiege->bSiegeRunning = 0;
    std::list<sSchedulePeriod> periods;
    g_ShardScheduleManager.GetNextPeriods(periods, "SCHEDULE_DEF_SIEGE_PERIOD", 1, now);
    if (periods.empty())
        return FALSE;
    const sSchedulePeriod& next = periods.front();
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
    m_pMsg->WriteBytes(&stBegin, sizeof(stBegin));
    m_pMsg->WriteBytes(&stEnd, sizeof(stEnd));
    BroadcastToGameServers();
    LogPrintf(ShardScheduleLogFlags(), "Scheduleing : SiegeFortress is End..... !");
    InsertScheduleLog();
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_SiegeFortressStatusUpdate, CShardSJ_SiegeTaxPeriod

BOOL CShardSJ_SiegeFortressStatusUpdate::OnBegin()
{
    BS_ASSERT(g_pSiegeFortressMgr);
    SiegeFortressMgr_StatusUpdateForWeb(g_pSiegeFortressMgr);
    LogPrintf(ShardScheduleLogFlags(),
              "Scheduleing : SiegeFortress State Update is Start & End ..... !");
    InsertScheduleLog();
    return TRUE;
}

BOOL CShardSJ_SiegeTaxPeriod::OnBegin()
{
    BS_ASSERT(g_pShardMainProcess);
    SHARD_SIEGE_SCHEDULE* pSiege = MainProcess_GetSiegeSchedule();
    pSiege->bTaxPeriod = 1;
    CDateTime begin(m_Info.dwBegin);
    CDateTime end(m_Info.dwEnd);
    begin.GetAsSystemTime(pSiege->stTaxBegin);
    end.GetAsSystemTime(pSiege->stTaxEnd);
    BeginNotify(NULL);
    BroadcastToGameServers();
    LogPrintf(ShardScheduleLogFlags(), "Scheduleing : SiegeFortress Tax Period is Start..... !");
    InsertScheduleLog();
    return TRUE;
}

BOOL CShardSJ_SiegeTaxPeriod::OnEnd(CDateTime& /*now*/)
{
    BS_ASSERT(g_pShardMainProcess);
    MainProcess_GetSiegeSchedule()->bTaxPeriod = 0;
    BeginNotify(NULL);
    BroadcastToGameServers();
    LogPrintf(ShardScheduleLogFlags(), "Scheduleing : SiegeFortress Tax Period is End..... !");
    InsertScheduleLog();
    return TRUE;
}
