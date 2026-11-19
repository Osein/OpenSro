#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "BSLib/NewSchedule/ScheduleExecutor.h"
#include "BSLib/NewSchedule/ScheduleManager.h"

class CMsg;

// The shard manager's schedules: CShard_ScheduleManager (the _RefScheduleDefine / _Schedule
// tables and the begin / end events), CShard_ScheduleExecutor (the running jobs) and the job
// base IShard_ScheduleJob. A job tells the game servers about its schedule with 0x34FE:
//     DWORD group ID, DWORD schedule ID, DWORD begin, DWORD end, DWORD state (0 end, 1 begin,
//     2 tick), string define, then the job's own fields.

enum
{
    MSG_SHARD_SCHEDULE_NOTIFY = 0x34FE,
};

// The head of 0x34FE: group, schedule, begin, end, state and define.
void ShardSchedule_WriteHead(CMsg* pMsg, const NewSchedule::sScheduleInfo& info);

// The job classes are in ShardScheduleJob.h, the temple gates in ShardTempleGate.h.

// CShard_ScheduleExecutor (vftable 0x795fdc), the static object g_ShardScheduleExecutor
// (0x8a7b48).
class CShard_ScheduleExecutor : public NewSchedule::CScheduleExecutor
{
public:
    // 0x6645c0 (dynamic initializer): the verbose flag is cleared.
    CShard_ScheduleExecutor() { m_bVerboseLog = FALSE; }

    // The begin (bBegin 1) or end (0) of a schedule event; asserts on another state. The
    // original returns the result of the slot it calls (the base declares no result).
    virtual void Dispatch(NewSchedule::sScheduleInfo& info);                    // 0  0x6673e0
    // A schedule message (0x34FE head: group, schedule, begin, end, state, define) into a
    // cleared info, then begin (state 1), end (0) or tick (2); asserts and gives TRUE on another
    // state. dwParam is not used.
    virtual BOOL OnScheduleMsg(CMsg* pMsg, DWORD dwParam);                      // 1  0x667420
    // The running copy of the registered job: CreateInstance, SetScheduleInfo, slot 8, AddJob
    // (a failed insert is logged, the copy stays). FALSE without a prototype or copy.
    virtual BOOL BeginScheduleJob(NewSchedule::sScheduleInfo& info, DWORD dw1,
                                  DWORD dw2);                                   // 2  0x6675c0
    // Slot 10 of the running job, CScheduleExecutor::EndJob (OnEnd), then DeleteInstance on
    // the running copy (BUG(SM-145)). FALSE when the job does not run.
    virtual BOOL EndScheduleJob(NewSchedule::sScheduleInfo& info, DWORD dw1,
                                DWORD dw2);                                     // 3  0x667690
    // Slot 9 of the running job (not OnTick). FALSE when the job does not run.
    virtual BOOL TickScheduleJob(NewSchedule::sScheduleInfo& info, DWORD dw1,
                                 DWORD dw2);                                    // 4  0x667720

    // 0x667310: m_bWorkingStateWhenServiceStart of the registered job, -1 without one.
    static int GetWorkingStateWhenServiceStart(std::string strDefine);
    // 0x666be0: one prototype of every job class into the job list (CreateJobList must have
    // run); a failed registration is logged. Always TRUE.
    static BOOL RegisterDefaultJobs();

public:
    BOOL    m_bVerboseLog;  // +14 1: the schedule logs go out with 0x2000001, else 0
};

C_ASSERT(sizeof(CShard_ScheduleExecutor) == 0x18);

extern CShard_ScheduleExecutor g_ShardScheduleExecutor;

// The flags of the schedule progress logs.
inline DWORD ShardScheduleLogFlags()
{
    return g_ShardScheduleExecutor.m_bVerboseLog != 1 ? 0 : 0x2000001;
}

// CShard_ScheduleManager (vftable 0x7957c8), the static object g_ShardScheduleManager
// (0x8a7b24).
class CShard_ScheduleManager : public NewSchedule::CScheduleManager
{
public:
    CShard_ScheduleManager() {}                                                 // 0x663500

    // CSiegeFortressMgr::InitSiegeScheduleTimes first.
    virtual void RebuildEventList();                                            // 0  0x663660
    // A period running at service start: the job runs when its define says so.
    virtual void OnServiceStartSchedule(NewSchedule::sScheduleEvent& event);   // 2  0x663690
    // SCHEDULE_DEF_SPECIAL_GOODS_SELL goes straight to the game servers, the rest to the
    // executor.
    virtual void OnScheduleEvent(NewSchedule::sScheduleEvent& event);          // 3  0x663870

    // SELECT * FROM _RefScheduleDefine and _Schedule into the groups (FALSE when one fails).
    static BOOL LoadFromDB();                                                   // 0x663d20
    // The own server body is in state 5 or later (logs and FALSE without one).
    static BOOL IsServerBodyReady();                                            // 0x663cf0
    // Logs the event queue.
    static void DumpSchedules();                                                // 0x663aa0
    // 0x34FE {head of info} to every game server.
    static void BroadcastSchedule(const NewSchedule::sScheduleInfo& info);     // 0x664030

    // CDBQuery row callbacks (a buffer of m_pUserData bytes at m_dwParam before each row).
    int OnScheduleDefineRow(DWORD dwQuery, DWORD dwUnused);                     // 0x663f20
    int OnScheduleRow(DWORD dwQuery, DWORD dwUnused);                           // 0x663f70
};

C_ASSERT(sizeof(CShard_ScheduleManager) == 0x24);

extern CShard_ScheduleManager g_ShardScheduleManager;

// sScheduleRow::Copy (0x663560): field by field into a cleared row; the weekdays and
// milliseconds of both SYSTEMTIMEs stay 0, the strings are copied with strcpy_s.
NewSchedule::SCHEDULE_DATA* CopyScheduleRow(const NewSchedule::SCHEDULE_DATA* pSrc,
                                            NewSchedule::SCHEDULE_DATA* pDst);

// Schedule parameter text "KEY=VALUE/KEY=VALUE".
typedef std::vector<std::pair<std::string, std::string> > ScheduleParamList;
// 0x6661e0: the tokens of strParam between '/' (always TRUE).
BOOL ScheduleParam_SplitBySlash(const std::string& strParam, std::vector<std::string>* pTokens);
// 0x666260: every token split at '='; a token without a value logs the whole text and fails
// (the pairs before it stay in *pParams).
BOOL ScheduleParamParser_Parse(const std::string& strParam, ScheduleParamList* pParams);
