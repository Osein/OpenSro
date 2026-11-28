#pragma once

#include "BSLib/DB/Database.h"
#include "ShardManager/AQBase.h"

// Log queries of the SMC console (answer 0x8001): the character log (_LOGEVENTCHAR), the item
// log (_LOGEVENTITEM), the quest rows of the character log and the history of one item serial.

enum
{
    AQ_OPCODE_LOG_SELECT    = 0x8001,
};

// Log types of AQ_LogSelector.
enum
{
    AQ_LOG_CHAR             = 1,
    AQ_LOG_ITEM             = 2,
    AQ_LOG_QUEST            = 3,
    AQ_LOG_ITEM_SERIAL      = 4,
};

// AQ_LogSelector (0xE4 bytes).
class AQ_LogSelector : public CAQJobT<AQ_LogSelector>
{
public:
    AQ_LogSelector() {}                                                 // 0x46fd80
    virtual ~AQ_LogSelector() {}                                        // 0 (0x46fe40, 0x471b60)

    virtual void ReleaseId();                                           // 2 (0x46ff80)
    virtual bool Execute();                                             // 6 (0x470130)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x46ffd0)

    // 0x470100: the server-wide sub types (character 9, item 17) and the serial history are
    // selected without a character.
    BOOL IsCharIdNotRequired() const;
    // 0x470680 / 0x470870: the event filter after the WHERE clause of the sub type
    // (" ORDER BY EVENTTIME" alone for a sub type without one).
    void BuildCharEventFilter(std::string* pstrFilter);
    void BuildItemEventFilter(std::string* pstrFilter);
    // 0x470d30: the selected rows into pMsg, then the time of the last row as text.
    void WriteLogRows(ServerFramework::CMassiveMsg* pMsg, CDBRecordList* pList);
    // 0x470f80: "Querying Log [<type>] (<sub type>) : [ CharName <name> ]".
    void BuildDescription();

public:
    BYTE            m_byLogType;        // +50  AQ_LOG_*, 0xFF none
    BYTE            m_bySubType;        // +51  0xFF all
    std::string     m_strCharName;      // +54
    std::string     m_strFrom;          // +70  EventTime range
    std::string     m_strTo;            // +8C
    std::string     m_strFilterText;    // +A8  serial (type 4), item code name (item 16)
    DWORD           m_dwTop;            // +C4  type 4: row limit, 0 all
    std::string     m_strDesc;          // +C8
};

C_ASSERT(FIELD_OFFSET(AQ_LogSelector, m_strCharName) == 0x54);
C_ASSERT(FIELD_OFFSET(AQ_LogSelector, m_strFilterText) == 0xA8);
C_ASSERT(FIELD_OFFSET(AQ_LogSelector, m_dwTop) == 0xC4);
C_ASSERT(FIELD_OFFSET(AQ_LogSelector, m_strDesc) == 0xC8);
C_ASSERT(sizeof(AQ_LogSelector) == 0xE4);

// The shard DB tables of the log rows (index in the table list).
enum
{
    SHARD_TABLE_LOG_CHAR    = 0,
    SHARD_TABLE_LOG_ITEM    = 1,
};

// Writes a text as {WORD length, chars} (lstrlenA).
void AQ_WriteText(ServerFramework::CMassiveMsg* pMsg, const char* psz);
// "%d-%d-%d %d:%d:%d.%d" of a timestamp (fraction in milliseconds) as {WORD length, chars}.
void AQ_WriteTimeText(ServerFramework::CMassiveMsg* pMsg, const TIMESTAMP_STRUCT& time);
