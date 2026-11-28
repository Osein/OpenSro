#pragma once

#include <windows.h>
#include <string>

#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"

// The worker objects of AQ_CharDataEditor: mode 0 selects character data (CCharDataSelector),
// mode 1 edits it (CCharDataEditor). ReadRequest formats one statement from the request,
// Execute runs it and copies the result rows into m_pReply, BuildReply does the follow-up work
// after a successful edit.

// CBaseCharDataEditor (vftable 0x78ac2c, 0x64 bytes, every slot but the destructor pure).
class CBaseCharDataEditor
{
public:
    CBaseCharDataEditor();                                                              // 0x486850
    virtual ~CBaseCharDataEditor();                                     // 0 (0x4868d0, 0x486990)
    // Reads the editor request after the AQ header (request ID and operator of the job).
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator) = 0; // 1
    virtual void Reset() = 0;                                                           // 2
    virtual bool Execute() = 0;                                                         // 3
    virtual bool BuildReply() = 0;                                                      // 4

public:
    DWORD                           m_dwRequestID;  // +04
    std::string                     m_strOperator;  // +08
    BYTE                            m_byResult;     // +24  0xFF none
    BYTE                            m_byQueryType;  // +25  written to the console answer
    ServerFramework::CMassiveMsg*   m_pReply;       // +28
    std::string                     m_strQuery;     // +2C
    std::string                     m_strMessage;   // +48  console description / log
};

C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_byResult) == 0x24);
C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_pReply) == 0x28);
C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_strQuery) == 0x2C);
C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_strMessage) == 0x48);
C_ASSERT(sizeof(CBaseCharDataEditor) == 0x64);

// CCharDataSelector (vftable 0x78ac44): read-only queries (position, attributes, guild, items,
// trade jobs, training camp, timed jobs, collection book) of a character.
class CCharDataSelector : public CBaseCharDataEditor
{
public:
    CCharDataSelector();                                                                // 0x4869b0
    virtual ~CCharDataSelector();                                       // 0 (0x486a10)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator); // 1 (0x486a90)
    virtual void Reset();                                                               // 2 (0x487da0)
    virtual bool Execute();                                                             // 3 (0x487dd0)
    // 0x69bb10: nothing to do after a selection.
    virtual bool BuildReply();                                                          // 4

    // CCharDataSelector_TriggerWriteRow (0x488100): {BYTE 1, row} per row, no header.
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);
};

// CCharDataEditor (vftable 0x78ac5c, 0x120 bytes): changes of character data. The request
// reader (0x4883e0) fills the edit fields and the statement.
class CCharDataEditor : public CBaseCharDataEditor
{
public:
    CCharDataEditor();                                                                  // 0x488180
    virtual ~CCharDataEditor();                                         // 0 (0x488280, 0x488260)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator); // 1 (0x4883e0)
    virtual void Reset();                                                               // 2 (0x48bbf0)
    virtual bool Execute();                                                             // 3 (0x48bc80)
    virtual bool BuildReply();                                                          // 4 (0x48bec0)

    // 0x48c070: row callback of Execute; an item creation (m_byItemOp 2) answers
    // {int error, ID, serial} and the description gets the new item or the error code.
    int OnQueryRow(DWORD dwQuery, DWORD dwUnused);

    // 0x48b930: the log code of the edit type ("MCP", "MCN", ...; empty for the others) and of
    // the item operation ("_D", "_A", "_M", "_G") in m_strEditType.
    const char* GetEditTypeDesc();

public:
    BYTE                m_byItemOp;     // +64  item / timed job operation (1 delete, 2 add,
                                        //      3 edit, 4 gold); 2: the statement creates an item
    DWORD               m_dwRefItemID;  // +68  item of an item edit (_ADDLOGITEM)
    DWORD               m_dw6C;         // +6C
    unsigned __int64    m_qwItemID;     // +70
    unsigned __int64    m_qwSerial;     // +78
    BYTE                m_bySlot;       // +80
    DWORD               m_dw84;         // +84
    DWORD               m_dwJID;        // +88
    DWORD               m_dw8C;         // +8C  second DWORD of a rename request (unused)
    std::string         m_strOldName;   // +90  character rename
    std::string         m_strNewName;   // +AC
    std::string         m_strEditType;  // +C8  log code of the edit (GetEditTypeDesc)
    std::string         m_strLogQuery;  // +E4  _ADDLOGCHAR / _ADDLOGITEM statement
    std::string         m_strEditDesc;  // +100 arguments of the edit for the SMC log
    DWORD               m_dw11C;        // +11C
};

C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_byItemOp) == 0x64);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_qwItemID) == 0x70);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_bySlot) == 0x80);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_dwJID) == 0x88);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_strOldName) == 0x90);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_strNewName) == 0xAC);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_strLogQuery) == 0xE4);
C_ASSERT(FIELD_OFFSET(CCharDataEditor, m_strEditDesc) == 0x100);
C_ASSERT(sizeof(CCharDataEditor) == 0x120);

// BuildSMCLog (0x48bb10, CCharDataEditor::Execute): the _ADDLOGCHAR (character edits) or
// _ADDLOGITEM (item edits 9, 10, 11, 19) statement of an edit into pszOut (0x80 bytes); edit
// types 5, 8 and > 20 leave pszOut unchanged. A failure is only logged (the return register
// of the original holds no result).
void BuildSMCLog(const char* pszDesc, CCharDataEditor* pEditor, char* pszOut);
// 0x4663e0 (CBaseCharDataEditor_Helper_4663E0): result 14 and "User In Game!! - CharEdit"
// when the character is in game.
bool CharDataEditor_CheckNotInGame(CBaseCharDataEditor* pEditor, const std::string& strName);
