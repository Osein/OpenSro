#pragma once

#include "BSLib/DB/Database.h"
#include "ShardManager/AQBase.h"

// Siege fortress queries of the SMC console: the fortress data (answer 0x8010) and the siege
// log (_LOGEVENTSIEGEFORTRESS, answer 0x800E).

enum
{
    AQ_OPCODE_SIEGE_LOG_SELECT  = 0x800E,
    AQ_OPCODE_SIEGE_DATA_SELECT = 0x8010,
};

// The shard DB table of _LOGEVENTSIEGEFORTRESS (index in the table list).
enum
{
    SHARD_TABLE_LOG_SIEGE_FORTRESS  = 3,
};

// AQ_SiegeDataSelector (0x74 bytes): target 1 is a fortress with its structures, the members
// of the owner guild and the siege requests.
class AQ_SiegeDataSelector : public CAQJobT<AQ_SiegeDataSelector>
{
public:
    AQ_SiegeDataSelector() {}                                           // 0x47db50
    virtual ~AQ_SiegeDataSelector() {}                                  // 0 (0x47dbc0, 0x47ec50)

    virtual void ReleaseId();                                           // 2 (0x47dd60)
    virtual bool Execute();                                             // 6 (0x47dd80)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x47dc40)

    // 0x47dfd0: the owner guild name ("Not occupied yet"), tax ratio, tax, introduction and
    // its permission of the fortress, the members with siege authority, the structures, the
    // NPC hire flag and the requesting guilds.
    static void WriteFortressData(CDBRecordList* pFortresses, CDBRecordList* pRequests,
                                  CDBRecordList* pStructs, CDBRecordList* pMembers,
                                  ServerFramework::CMassiveMsg* pMsg);

public:
    BYTE            m_byQueryTarget;    // +50  1 fortress, 0xFF none
    DWORD           m_dwFortressID;     // +54
    std::string     m_strDesc;          // +58
};

C_ASSERT(FIELD_OFFSET(AQ_SiegeDataSelector, m_dwFortressID) == 0x54);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataSelector, m_strDesc) == 0x58);
C_ASSERT(sizeof(AQ_SiegeDataSelector) == 0x74);

// AQ_SiegeLogSelector (0xB0 bytes).
class AQ_SiegeLogSelector : public CAQJobT<AQ_SiegeLogSelector>
{
public:
    AQ_SiegeLogSelector() {}                                            // 0x47f1d0
    virtual ~AQ_SiegeLogSelector() {}                                   // 0 (0x47f260, 0x4803c0)

    virtual bool Execute();                                             // 6 (0x47f430)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x47f340)

    // 0x47f730: per row {FortressID, EventTime, BYTE EventID, CharID, Data1, Data2, strDesc},
    // then the time of the last row as text.
    static void WriteLogRows(ServerFramework::CMassiveMsg* pMsg, CDBRecordList* pList);
    // 0x47f8d0: " [SiegeFortress] (<event type>)", appended to m_strDesc.
    void BuildDescription();
    // 0x47faf0: the EventID condition of the event type.
    void BuildEventFilter(std::string* pstrFilter);

public:
    BYTE            m_byEventType;      // +50  250 all
    DWORD           m_dwFortressID;     // +54
    std::string     m_strFrom;          // +58  EventTime range
    std::string     m_strTo;            // +74
    DWORD           m_dw90;             // +90
    std::string     m_strDesc;          // +94
};

C_ASSERT(FIELD_OFFSET(AQ_SiegeLogSelector, m_strFrom) == 0x58);
C_ASSERT(FIELD_OFFSET(AQ_SiegeLogSelector, m_strDesc) == 0x94);
C_ASSERT(sizeof(AQ_SiegeLogSelector) == 0xB0);
