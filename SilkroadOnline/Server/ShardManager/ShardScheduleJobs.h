#pragma once

#include "ShardManager/ShardSchedule.h"

class CGameWorldLayer;

// The shard schedule jobs. Each class registers one prototype for its _RefScheduleDefine code
// name (CShard_ScheduleExecutor::RegisterDefaultJobs); the constructors (0x664620..0x666030)
// set the define and the service-start flag, slot 2 (CreateInstance) makes the running copy.
// OnBegin / OnTick / OnEnd marked "SM_worldN" are provided by that group.

// Start of a job class declaration: the class, its CreateInstance and its constructor.
#define SHARD_SCHEDULE_JOB_BEGIN(Class, Base)                                   \
    class Class : public CShardScheduleJobT<Class, Base>                        \
    {                                                                           \
    public:                                                                     \
        Class();

// --- jobs on IShard_ScheduleJob (flag at +74) ---------------------------------------------

// SCHEDULE_DEF_MENTOR_HONOR_UPDATE, flag 0 (vftable 0x795ff4).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_MentorHonor, IShard_ScheduleJob)               // 0x664620
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2
};
C_ASSERT(sizeof(CShardSJ_MentorHonor) == 0x78);

// SCHEDULE_DEF_SIEGE_REQUEST_PERIOD, flag 0 (vftable 0x796024).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegeRequestPeriod, IShard_ScheduleJob)        // 0x664780
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2
};
C_ASSERT(sizeof(CShardSJ_SiegeRequestPeriod) == 0x78);

// SCHEDULE_DEF_SIEGE_PERIOD, flag 1 (vftable 0x796054). OnBegin sets the stage times; OnTick
// announces each stage once, OnEnd the next siege.
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_SiegePeriod, IShard_ScheduleJob)               // 0x664880
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnTick(CDateTime& now);                                        // 6  0x65f060
    // The next SCHEDULE_DEF_SIEGE_PERIOD period after now (begin + 30 minutes, end) into the
    // main process's siege times and to the game servers; FALSE without one.
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  0x65f380

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
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  0x65f960
};
C_ASSERT(sizeof(CShardSJ_SiegeTaxPeriod) == 0x78);

// SCHEDULE_DEF_MEET_ROC, flag 0 (vftable 0x7960e4).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_Roc, IShard_ScheduleJob)                       // 0x664bc0
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2

public:
    CGameWorldLayer*    m_pLayer;   // +78
};
C_ASSERT(sizeof(CShardSJ_Roc) == 0x7C);

// SCHEDULE_DEF_CHINS_TOMB, flag 0 (vftable 0x796114).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_ChinsTomb, IShard_ScheduleJob)                 // 0x664cc0
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2

public:
    CGameWorldLayer*    m_pLayer1;  // +78
    CGameWorldLayer*    m_pLayer2;  // +7C
};
C_ASSERT(sizeof(CShardSJ_ChinsTomb) == 0x80);

// SCHEDULE_DEF_FLAG_WORLD_PARTICIPATION, flag 0 (vftable 0x7961d4).
SHARD_SCHEDULE_JOB_BEGIN(CShard_FlagWorldParticipation, IShard_ScheduleJob)      // 0x6651e0
    virtual BOOL OnBegin();                                                     // 5  SM_world4
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world4
};
C_ASSERT(sizeof(CShard_FlagWorldParticipation) == 0x78);

// SCHEDULE_DEF_FLAG_WORLD, flag 1 (vftable 0x796204; the class name is spelled so).
SHARD_SCHEDULE_JOB_BEGIN(CSahrd_FlagWorld, IShard_ScheduleJob)                   // 0x6652e0
    virtual BOOL OnBegin();                                                     // 5  SM_world4
    virtual BOOL OnTick(CDateTime& now);                                        // 6  SM_world4
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world4

public:
    DWORD   m_dw78;     // +78
    DWORD   m_dw7C;     // +7C
    DWORD   m_dw80;     // +80
    DWORD   m_dw84;     // +84
};
C_ASSERT(sizeof(CSahrd_FlagWorld) == 0x88);

// SCHEDULE_DEF_EVENT_EUBUSINESS, flag 0 (vftable 0x796234).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_EUBusinessEventUpdate, IShard_ScheduleJob)     // 0x6653e0
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2
};
C_ASSERT(sizeof(CShardSJ_EUBusinessEventUpdate) == 0x78);

// SCHEDULE_DEF_EVENT_UNIQUE_MONSTER_SPAWN, flag 0 (vftable 0x796264).
SHARD_SCHEDULE_JOB_BEGIN(CShardSJ_UniqueSpawn, IShard_ScheduleJob)               // 0x6654e0
    virtual BOOL OnBegin();                                                     // 5  SM_world2
    virtual BOOL OnEnd(CDateTime& now);                                         // 7  SM_world2
};
C_ASSERT(sizeof(CShardSJ_UniqueSpawn) == 0x78);

// --- battle arena jobs --------------------------------------------------------------------

// Provided by SM_world5: the participation phase (ctor 0x677de0, slots 5..7 0x677e50,
// 0x678000, 0x6780e0) and the match (ctor 0x678250, slots 0x6782c0, 0x678480, 0x678720) of an
// arena type. The leaf classes set the type.
class CShard_BattleArenaScheduleParticipation : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaScheduleParticipation();
    // ~ 0x6656e0: the base members only.
    virtual BOOL OnBegin();
    virtual BOOL OnTick(CDateTime& now);
    virtual BOOL OnEnd(CDateTime& now);

public:
    DWORD   m_dw78;         // +78
    DWORD   m_dw7C;         // +7C
    DWORD   m_dw80;         // +80
    DWORD   m_dw84;         // +84
    BYTE    m_byArenaType;  // +88 0 random, 1 party, 2 guild, 3 job
    DWORD   m_dw8C;         // +8C
};
C_ASSERT(FIELD_OFFSET(CShard_BattleArenaScheduleParticipation, m_byArenaType) == 0x88);
C_ASSERT(sizeof(CShard_BattleArenaScheduleParticipation) == 0x90);

class CShard_BattleArenaSchedule : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaSchedule();
    // ~ 0x665830: the base members only.
    virtual BOOL OnBegin();
    virtual BOOL OnTick(CDateTime& now);
    virtual BOOL OnEnd(CDateTime& now);

public:
    DWORD   m_dw78;         // +78
    DWORD   m_dw7C;         // +7C
    DWORD   m_dw80;         // +80
    DWORD   m_dw84;         // +84
    BYTE    m_byArenaType;  // +88 0 random, 1 party, 2 guild, 3 job
    DWORD   m_dw8C;         // +8C
    DWORD   m_adw90[4];     // +90
};
C_ASSERT(FIELD_OFFSET(CShard_BattleArenaSchedule, m_byArenaType) == 0x88);
C_ASSERT(sizeof(CShard_BattleArenaSchedule) == 0xA0);

// The match phase of an arena game (vftable 0x796414; slots 2 and 3 pure). Slots 5 and 7
// (0x6788c0, 0x678a20) are provided by SM_world5.
class CShard_BattleArenaMatchSchedule : public IShard_ScheduleJob
{
public:
    CShard_BattleArenaMatchSchedule() { m_bWorkingStateWhenServiceStart = 0; } // 0x665e80
    // ~ 0x665fe0: the base members only.
    virtual NewSchedule::IScheduleJob* CreateInstance() = 0;                    // 2
    virtual BOOL OnBegin();                                                     // 5
    virtual BOOL OnEnd(CDateTime& now);                                         // 7

public:
    BYTE    m_byMatchType;  // +78 1 point, 2 flag
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
