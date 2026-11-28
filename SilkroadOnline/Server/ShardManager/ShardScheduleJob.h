#pragma once

#include <windows.h>

#include <list>
#include <string>
#include <utility>
#include <vector>

#include "BSLib/NewSchedule/ScheduleExecutor.h"
#include "ShardManager/ShardSchedule.h"

class CMsg;
class GameWorldLayer;

// The schedule jobs of SR_ShardManager: IShard_ScheduleJob and the job classes. Each class
// registers one prototype for its _RefScheduleDefine code name
// (CShard_ScheduleExecutor::RegisterDefaultJobs); the constructors (0x664620..0x666030) set the
// define and the service-start flag, slot 2 (CreateInstance) makes the running copy that the
// executor runs. A job tells the game servers about its schedule with 0x34FE (see
// ShardSchedule.h) and writes a _LogSchedule row at begin and end. The temple gate jobs are in
// ShardTempleGate.h. Slot 7 (OnEnd) returns a BOOL in the original that nobody reads.
//
// The flag world jobs run in FlagWorld.cpp, the battle arena jobs in BattleArena.cpp.

// An entry of the open list (8 bytes): a notice (bOpen 0) or the opening (1) at dwTime.
struct sTempleOpenTime
{
    DWORD   dwTime;
    BYTE    bOpen;
};

// The list sort (0x666bc0): by time.
inline bool operator<(const sTempleOpenTime& left, const sTempleOpenTime& right)
{
    return left.dwTime < right.dwTime;
}

// The open list and the opening time (job +78 / +84; filled by ScheduleParam_BuildOpenTimeList).
struct sTempleOpenSchedule
{
    std::list<sTempleOpenTime>  m_list;         // +00
    DWORD                       m_dwOpenTime;   // +0C
};

C_ASSERT(sizeof(sTempleOpenSchedule) == 0x10);

// ---------------------------------------------------------------------------------------------
// The schedule parameter helpers of the temple gate jobs (ShardScheduleParam.cpp)

// ScheduleParam_BuildOpenTimeList (0x666680): dwBegin + WORLD_OPEN_TIME minutes for each
// WORLD_OPEN_TIME entry (the last one is the open time), then the open time - NOTIFY_BEFORE_OPEN
// minutes for each NOTIFY_BEFORE_OPEN entry, sorted. FALSE without a WORLD_OPEN_TIME entry.
BOOL ScheduleParam_BuildOpenTimeList(const std::string& strParam, sTempleOpenSchedule* pOpen,
                                     DWORD dwBegin);
// ScheduleParam_GetInt (0x6669c0): atol of the first value of strKey.
BOOL ScheduleParam_GetInt(const std::string& strParam, std::string strKey, int* pnValue);

// ---------------------------------------------------------------------------------------------
// IShard_ScheduleJob (vftable 0x795fac)

class IShard_ScheduleJob : public NewSchedule::IScheduleJob
{
public:
    // 0x6644d0. m_pNotifyMsg is not set.
    IShard_ScheduleJob() { m_bWorkingStateWhenServiceStart = 1; }
    // ~IShard_ScheduleJob 0x664730: the base members only.

    // Slots 3, 4, 6 and 7 of every job class share one function each (0x664980, 0x6649b0 and
    // 0x664720 for both slot 6 and 7); IShard_ScheduleJob's own entries are pure.
    // BUG(SM-132): deletes the copy through IScheduleJob, whose destructor is not virtual.
    virtual void DeleteInstance();                                              // 3  0x664980
    virtual BOOL CanBegin() { return TRUE; }                                    // 4  0x6649b0
    virtual BOOL OnTick(CDateTime& /*now*/) { return TRUE; }                    // 6  0x664720
    virtual void OnEnd(CDateTime& /*now*/) {}                                   // 7  0x664720
    // Called by the executor after SetScheduleInfo, before OnTick and before OnEnd with the
    // two extra arguments of the event; empty in every job (0x664530).
    virtual void OnBeginParam(DWORD /*dw1*/, DWORD /*dw2*/) {}                  // 8
    virtual void OnTickParam(DWORD /*dw1*/, DWORD /*dw2*/) {}                   // 9
    virtual void OnEndParam(DWORD /*dw1*/, DWORD /*dw2*/) {}                    // 10

    // IShard_ScheduleJob::BeginNotify_34FE (0x666130): a new 0x34FE message in m_pNotifyMsg with
    // the schedule info of pInfo (this job's own when NULL).
    void BeginNotify(const NewSchedule::sScheduleInfo* pInfo);
    // ShardSJ_BroadcastToGameServers (0x6661c0): m_pNotifyMsg to every game server.
    void BroadcastNotify();

    // The two log lines of every begin and end (inlined): LogPrintf of pszText (NULL: none),
    // then the _LogSchedule row "Start" / "End".
    void LogSchedule(const char* pszText);
    // The same with one %d argument in the text.
    void LogSchedule(const char* pszFormat, int nArg);
    // The _LogSchedule row of LogSchedule alone.
    void LogScheduleRow();

public:
    CMsg*   m_pNotifyMsg;                       // +70
    BYTE    m_bWorkingStateWhenServiceStart;    // +74 read by GetWorkingStateWhenServiceStart
};

C_ASSERT(FIELD_OFFSET(IShard_ScheduleJob, m_pNotifyMsg) == 0x70);
C_ASSERT(sizeof(IShard_ScheduleJob) == 0x78);

// The job classes: slot 2 makes the running copy (operator new of the class, kept in
// m_pInstance and returned).
template <class TJob, class TBase>
class CShardScheduleJobT : public TBase
{
public:
    virtual NewSchedule::IScheduleJob* CreateInstance()                         // 2
    {
        TJob* pJob = new TJob;
        this->m_pInstance = pJob;
        return pJob;
    }
};

// Start of a job class declaration: the class, its CreateInstance and its constructor.
#define SHARD_SCHEDULE_JOB_BEGIN(Class, Base)                                   \
    class Class : public CShardScheduleJobT<Class, Base>                        \
    {                                                                           \
    public:                                                                     \
        Class();

// Constructor body of a job class: the define and the job's flag member.
#define SHARD_SCHEDULE_JOB_CTOR(Class, pszDefine, Member, Value)                \
    Class::Class()                                                              \
    {                                                                           \
        NewSchedule::IScheduleJob::SetDefineName(pszDefine);                    \
        Member = Value;                                                         \
    }

// ---------------------------------------------------------------------------------------------
// Jobs on IShard_ScheduleJob (flag at +74)

// SCHEDULE_DEF_CHINS_TOMB, flag 0 (vftable 0x796114): Chin's tomb floors 5 and 6
// (INS_QINSHI_FLOOR_5 / _6).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_ChinsTomb, IShard_ScheduleJob)                 // 0x664cc0
    virtual BOOL OnBegin();                                                     // 5  0x65d770
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65d900
    // CShardSJ_ChinsTomb::OpenGameWorlds (0x65da80).
    void OpenGameWorlds();

public:
    GameWorldLayer* m_pFloor5;          // +78
    GameWorldLayer* m_pFloor6;          // +7C
};

C_ASSERT(FIELD_OFFSET(CShardSJ_ChinsTomb, m_pFloor5) == 0x78);
C_ASSERT(sizeof(CShardSJ_ChinsTomb) == 0x80);

// SCHEDULE_DEF_MEET_ROC, flag 0 (vftable 0x7960e4): the Roc (INS_ROC_TOP).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_Roc, IShard_ScheduleJob)                       // 0x664bc0
    virtual BOOL OnBegin();                                                     // 5  0x65e290
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65e400
    // CShardSJ_Roc::OpenGameWorld (0x65e560).
    void OpenGameWorld();

public:
    GameWorldLayer* m_pLayer;           // +78
};

C_ASSERT(FIELD_OFFSET(CShardSJ_Roc, m_pLayer) == 0x78);
C_ASSERT(sizeof(CShardSJ_Roc) == 0x7C);

// SCHEDULE_DEF_EVENT_EUBUSINESS, flag 0 (vftable 0x796234).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_EUBusinessEventUpdate, IShard_ScheduleJob)     // 0x6653e0
    virtual BOOL OnBegin();                                                     // 5  0x65ddb0
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65def0
};
C_ASSERT(sizeof(CShardSJ_EUBusinessEventUpdate) == 0x78);

// SCHEDULE_DEF_EVENT_UNIQUE_MONSTER_SPAWN, flag 0 (vftable 0x796264).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_UniqueSpawn, IShard_ScheduleJob)               // 0x6654e0
    virtual BOOL OnBegin();                                                     // 5  0x65e030
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65e180
};
C_ASSERT(sizeof(CShardSJ_UniqueSpawn) == 0x78);

// SCHEDULE_DEF_MENTOR_HONOR_UPDATE, flag 0 (vftable 0x795ff4).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_MentorHonor, IShard_ScheduleJob)               // 0x664620
    virtual BOOL OnBegin();                                                     // 5  0x65e720
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65e870
};
C_ASSERT(sizeof(CShardSJ_MentorHonor) == 0x78);

// SCHEDULE_DEF_SIEGE_REQUEST_PERIOD, flag 0 (vftable 0x796024).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegeRequestPeriod, IShard_ScheduleJob)        // 0x664780
    // The period into the main process's siege times.
    virtual BOOL OnBegin();                                                     // 5  0x65ea80
    // The next SCHEDULE_DEF_SIEGE_REQUEST_PERIOD period after now to the game servers.
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65ec10
};
C_ASSERT(sizeof(CShardSJ_SiegeRequestPeriod) == 0x78);

// SCHEDULE_DEF_SIEGE_PERIOD, flag 1 (vftable 0x796054). OnBegin sets the stage times; OnTick
// announces each stage once, OnEnd the next siege.
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegePeriod, IShard_ScheduleJob)               // 0x664880
    virtual BOOL OnBegin();                                                     // 5  0x65eeb0
    virtual BOOL OnTick(CDateTime& now);                                        // 6  0x65f060
    // The next SCHEDULE_DEF_SIEGE_PERIOD period after now (begin + 30 minutes, end) into the
    // main process's siege times and to the game servers.
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65f380

public:
    BYTE    m_byStage;          // +78 0 .. 5, the last announced stage
    DWORD   m_dwBegin;          // +7C
    DWORD   m_dwStart;          // +80 begin + 30 min
    DWORD   m_dwEnd30Min;       // +84 end - 30 min
    DWORD   m_dwEnd20Min;       // +88
    DWORD   m_dwEnd10Min;       // +8C
    DWORD   m_dwEnd1Min;        // +90
};
C_ASSERT(FIELD_OFFSET(CShardSJ_SiegePeriod, m_dwStart) == 0x80);
C_ASSERT(sizeof(CShardSJ_SiegePeriod) == 0x94);

// SCHEDULE_DEF_SIEGE_FORTRESS_DB_UPDATE, flag 1 (vftable 0x796084).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegeFortressStatusUpdate, IShard_ScheduleJob) // 0x6649c0
    virtual BOOL OnBegin();                                                     // 5  0x65f680
};
C_ASSERT(sizeof(CShardSJ_SiegeFortressStatusUpdate) == 0x78);

// SCHEDULE_DEF_SIEGE_TAX_PERIOD, flag 0 (vftable 0x7960b4).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegeTaxPeriod, IShard_ScheduleJob)            // 0x664ac0
    virtual BOOL OnBegin();                                                     // 5  0x65f7c0
    virtual void OnEnd(CDateTime& now);                                         // 7  0x65f960
};
C_ASSERT(sizeof(CShardSJ_SiegeTaxPeriod) == 0x78);

// SCHEDULE_DEF_FLAG_WORLD_PARTICIPATION, flag 0 (vftable 0x7961d4): the flag world
// participation phase (slots 5 and 7 in FlagWorld.cpp).
SHARD_SCHEDULE_JOB_BEGIN(CShard_FlagWorldParticipation, IShard_ScheduleJob)      // 0x6651e0
    virtual BOOL OnBegin();                                                     // 5  0x66d230
    virtual void OnEnd(CDateTime& now);                                         // 7  0x66d3c0
};
C_ASSERT(sizeof(CShard_FlagWorldParticipation) == 0x78);

// SCHEDULE_DEF_FLAG_WORLD, flag 1 (vftable 0x796204; the class name is spelled so in the
// original): the flag world game. Begins 5 minutes before the game: 1 minute before it the game
// servers hear of it, at the game start the participant levels are requested (slots 5 to 7 in
// FlagWorld.cpp).
SHARD_SCHEDULE_JOB_BEGIN(CSahrd_FlagWorld, IShard_ScheduleJob)                   // 0x6652e0
    virtual BOOL OnBegin();                                                     // 5  0x66d520
    virtual BOOL OnTick(CDateTime& now);                                        // 6  0x66d6c0
    virtual void OnEnd(CDateTime& now);                                         // 7  0x66d810

public:
    BYTE    m_byStage;          // +78 0 begun, 1 one minute notice sent, 2 game started
    DWORD   m_dwBegin;          // +7C
    DWORD   m_dwOneMinuteLeft;  // +80 begin + 240 s
    DWORD   m_dwGameStart;      // +84 begin + 300 s
};
C_ASSERT(FIELD_OFFSET(CSahrd_FlagWorld, m_byStage) == 0x78);
C_ASSERT(sizeof(CSahrd_FlagWorld) == 0x88);

// ---------------------------------------------------------------------------------------------
// Battle arena jobs

// The participation phase of a battle arena kind (vftable 0x796d04; the Random / Party / Guild
// / Job Participation leaves set the kind; slots 5 to 7 in BattleArena.cpp). A "let's
// participate" notice goes out every 10 minutes.
class CShard_BattleArenaScheduleParticipation : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaScheduleParticipation();                                  // 0x677de0
    // ~ 0x6656e0: the base members only.
    virtual BOOL OnBegin();                                                     // 5  0x677e50
    virtual BOOL OnTick(CDateTime& now);                                        // 6  0x678000
    virtual void OnEnd(CDateTime& now);                                         // 7  0x6780e0

public:
    DWORD   m_dwRunning;        // +78
    DWORD   m_dw7C;             // +7C
    DWORD   m_dwNoticeTick;     // +80
    DWORD   m_dw84;             // +84
    BYTE    m_byKind;           // +88 0 random, 1 party, 2 guild, 3 job
    DWORD   m_dw8C;             // +8C
};
C_ASSERT(FIELD_OFFSET(CShard_BattleArenaScheduleParticipation, m_byKind) == 0x88);
C_ASSERT(sizeof(CShard_BattleArenaScheduleParticipation) == 0x90);

// The game of a battle arena kind (vftable 0x796d34; the Random / Party / Guild / Job leaves set
// the kind; slots 5 to 7 in BattleArena.cpp). Begins 5 minutes before the game; at the game
// start the registered players are checked, near the end the game servers stop the
// resurrection and the play.
class CShard_BattleArenaSchedule : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaSchedule();                                               // 0x678250
    // ~ 0x665830: the base members only.
    virtual BOOL OnBegin();                                                     // 5  0x6782c0
    virtual BOOL OnTick(CDateTime& now);                                        // 6  0x678480
    virtual void OnEnd(CDateTime& now);                                         // 7  0x678720
    // CShard_BattleArenaSchedule::GetRemainSeconds (0x678890): end of the period - now.
    DWORD GetRemainSeconds(CDateTime now);

public:
    DWORD   m_dwRunning;            // +78 1 from the game start
    DWORD   m_bEndPlaySent;         // +7C
    DWORD   m_dwCheckTick;          // +80
    DWORD   m_dwCheckInterval;      // +84 ms; 60000, 1000 in the last minute
    BYTE    m_byKind;               // +88 0 random, 1 party, 2 guild, 3 job
    DWORD   m_bStopResurrectSent;   // +8C
    BYTE    m_byStage;              // +90 0 begun, 1 one minute notice sent, 2 game started
    DWORD   m_dwBegin;              // +94
    DWORD   m_dwOneMinuteLeft;      // +98 begin + 240 s
    DWORD   m_dwGameStart;          // +9C begin + 300 s
};
C_ASSERT(FIELD_OFFSET(CShard_BattleArenaSchedule, m_byKind) == 0x88);
C_ASSERT(FIELD_OFFSET(CShard_BattleArenaSchedule, m_byStage) == 0x90);
C_ASSERT(sizeof(CShard_BattleArenaSchedule) == 0xA0);

// A battle arena match type is open while its period runs (vftable 0x796414; slots 2 and 3
// pure; the MatchPoint / MatchFlag leaves set the type; slots 5 and 7 in BattleArena.cpp).
class CShard_BattleArenaMatchSchedule : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaMatchSchedule() { m_bWorkingStateWhenServiceStart = 0; } // 0x665e80
    // ~ 0x665fe0: the base members only.
    virtual NewSchedule::IScheduleJob* CreateInstance() = 0;                    // 2
    virtual BOOL OnBegin();                                                     // 5  0x6788c0
    virtual void OnEnd(CDateTime& now);                                         // 7  0x678a20

public:
    BYTE    m_byMatchType;      // +78 1 score / point, 2 flag
};
C_ASSERT(sizeof(CShard_BattleArenaMatchSchedule) == 0x7C);

SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaRandomParticipation,
                         CShard_BattleArenaScheduleParticipation)               // 0x6655e0
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaRandom, CShard_BattleArenaSchedule)   // 0x665730
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaPartyParticipation,
                         CShard_BattleArenaScheduleParticipation)               // 0x665880
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaParty, CShard_BattleArenaSchedule)    // 0x665980
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaGuildParticipation,
                         CShard_BattleArenaScheduleParticipation)               // 0x665a80
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaGuild, CShard_BattleArenaSchedule)    // 0x665b80
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaJobParticipation,
                         CShard_BattleArenaScheduleParticipation)               // 0x665c80
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaJob, CShard_BattleArenaSchedule)      // 0x665d80
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaMatchPoint, CShard_BattleArenaMatchSchedule) // 0x665ee0
};
SHARD_SCHEDULE_JOB_BEGIN(CShard_BattleArenaMatchFlag, CShard_BattleArenaMatchSchedule)  // 0x666030
};
