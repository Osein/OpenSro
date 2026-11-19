#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <string>

#include "BSLib/NewSchedule/ScheduleManager.h"

// NewSchedule jobs and their executor (August 2011 library, SR_ShardManager 0x6bea60..0x6befc0,
// 0x6c7510..0x6c7570; IScheduleJob's constructor, destructor and slot 0 lie in the application
// range at 0x6642c0..0x664540). The application derives the jobs (IShard_ScheduleJob) and the
// executor (CShard_ScheduleExecutor, a static object).
namespace NewSchedule
{

// IScheduleJob (vftable 0x795f80). Not a virtual destructor: the executor deletes jobs through
// this class.
class IScheduleJob
{
public:
    IScheduleJob() : m_pInstance(NULL) {}                                       // 0x6642c0
    // ~IScheduleJob 0x664540: the members only.

    virtual void SetDefineName(std::string strDefine);                          // 0  0x664330
    virtual BOOL SetScheduleInfo(const sScheduleInfo& info);                    // 1  0x6c7510
    // Makes the running copy of the prototype (also kept in m_pInstance).
    virtual IScheduleJob* CreateInstance() = 0;                                 // 2
    virtual void DeleteInstance() = 0;                                          // 3
    // Asked by AddJob before the job starts (TRUE in every SR_ShardManager job).
    virtual BOOL CanBegin() = 0;                                                // 4
    virtual BOOL OnBegin() = 0;                                                 // 5
    virtual BOOL OnTick(CDateTime& now) = 0;                                    // 6
    virtual void OnEnd(CDateTime& now) = 0;                                     // 7

public:
    sScheduleInfo   m_Info;         // +04
    std::string     m_strDefine;    // +50 code name of the schedule group the job serves
    IScheduleJob*   m_pInstance;    // +6C the running copy (application)
};

C_ASSERT(FIELD_OFFSET(IScheduleJob, m_strDefine) == 0x50);
C_ASSERT(sizeof(IScheduleJob) == 0x70);

typedef std::list<IScheduleJob*> ScheduleJobList;

// CScheduleExecutor (vftable 0x795fa4): running jobs by schedule ID and the list of job
// prototypes the application registers.
class CScheduleExecutor
{
public:
    CScheduleExecutor() : m_pJobList(NULL) {}                                   // 0x664410
    // Not virtual, like the original.
    ~CScheduleExecutor();                                                       // 0x6beab0

    virtual void Dispatch(sScheduleInfo& info) = 0;                             // 0

    BOOL CreateJobList();                                                       // 0x6beb20
    // Deletes the running jobs and the prototype list (not the prototypes); the list pointer
    // is not reset.
    void DeleteAllJobs();                                                       // 0x6bebf0
    // OnTick(now) of every running job.
    void TickAllJobs();                                                         // 0x6bed30
    IScheduleJob* FindJob(DWORD dwScheduleID);                                  // 0x6bee00
    // The first running job of the code name.
    IScheduleJob* FindJobByName(const std::string& strDefine);                  // 0x6bee60
    // CanBegin, the info, OnBegin, then the job runs under its schedule ID.
    BOOL AddJob(IScheduleJob* pJob, const sScheduleInfo& info);                 // 0x6bef50
    // The info and OnEnd(now) of the running job, which leaves the map (it is not deleted).
    void EndJob(DWORD dwScheduleID, const sScheduleInfo& info);                 // 0x6befc0
    // The prototype list (CreateJobList must have run).
    BOOL RegisterJob(IScheduleJob* pJob);                                       // 0x6bede0
    IScheduleJob* FindRegisteredJob(const std::string& strDefine);              // 0x6bedd0

public:
    std::map<DWORD, IScheduleJob*>  m_mapJob;       // +04
    ScheduleJobList*                m_pJobList;     // +10
};

C_ASSERT(FIELD_OFFSET(CScheduleExecutor, m_pJobList) == 0x10);
C_ASSERT(sizeof(CScheduleExecutor) == 0x14);

} // namespace NewSchedule
