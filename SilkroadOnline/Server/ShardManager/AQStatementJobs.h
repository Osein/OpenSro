#pragma once

#include "ShardManager/AQBase.h"

// Statement jobs of the SMC console: ReadRequest formats one SQL statement from the request,
// Execute runs it with a row trigger that copies every result row as raw bytes into the
// response ({BYTE 1, row} per row, a job specific header before the first row, BYTE 0 at the
// end). The row sizes are the fixed column layouts the console expects.

enum
{
    AQ_OPCODE_STATISTICS        = 0x8005,
    AQ_OPCODE_USER_DATA_SELECT  = 0x8008,
    AQ_OPCODE_TRIJOB_RANKING    = 0x800C,
};

// The shared shape (0x8C bytes): query target, statement and console description.
template <class T>
class CAQStatementJobT : public CAQJobT<T>
{
public:
    CAQStatementJobT() {}
    virtual ~CAQStatementJobT() {}

    // AQ_Reset_ClearString54 (0x482820, AQ_Statistics and AQ_TrijobRanking).
    virtual void ReleaseId()                                            // 2
    {
        AQ_Base::ReleaseId();
        m_byQueryTarget = 0xFF;
        m_strQuery.clear();
    }

    // Runs m_strQuery on connection 4 with the row trigger T::OnQueryRow; rows of nRowSize
    // bytes go to pRow (no buffer when pRow is NULL). The ExecuteFormat result.
    int RunStatement(AQ_DBQuery& query, void* pRow, int nRowSize);

public:
    BYTE            m_byQueryTarget;    // +50
    BYTE            m_bySubTarget;      // +51  AQ_TrijobRanking
    std::string     m_strQuery;         // +54
    std::string     m_strDesc;          // +70
};

// AQ_Statistics (vftable 0x7881ec): statistic statements (answer 0x8005).
class AQ_Statistics : public CAQStatementJobT<AQ_Statistics>
{
public:
    AQ_Statistics() {}                                                  // 0x480940
    virtual ~AQ_Statistics() {}                                         // 0 (0x4809c0, 0x481e30)

    virtual bool Execute();                                             // 6 (0x481080)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x480a70)

    // 0x481470: "Querying Statistics (<target>)".
    void BuildDescription();
    // AQ_Statistics_TriggerWriteRow (0x4813c0).
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
    // Before the first row: {BYTE 1, BYTE target}.
    void WriteRowHeader(ServerFramework::CMassiveMsg* pMsg);
};

C_ASSERT(FIELD_OFFSET(AQ_Statistics, m_byQueryTarget) == 0x50);
C_ASSERT(FIELD_OFFSET(AQ_Statistics, m_strQuery) == 0x54);
C_ASSERT(FIELD_OFFSET(AQ_Statistics, m_strDesc) == 0x70);
C_ASSERT(sizeof(AQ_Statistics) == 0x8C);

// AQ_TrijobRanking (vftable 0x788328): the ranking tables of a trade job (answer 0x800C).
class AQ_TrijobRanking : public CAQStatementJobT<AQ_TrijobRanking>
{
public:
    AQ_TrijobRanking() {}                                               // 0x4823b0
    virtual ~AQ_TrijobRanking() {}                                      // 0 (0x482430, 0x483360)

    virtual bool Execute();                                             // 6 (0x482840)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x4824e0)

    // 0x482a70: "Querying Trijob Ranking (<job>) (<kind>)".
    void BuildDescription();
    // AQ_TrijobRanking_TriggerWriteRow (0x4829b0).
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
    // Before the first row: {BYTE 1, BYTE job, BYTE kind}.
    void WriteRowHeader(ServerFramework::CMassiveMsg* pMsg);
};

C_ASSERT(sizeof(AQ_TrijobRanking) == 0x8C);

// AQ_UserDataSelector (vftable 0x788468): the characters of an account (answer 0x8008).
class AQ_UserDataSelector : public CAQStatementJobT<AQ_UserDataSelector>
{
public:
    AQ_UserDataSelector() {}                                            // 0x4838e0
    virtual ~AQ_UserDataSelector() {}                                   // 0 (0x483960, 0x484730)

    virtual void ReleaseId();                                           // 2 (0x483c70)
    virtual bool Execute();                                             // 6 (0x483c90)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x483a10)

    // AQ_UserDataSelector_TriggerWriteRow (0x483e10).
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
    // Before the first row: {BYTE 1, BYTE target, WORD shard ID}.
    void WriteRowHeader(ServerFramework::CMassiveMsg* pMsg);
};

C_ASSERT(sizeof(AQ_UserDataSelector) == 0x8C);
