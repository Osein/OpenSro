#pragma once

#include <windows.h>
#include <string>

#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"

// Provided by SM_admin3 (CBaseCharDataEditor 0x486850, CCharDataSelector 0x4869b0,
// CCharDataEditor 0x488180). Only the members AQ_CharDataEditor uses; replace this header with
// the real classes.
class CBaseCharDataEditor
{
public:
    CBaseCharDataEditor();
    virtual ~CBaseCharDataEditor();                                                     // 0
    // Reads the editor request after the AQ header (request ID and operator of the job).
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator) = 0; // 1
    virtual void Reset() = 0;                                                           // 2
    virtual bool Execute() = 0;                                                         // 3
    virtual bool BuildReply() = 0;                                                      // 4

public:
    DWORD                           m_dw04;         // +04
    std::string                     m_str08;        // +08
    BYTE                            m_byResult;     // +24
    BYTE                            m_byReplyFlag;  // +25
    ServerFramework::CMassiveMsg*   m_pReply;       // +28
    std::string                     m_str2C;        // +2C
    std::string                     m_strMessage;   // +48
};

C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_byResult) == 0x24);
C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_pReply) == 0x28);
C_ASSERT(FIELD_OFFSET(CBaseCharDataEditor, m_strMessage) == 0x48);
C_ASSERT(sizeof(CBaseCharDataEditor) == 0x64);

class CCharDataSelector : public CBaseCharDataEditor
{
public:
    CCharDataSelector();
    virtual ~CCharDataSelector();
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator);
    virtual void Reset();
    virtual bool Execute();
    virtual bool BuildReply();
};

class CCharDataEditor : public CBaseCharDataEditor
{
public:
    CCharDataEditor();
    virtual ~CCharDataEditor();
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator);
    virtual void Reset();
    virtual bool Execute();
    virtual bool BuildReply();

    BYTE m_abData[0x120 - 0x64];
};

C_ASSERT(sizeof(CCharDataEditor) == 0x120);
