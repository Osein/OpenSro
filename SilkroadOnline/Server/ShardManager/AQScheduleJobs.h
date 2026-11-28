#pragma once

#include <list>

#include "BSLib/NewSchedule/ScheduleManager.h"
#include "ShardManager/AQBase.h"

// Schedule jobs of the SMC console: the _RefScheduleDefine / _Schedule tables as raw rows, a
// change of one schedule (also applied to the running schedule manager), the schedule log and
// the replacement of every schedule by the standard shard's list.
//
// The rows go to the console as they come from the database: the jobs with a "Schedule
// Prepare" answer write {BYTE 1, row} per row and BYTE 0 at the end.

enum
{
    AQ_OPCODE_SCHEDULE_INIT             = 0x8019,
    AQ_OPCODE_SCHEDULE_DEFINE           = 0x801A,
    AQ_OPCODE_SCHEDULE_CHANGE           = 0x801B,
    AQ_OPCODE_SCHEDULE_LOG              = 0x8020,
    AQ_OPCODE_STANDARD_SCHEDULE_INFO    = 0x8022,
    AQ_OPCODE_SCHEDULE_CHANGE_STANDARD  = 0x8023,
};

// The shard DB table of _LogSchedule (index in the table list).
enum
{
    SHARD_TABLE_LOG_SCHEDULE    = 6,
};

// The schedule jobs that keep a DWORD of the request handler at +50.
template <class T>
class CAQScheduleJobT : public CAQJobT<T>
{
public:
    // AQ_Base::vf09_wrap (0x472190): the request header, then m_dwParam = dwParam.
    bool ReadRequestParam(CMsg* pMsg, DWORD dwRelayID, DWORD dwParam)
    {
        if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
            return false;
        m_dwParam = dwParam;
        return true;
    }

public:
    DWORD   m_dwParam;      // +50
};

// AQ_ScheduleInit (0x54 bytes): every _RefScheduleDefine row (answer 0x8019); the schedule
// manager logs its queue when the answer is sent.
class AQ_ScheduleInit : public CAQScheduleJobT<AQ_ScheduleInit>
{
public:
    AQ_ScheduleInit() {}                                                // 0x4720e0
    virtual ~AQ_ScheduleInit() {}                                       // 0 (0x478140)

    virtual bool Execute();                                             // 6 (0x4721b0)
    virtual bool OnComplete();                                          // 7 (0x4723b0)

    // 0x4726d0 (shared with AQ_ScheduleDefine and AQ_StandardShardScheduleInfo).
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
};

C_ASSERT(sizeof(AQ_ScheduleInit) == 0x54);

// AQ_ScheduleDefine (0x58 bytes): the _Schedule rows of one define (answer 0x801A).
class AQ_ScheduleDefine : public CAQScheduleJobT<AQ_ScheduleDefine>
{
public:
    AQ_ScheduleDefine() {}                                              // 0x472400
    virtual ~AQ_ScheduleDefine() {}                                     // 0 (0x472460, 0x4781a0)

    virtual bool Execute();                                             // 6 (0x4724f0)

    // 0x4724b0: the request header, m_dwParam and the define index.
    bool ReadRequestDefine(CMsg* pMsg, DWORD dwRelayID, DWORD dwParam);
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);

public:
    DWORD   m_dwDefineIdx;  // +54
};

C_ASSERT(FIELD_OFFSET(AQ_ScheduleDefine, m_dwDefineIdx) == 0x54);
C_ASSERT(sizeof(AQ_ScheduleDefine) == 0x58);

enum
{
    AQ_SCHEDULE_INSERT  = 0,
    AQ_SCHEDULE_DELETE  = 1,
};

// AQ_ScheduleChange (0x990 bytes): inserts (_InsertSchedule) or deletes one schedule (answer
// 0x801B); OnComplete applies the change to the schedule manager.
class AQ_ScheduleChange : public CAQJobT<AQ_ScheduleChange>
{
public:
    AQ_ScheduleChange();                                                // 0x472780
    virtual ~AQ_ScheduleChange() {}                                     // 0 (0x472800, 0x478200)

    virtual bool Execute();                                             // 6 (0x4729a0)
    virtual bool OnComplete();                                          // 7 (0x472d40)

    // 0x472880: the request header, the change type, the define and schedule index and the
    // raw _Schedule row. The original takes further arguments by value that it only destroys.
    bool ReadRequestChange(CMsg* pMsg, DWORD dwRelayID);
    // 0x472ce0 (shared with AQ_ScheduleChangeStandardShard): the row bytes alone.
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);

public:
    DWORD                       m_dwDefineIdx;      // +50
    DWORD                       m_dwScheduleIdx;    // +54
    std::string                 m_str58;            // +58  not used by the job
    BYTE                        m_byChangeType;     // +74  AQ_SCHEDULE_*
    BYTE                        m_ab75[7];          // +75
    // The row as the console sends it: the dates are TIMESTAMP_STRUCTs (year, month, day,
    // hour, minute, second, fraction) in the places of the SYSTEMTIMEs.
    NewSchedule::SCHEDULE_DATA  m_Row;              // +7C
};

C_ASSERT(FIELD_OFFSET(AQ_ScheduleChange, m_str58) == 0x58);
C_ASSERT(FIELD_OFFSET(AQ_ScheduleChange, m_byChangeType) == 0x74);
C_ASSERT(FIELD_OFFSET(AQ_ScheduleChange, m_Row) == 0x7C);
C_ASSERT(sizeof(AQ_ScheduleChange) == 0x990);

// AQ_ScheduleLog (0x88 bytes): _LogSchedule rows of a time range (answer 0x8020).
class AQ_ScheduleLog : public CAQJobT<AQ_ScheduleLog>
{
public:
    AQ_ScheduleLog() {}                                                 // 0x472ef0
    virtual ~AQ_ScheduleLog() {}                                        // 0 (0x472f70, 0x478260)

    virtual bool Execute();                                             // 6 (0x473060)

    // 0x473020: the request header and the time range.
    bool ReadRequestRange(CMsg* pMsg, DWORD dwRelayID);
    // 0x473360: per row {BYTE 1, ID, ServerType, ServerBodyID, ScheduleDefine, ScheduleIdx,
    // Type, OccureTime}, BYTE 0 at the end; a row of another class asserts and ends the answer
    // without the BYTE 0.
    static void WriteLogRows(ServerFramework::CMassiveMsg* pMsg, CDBRecordList* pList);

public:
    std::string     m_strFrom;      // +50  OccureTime range
    std::string     m_strTo;        // +6C
};

C_ASSERT(FIELD_OFFSET(AQ_ScheduleLog, m_strTo) == 0x6C);
C_ASSERT(sizeof(AQ_ScheduleLog) == 0x88);

// AQ_ScheduleInsertLog (0x6C bytes): runs a prepared _LogSchedule insert on the log database
// (DB_Insert_LogSchedule fills the statement). No console answer of its own.
class AQ_ScheduleInsertLog : public CAQJobT<AQ_ScheduleInsertLog>
{
public:
    AQ_ScheduleInsertLog() {}                                           // 0x4734c0
    virtual ~AQ_ScheduleInsertLog() {}                                  // 0 (0x473530, 0x4782c0)

    virtual bool Execute();                                             // 6 (0x473810)

public:
    std::string     m_strQuery;     // +50
};

C_ASSERT(FIELD_OFFSET(AQ_ScheduleInsertLog, m_strQuery) == 0x50);
C_ASSERT(sizeof(AQ_ScheduleInsertLog) == 0x6C);

// AQ_StandardShardScheduleInfo (0x54 bytes): every schedule with its define name, as the
// standard shard's list (answer 0x8022).
class AQ_StandardShardScheduleInfo : public CAQScheduleJobT<AQ_StandardShardScheduleInfo>
{
public:
    AQ_StandardShardScheduleInfo() {}                                   // 0x473b60
    virtual ~AQ_StandardShardScheduleInfo() {}                          // 0 (0x478320)

    virtual bool Execute();                                             // 6 (0x473c10)

    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
};

C_ASSERT(sizeof(AQ_StandardShardScheduleInfo) == 0x54);

#pragma pack(push, 1)

// A schedule of the standard shard (0x98B bytes): the define name and the _Schedule row (the
// row of AQ_StandardShardScheduleInfo; dates as TIMESTAMP_STRUCTs).
struct AQ_STANDARD_SCHEDULE_ROW
{
    char                        szDefineName[0x78];     // +000
    NewSchedule::SCHEDULE_DATA  Data;                   // +078
};

#pragma pack(pop)

C_ASSERT(sizeof(AQ_STANDARD_SCHEDULE_ROW) == 0x98B);

// AQ_ScheduleChangeStandardShard (0x60 bytes): deletes every schedule and inserts the list
// (_InsertSchedule_AllShard), each one also into the schedule manager (answer 0x8023).
class AQ_ScheduleChangeStandardShard : public CAQJobT<AQ_ScheduleChangeStandardShard>
{
public:
    AQ_ScheduleChangeStandardShard() {}                                 // 0x473e40
    virtual ~AQ_ScheduleChangeStandardShard() {}                        // 0 (0x473eb0, 0x478380)

    virtual bool Execute();                                             // 6 (0x474020)
    virtual bool OnComplete();                                          // 7 (0x4744a0)
    virtual bool OnFail();                                              // 8 (0x474500)

    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);

public:
    DWORD                                   m_dw50;             // +50
    std::list<AQ_STANDARD_SCHEDULE_ROW>     m_listSchedule;     // +54
};

C_ASSERT(FIELD_OFFSET(AQ_ScheduleChangeStandardShard, m_listSchedule) == 0x54);
C_ASSERT(sizeof(AQ_ScheduleChangeStandardShard) == 0x60);

// AQ_ScheduleChangeStandardShard::CopyRecord (0x474550): the row of a standard shard schedule
// with the new schedule and define index, dates as SYSTEMTIMEs (weekday and milliseconds are
// not set).
NewSchedule::SCHEDULE_DATA* AQ_CopyStandardScheduleRow(NewSchedule::SCHEDULE_DATA* pDst,
                                                       DWORD dwScheduleIdx, DWORD dwDefineIdx,
                                                       const AQ_STANDARD_SCHEDULE_ROW* pSrc);
