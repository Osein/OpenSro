// NewSchedule (August 2011 library): rule periods across day, week and month ends with fixed
// local times, the event queue of CScheduleManager and the job calls of CScheduleExecutor.
// Compiled with SHARD_DEFS (the unchecked CDateTime constructor), linked with SHARD_LIBS.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "BSLib/NewSchedule/ScheduleExecutor.h"
#include "TestCheck.h"

using namespace NewSchedule;

static __time32_t T(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec)
{
    return CDateTime(nYear, nMonth, nDay, nHour, nMin, nSec).GetTime();
}

static void SetSystemTime(SYSTEMTIME& st, __time32_t t)
{
    CDateTime dt(t);
    dt.GetAsSystemTime(st);
}

// A rule valid through 2011 starting 2011-06-01 00:00, daily start 10:00, one hour.
static void InitData(SCHEDULE_DATA& data, int nType)
{
    memset(&data, 0, sizeof(data));
    data.dwScheduleID = 7;
    data.dwDefineID = 1;
    SetSystemTime(data.stBegin, T(2011, 6, 1, 0, 0, 0));
    SetSystemTime(data.stEnd, T(2011, 12, 31, 23, 0, 0));
    data.nType = nType;
    data.nInterval = 1;
    data.byHour = 10;
    data.nDuration = 3600;
    strcpy_s(data.szName, sizeof(data.szName), "Rule");
}

static ISchedule* Make(const SCHEDULE_DATA& data)
{
    ISchedule* p = ISchedule::CreateByType(&data);
    memcpy(&p->m_Data, &data, sizeof(data));
    return p;
}

static void TestFactory()
{
    SCHEDULE_DATA data;
    for (int nType = 1; nType <= 10; ++nType)
    {
        InitData(data, nType);
        ISchedule* p = ISchedule::CreateByType(&data);
        CHECK(p != NULL);
        // BUG(FW11-022): Everymonths_Date says 3.
        CHECK(p->m_nType == (nType == 6 ? 3 : nType));
        delete p;
    }
    InitData(data, 11);
    CHECK(ISchedule::CreateByType(&data) == NULL);
    CHECK(ISchedule::CreateByType(NULL) == NULL);
}

static void TestHelpers()
{
    CDateTime t(T(2011, 6, 20, 12, 0, 0));
    CHECK(GetWeekOfMonth(t) == 4);
    CDateTime start(T(2011, 6, 1, 0, 0, 0));
    CDateTime t2(T(2011, 7, 20, 0, 0, 0));
    CHECK(CountMonths(start, t2) == 2);
    CDateTime sunday(T(2011, 6, 5, 0, 0, 0));
    CDateTime t3(T(2011, 6, 14, 12, 0, 0));
    CHECK(CountWeeksBetween(sunday, t3) == 2);
}

static void TestEverydays()
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_EVERYDAYS);
    ISchedule* p = Make(data);
    sSchedulePeriod cur, next;
    CDateTime running(T(2011, 6, 30, 10, 30, 0));
    CHECK(p->GetCurrentPeriod(running, &cur, &next) == TRUE);
    CHECK(cur.tBegin == T(2011, 6, 30, 10, 0, 0) && cur.tEnd == T(2011, 6, 30, 11, 0, 0));
    CHECK(next.tBegin == T(2011, 7, 1, 10, 0, 0) && next.tEnd == T(2011, 7, 1, 11, 0, 0));
    CHECK(next.dwScheduleID == 7 && next.strName == "Rule");
    CDateTime after(T(2011, 6, 30, 12, 0, 0));
    CHECK(p->GetCurrentPeriod(after, &cur, &next) == FALSE);
    CHECK(next.tBegin == T(2011, 7, 1, 10, 0, 0));
    sSchedulePeriod prev;
    CHECK(p->GetPrevPeriod(running, &prev) == TRUE);
    CHECK(prev.tBegin == T(2011, 6, 29, 10, 0, 0));
    CHECK(p->GetPrevPeriod(after, &prev) == TRUE);
    CHECK(prev.tBegin == T(2011, 6, 30, 10, 0, 0));
    p->m_Period.tBegin = T(2011, 6, 30, 10, 0, 0);
    p->AdvanceToNextPeriod(after);
    CHECK(p->m_Period.tBegin == T(2011, 7, 1, 10, 0, 0));
    CHECK(p->m_Period.tEnd == T(2011, 7, 1, 11, 0, 0));
    delete p;
}

static void TestSomedays()
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_SOMEDAYS);
    data.nInterval = 3;
    data.byDay = 2;
    ISchedule* p = Make(data);
    sSchedulePeriod cur, next;
    CDateTime t(T(2011, 6, 3, 9, 0, 0));
    CHECK(p->GetCurrentPeriod(t, &cur, &next) == FALSE);
    CHECK(next.tBegin == T(2011, 6, 5, 10, 0, 0));
    p->m_Period.tBegin = T(2011, 6, 5, 10, 0, 0);
    p->AdvanceToNextPeriod(t);
    CHECK(p->m_Period.tBegin == T(2011, 6, 8, 10, 0, 0));
    delete p;
}

static void TestWeeks()
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_EVERYWEEKS_DAYOFWEEK);
    data.byDayOfWeek = 2;   // Monday
    data.byHour = 20;
    ISchedule* p = Make(data);
    sSchedulePeriod cur, next, prev;
    CDateTime saturday(T(2011, 7, 2, 12, 0, 0));
    CHECK(p->GetCurrentPeriod(saturday, &cur, &next) == FALSE);
    CHECK(next.tBegin == T(2011, 7, 4, 20, 0, 0));
    CHECK(p->GetPrevPeriod(saturday, &prev) == TRUE);
    CHECK(prev.tBegin == T(2011, 6, 27, 20, 0, 0));
    delete p;

    InitData(data, SCHEDULE_TYPE_SOMEWEEKS_DAYOFWEEK);
    SetSystemTime(data.stBegin, T(2011, 6, 5, 0, 0, 0));
    data.nInterval = 2;
    data.byWeek = 1;
    data.byDayOfWeek = 2;
    p = Make(data);
    CDateTime tuesday(T(2011, 6, 14, 12, 0, 0));
    p->GetCurrentPeriod(tuesday, &cur, &next);
    CHECK(next.tBegin == T(2011, 6, 20, 10, 0, 0));
    delete p;
}

static void TestMonths()
{
    SCHEDULE_DATA data;
    sSchedulePeriod cur, next;
    // Week 2 of July 2011 begins on Sunday July 3: its Tuesday is July 5.
    InitData(data, SCHEDULE_TYPE_EVERYMONTHS_DAYOFWEEK);
    data.byWeek = 2;
    data.byDayOfWeek = 3;
    ISchedule* p = Make(data);
    CDateTime t(T(2011, 6, 20, 12, 0, 0));
    p->GetCurrentPeriod(t, &cur, &next);
    CHECK(next.tBegin == T(2011, 7, 5, 10, 0, 0));
    delete p;

    // June 31 is normalised to July 1.
    InitData(data, SCHEDULE_TYPE_EVERYMONTHS_DATE);
    data.byDay = 31;
    p = Make(data);
    p->GetCurrentPeriod(t, &cur, &next);
    CHECK(next.tBegin == T(2011, 7, 1, 10, 0, 0));
    delete p;

    InitData(data, SCHEDULE_TYPE_SOMEMONTHS_DATE);
    data.nInterval = 2;
    data.byMonth = 1;
    data.byDay = 15;
    p = Make(data);
    CDateTime july(T(2011, 7, 20, 0, 0, 0));
    p->GetCurrentPeriod(july, &cur, &next);
    CHECK(next.tBegin == T(2011, 8, 15, 10, 0, 0));
    p->m_Period.tBegin = T(2011, 8, 15, 10, 0, 0);
    p->AdvanceToNextPeriod(july);
    CHECK(p->m_Period.tBegin == T(2011, 10, 15, 10, 0, 0));
    delete p;
}

static void TestRepeat()
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_REPEAT_SOMETIME);
    data.nDuration = 7200;
    data.nRepeatInterval = 30;
    data.nRepeatLength = 10;
    ISchedule* p = Make(data);
    sSchedulePeriod cur, next;
    CDateTime between(T(2011, 6, 30, 10, 45, 0));
    CHECK(p->GetCurrentPeriod(between, &cur, &next) == FALSE);
    CHECK(next.tBegin == T(2011, 6, 30, 11, 0, 0) && next.tEnd == T(2011, 6, 30, 11, 10, 0));
    CDateTime inSlot(T(2011, 6, 30, 10, 35, 0));
    CHECK(p->GetCurrentPeriod(inSlot, &cur, &next) == TRUE);
    // BUG(FW11-025): the running slot is put 5 minutes after the time.
    CHECK(cur.tBegin == T(2011, 6, 30, 10, 40, 0) && cur.tEnd == T(2011, 6, 30, 10, 45, 0));
    CHECK(next.tBegin == T(2011, 6, 30, 11, 10, 0));
    // After the daily window the next slot is the first one of the next day.
    CDateTime late(T(2011, 6, 30, 12, 30, 0));
    p->GetCurrentPeriod(late, &cur, &next);
    CHECK(next.tBegin == T(2011, 7, 1, 10, 0, 0) && next.tEnd == T(2011, 7, 1, 10, 10, 0));
    delete p;
}

static void TestOnce()
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_ONCE_DATE);
    SetSystemTime(data.stBegin, T(2011, 6, 10, 10, 0, 0));
    SetSystemTime(data.stEnd, T(2011, 6, 10, 12, 0, 0));
    ISchedule* p = Make(data);
    sSchedulePeriod cur, next;
    CDateTime t(T(2011, 6, 10, 11, 0, 0));
    CHECK(p->GetCurrentPeriod(t, &cur, &next) == TRUE);
    CHECK(cur.tBegin == T(2011, 6, 10, 10, 0, 0) && cur.tEnd == T(2011, 6, 10, 12, 0, 0));
    CHECK(next.tBegin == T(2011, 6, 10, 12, 30, 0) && next.tEnd == next.tBegin);
    CHECK(p->IsAfterEnd(t) == FALSE);
    CDateTime late(T(2011, 6, 10, 13, 0, 0));
    CHECK(p->IsAfterEnd(late) == TRUE);
    CHECK(p->ClampToEndTime(late).GetTime() == T(2011, 6, 10, 12, 0, 0));
    delete p;
}

// ---------------------------------------------------------------------------------------------
// Manager and executor

class CTestManager : public CScheduleManager
{
public:
    virtual void OnServiceStartSchedule(sScheduleEvent& event) { m_vecStart.push_back(event); }
    virtual void OnScheduleEvent(sScheduleEvent& event) { m_vecEvent.push_back(event); }
    std::vector<sScheduleEvent> m_vecStart;
    std::vector<sScheduleEvent> m_vecEvent;
};

static void AddOnce(CScheduleManager& manager, DWORD dwGroup, DWORD dwID, __time32_t tBegin,
                    __time32_t tEnd)
{
    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_ONCE_DATE);
    data.dwScheduleID = dwID;
    data.dwDefineID = dwGroup;
    SetSystemTime(data.stBegin, tBegin);
    SetSystemTime(data.stEnd, tEnd);
    manager.AddSchedule(&data);
}

static void TestManager()
{
    CTestManager manager;
    SCHEDULE_DEFINE_DATA define;
    memset(&define, 0, sizeof(define));
    define.dwDefineID = 1;
    strcpy_s(define.szCodeName, sizeof(define.szCodeName), "GROUP_A");
    manager.AddGroup(&define);
    define.dwDefineID = 2;
    strcpy_s(define.szCodeName, sizeof(define.szCodeName), "GROUP_B");
    manager.AddGroup(&define);
    CHECK(manager.FindGroup(2) && manager.FindGroup(2)->GetDefineName() == "GROUP_B");
    CHECK(manager.FindGroup(3) == NULL);

    __time32_t now = _time32(NULL);
    // Same second in begin and end: Once_Date takes the end's second from the begin (FW11-026).
    AddOnce(manager, 1, 11, now - 59, now + 1);         // running, ends in a second
    AddOnce(manager, 2, 21, now + 3600, now + 7200);    // later
    manager.RebuildEventList();
    CHECK(manager.m_bRunning == TRUE);
    // The running period was reported at service start; its end event is queued.
    CHECK(manager.m_vecStart.size() == 1 && manager.m_vecStart[0].dwScheduleID == 11);
    CHECK(manager.m_listEvent.size() == 3);
    std::list<sScheduleEvent>::iterator it = manager.m_listEvent.begin();
    CHECK(it->dwScheduleID == 11 && it->bBegin == 0 && it->dwEnd == (DWORD)now + 1);
    ++it;
    CHECK(it->dwScheduleID == 21 && it->bBegin == 1 && it->dwBegin == (DWORD)now + 3600);
    ++it;
    CHECK(it->dwScheduleID == 21 && it->bBegin == 0 && it->dwEnd == (DWORD)now + 7200);
    CHECK(manager.m_listEvent.front().strName == "Rule");

    manager.ProcessDueEvents();
    CHECK(manager.m_vecEvent.empty());
    while (_time32(NULL) <= now + 1)
        Sleep(50);
    manager.ProcessDueEvents();
    CHECK(manager.m_vecEvent.size() == 1 && manager.m_vecEvent[0].dwScheduleID == 11);
    CHECK(manager.m_listEvent.size() == 2);
    sScheduleInfo info = manager.m_vecEvent[0].ToScheduleInfo();
    CHECK(info.strDefine == "GROUP_A" && info.dwGroupID == 1 && info.bBegin == FALSE);

    SCHEDULE_DATA data;
    InitData(data, SCHEDULE_TYPE_ONCE_DATE);
    data.dwScheduleID = 21;
    data.dwDefineID = 2;
    manager.RemoveSchedule(&data);
    CHECK(manager.m_listEvent.empty());
}

class CTestJob : public IScheduleJob
{
public:
    CTestJob(const char* pszDefine) : m_nBegin(0), m_nTick(0), m_nEnd(0)
    {
        SetDefineName(pszDefine);
    }
    virtual IScheduleJob* CreateInstance() { return NULL; }
    virtual void DeleteInstance() {}
    virtual BOOL CanBegin() { return TRUE; }
    virtual BOOL OnBegin() { ++m_nBegin; return TRUE; }
    virtual BOOL OnTick(CDateTime&) { ++m_nTick; return TRUE; }
    virtual BOOL OnEnd(CDateTime&) { ++m_nEnd; return TRUE; }
    int m_nBegin;
    int m_nTick;
    int m_nEnd;
};

class CTestExecutor : public CScheduleExecutor
{
public:
    virtual void Dispatch(sScheduleInfo&) {}
};

static void TestExecutor()
{
    CTestExecutor executor;
    CHECK(executor.CreateJobList() == TRUE);
    CTestJob prototype("DEF_A");
    CHECK(executor.RegisterJob(&prototype) == TRUE);
    CHECK(executor.RegisterJob(NULL) == FALSE);
    CHECK(executor.FindRegisteredJob("DEF_A") == &prototype);
    CHECK(executor.FindRegisteredJob("DEF_B") == NULL);

    CTestJob* pJob = new CTestJob("DEF_A");
    sScheduleInfo info;
    info.dwScheduleID = 5;
    info.bBegin = TRUE;
    CHECK(executor.AddJob(pJob, info) == TRUE);
    CHECK(pJob->m_nBegin == 1 && pJob->m_Info.dwScheduleID == 5);
    CHECK(executor.FindJob(5) == pJob && executor.FindJob(6) == NULL);
    CHECK(executor.FindJobByName("DEF_A") == pJob);
    executor.TickAllJobs();
    executor.TickAllJobs();
    CHECK(pJob->m_nTick == 2);
    info.bBegin = FALSE;
    executor.EndJob(5, info);
    CHECK(pJob->m_nEnd == 1 && pJob->m_Info.bBegin == FALSE);
    CHECK(executor.FindJob(5) == NULL);
    delete pJob;
}

int main()
{
    TestFactory();
    TestHelpers();
    TestEverydays();
    TestSomedays();
    TestWeeks();
    TestMonths();
    TestRepeat();
    TestOnce();
    TestManager();
    TestExecutor();
    return TEST_RESULT("NewScheduleTest");
}
