#pragma once

#include "BSLib/DB/Database.h"
#include "ShardManager/AQBase.h"

// AQ_ServerEvent (vftable 0x787940, 0x7C bytes): a change of a server event of the shard
// (update, insert reward, delete or update reward). The owner fills the fields; the job has no
// console answer (OnComplete / OnFail are the shared 0x69bb10).
class AQ_ServerEvent : public CAQJobT<AQ_ServerEvent>
{
public:
    AQ_ServerEvent();                                                   // 0x47a8f0
    virtual ~AQ_ServerEvent() {}                                        // 0 (0x47a660, 0x47cf60)

    virtual void ReleaseId();                                           // 2 (0x47a6e0)
    virtual bool Execute();                                             // 6 (0x47aad0)
    virtual bool OnComplete();                                          // 7 (0x69bb10)
    virtual bool OnFail();                                              // 8 (0x69bb10)

    // 0x47af20, 0x47aff0, 0x47b0d0: run m_strQuery ({?=CALL ...}) on the shard database. A
    // failed statement, return value -1001 (logged as failure) or -1000 (logged as missing or
    // existing row) fail; any other value succeeds.
    bool ExecUpdate();
    bool ExecInsertReward();
    bool ExecDeleteOrUpdateReward();

public:
    DWORD           m_dwServerEventID;  // +50
    DWORD           m_dw54;             // +54
    DWORD           m_dw58;             // +58
    int             m_nEventType;       // +5C  1 update, 2 insert reward, 3 / 4 reward change
    std::string     m_strQuery;         // +60
};

C_ASSERT(FIELD_OFFSET(AQ_ServerEvent, m_nEventType) == 0x5C);
C_ASSERT(FIELD_OFFSET(AQ_ServerEvent, m_strQuery) == 0x60);
C_ASSERT(sizeof(AQ_ServerEvent) == 0x7C);

enum
{
    AQ_OPCODE_SERVER_EVENT_LOG  = 0x8018,
};

// Modes of AQ_ServerEventLog.
enum
{
    AQ_SERVER_EVENT_LOG_SELECT  = 3,    // console query of _LogServerEvent (ReadRequest)
    AQ_SERVER_EVENT_LOG_INSERT  = 5,    // prepared insert of a log row; also the connection
};

// AQ_ServerEventLog (vftable 0x787970, 0xD4 bytes): the console query of the server event log
// (answer 0x8018) or, without an answer, one insert into it.
class AQ_ServerEventLog : public CAQJobT<AQ_ServerEventLog>
{
public:
    AQ_ServerEventLog();                                                // 0x47b1f0
    virtual ~AQ_ServerEventLog() {}                                     // 0 (0x47a700, 0x47cfc0)

    virtual void ReleaseId();                                           // 2 (0x47a810)
    virtual bool Execute();                                             // 6 (0x47bb80)
    virtual bool OnComplete();                                          // 7 (0x47bb40)
    virtual bool OnFail();                                              // 8 (0x47bb60)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x47b390)

    // 0x47bde0: the LogType condition of m_byLogType.
    void BuildLogTypeFilter(std::string* pstrFilter);
    // 0x47c040: " [ServerEvent] (<log type>)", appended to m_strDesc.
    void BuildDescription();
    // 0x47bec0: per row {ID, EventTime, ServerEventID, BYTE LogType, strDesc}, then the time of
    // the last row as text.
    static void WriteLogRows(CDBRecordList* pList, ServerFramework::CMassiveMsg* pMsg);

public:
    DWORD           m_dwAfterID;        // +50  rows with a greater ID
    BYTE            m_by54;             // +54
    DWORD           m_dwServerEventID;  // +58
    std::string     m_strQuery;         // +5C
    std::string     m_strFrom;          // +78  EventTime range
    std::string     m_strTo;            // +94
    std::string     m_strDesc;          // +B0
    BYTE            m_byLogType;        // +CC  0xFF all
    int             m_nMode;            // +D0  AQ_SERVER_EVENT_LOG_*, -1 none
};

C_ASSERT(FIELD_OFFSET(AQ_ServerEventLog, m_dwServerEventID) == 0x58);
C_ASSERT(FIELD_OFFSET(AQ_ServerEventLog, m_strQuery) == 0x5C);
C_ASSERT(FIELD_OFFSET(AQ_ServerEventLog, m_strDesc) == 0xB0);
C_ASSERT(FIELD_OFFSET(AQ_ServerEventLog, m_byLogType) == 0xCC);
C_ASSERT(FIELD_OFFSET(AQ_ServerEventLog, m_nMode) == 0xD0);
C_ASSERT(sizeof(AQ_ServerEventLog) == 0xD4);

// The shard DB table of _LogServerEvent (index in the table list).
enum
{
    SHARD_TABLE_LOG_SERVER_EVENT    = 5,
};
