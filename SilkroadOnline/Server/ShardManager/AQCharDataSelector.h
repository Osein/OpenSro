#pragma once

#include "ShardManager/AQBase.h"

// AQ_CharDataSelector (0xF8 bytes): the character data queries of the SMC console (answer
// 0x8002). The query target selects what is read; the name is a character or guild name.
class AQ_CharDataSelector : public CAQJobT<AQ_CharDataSelector>
{
public:
    AQ_CharDataSelector();                                              // 0x468660
    virtual ~AQ_CharDataSelector() {}                                   // 0 (0x468720, 0x46e960)

    virtual void ReleaseId();                                           // 2 (0x468860)
    virtual bool Execute();                                             // 6 (0x468aa0)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x468880)

    // 0x46bbb0: "Querying Data of Character (<target>) : [ CharName <name> ]".
    void BuildDescription();

    // Pending in SM_admin1 (record layouts of the loaders of SM_admin3).
    bool QueryCharInfo();               // 0x469c50, target 1
    bool QueryItems();                  // 0x46a010, targets 2, 3, 4, 7, 11, 14, 16
    bool QuerySkills();                 // 0x46a5c0, target 6
    bool QueryGuild();                  // 0x46b830, target 8
    bool QueryTrainingCamp();           // 0x46ba20, target 12
    // Provided by SM_admin2.
    bool QueryQuests();                 // 0x46be50, target 5
    bool QueryPetDetail();              // 0x46c180, targets 9, 10
    bool QueryItemDetail();             // 0x46cce0, target 18
    bool QueryItemExtra();              // 0x46ce40, target 20
    bool QueryCollectionBook();         // 0x46d0a0, target 21
    bool QueryOpenMarketByChar();       // 0x46d530, target 22
    bool QueryOpenMarketByDate();       // 0x46d680, target 23

public:
    BYTE                m_byQueryTarget;    // +50
    DWORD               m_dwTargetID;       // +54  targets 7, 10, 11
    std::string         m_strName;          // +58
    std::string         m_strDesc;          // +74
    unsigned __int64    m_qwItemSerial;     // +90  targets 16, 18, 20
    DWORD               m_dw98;             // +98  target 21
    DWORD               m_dw9C;             // +9C  target 23
    BYTE                m_byA0;             // +A0  target 23
    BYTE                m_byA1;             // +A1  target 23
    std::string         m_strA4;            // +A4
    std::string         m_strC0;            // +C0  target 23
    std::string         m_strDC;            // +DC  target 23
};

C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_strName) == 0x58);
C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_strDesc) == 0x74);
C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_qwItemSerial) == 0x90);
C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_dw9C) == 0x9C);
C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_strA4) == 0xA4);
C_ASSERT(FIELD_OFFSET(AQ_CharDataSelector, m_strDC) == 0xDC);
C_ASSERT(sizeof(AQ_CharDataSelector) == 0xF8);
