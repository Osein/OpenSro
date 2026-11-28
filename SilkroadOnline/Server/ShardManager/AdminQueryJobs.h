#pragma once

#include "ServerCommon/InstanceRecords.h"
#include "ShardManager/AQBase.h"
#include "ShardManager/CharDataEditor.h"

// Admin query jobs of the SMC console that change shard data.

enum
{
    AQ_OPCODE_CHAR_DATA_SELECT  = 0x8002,
    AQ_OPCODE_CHAR_DATA_EDIT    = 0x8007,
    AQ_OPCODE_SIEGE_DATA_UPDATE = 0x801F,
};

// AQ_SiegeDataUpdater (vftable 0x7807ac, 0x118 bytes): changes the introduction text (target 0)
// or the introduction modification permission (target 1) of a fortress.
class AQ_SiegeDataUpdater : public CAQJobT<AQ_SiegeDataUpdater>
{
public:
    AQ_SiegeDataUpdater();                                              // 0x401450
    virtual ~AQ_SiegeDataUpdater() {}                                   // 0 (0x4015f0, 0x4024c0)

    virtual void ReleaseId();                                           // 2 (0x401900)
    virtual bool Execute();                                             // 6 (0x401930)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x4016e0)

    // Provided by SM_world1 (0x401990): updates the record, writes the answer and notifies the
    // game servers (0x3542).
    bool ExecuteAndReply();

public:
    BYTE                    m_byQueryTarget;        // +50  0 introduction, 1 permission
    BYTE                    m_byIntroPermission;    // +51
    DWORD                   m_dwFortressID;         // +54
    std::string             m_strDesc;              // +58  console description of the job
    std::string             m_strIntroduction;      // +74
    std::string             m_str90;                // +90
    CInstanceSiegeFortress  m_Fortress;             // +B0
};

C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_byQueryTarget) == 0x50);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_dwFortressID) == 0x54);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_strDesc) == 0x58);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_strIntroduction) == 0x74);
C_ASSERT(FIELD_OFFSET(AQ_SiegeDataUpdater, m_Fortress) == 0xB0);
C_ASSERT(sizeof(AQ_SiegeDataUpdater) == 0x118);

// AQ_AssociationReputation (0x6C bytes): runs a statement text on the shard database. The
// request reader is the inherited AQ_Base one; the text is filled by the owner.
class AQ_AssociationReputation : public CAQJobT<AQ_AssociationReputation>
{
public:
    AQ_AssociationReputation() {}                                       // 0x4654a0
    virtual ~AQ_AssociationReputation() {}                              // 0 (0x465510, 0x465ea0)

    virtual bool Execute();                                             // 6 (0x465690)

public:
    std::string     m_strQuery;                                         // +50
};

C_ASSERT(FIELD_OFFSET(AQ_AssociationReputation, m_strQuery) == 0x50);
C_ASSERT(sizeof(AQ_AssociationReputation) == 0x6C);

// DB_UpdateAssociationReputation (0x465590, CAssociationReputationMgr::QueueDBUpdate): Init on
// the job owner task of the main process and "exec _UpdateAssociationReputation %d, %d, %d, %d"
// with the two pairs {hunter, robber} into m_strQuery. FALSE (asserted) when the text does not
// fit.
BOOL DB_UpdateAssociationReputation(AQ_AssociationReputation* pJob, const int* pnAnubis,
                                    const int* pnHaroeris);

// AQ_CharDataEditor (0x58 bytes): mode 0 selects character data (CCharDataSelector), mode 1
// edits it (CCharDataEditor); the editor object does the work.
class AQ_CharDataEditor : public CAQJobT<AQ_CharDataEditor>
{
public:
    AQ_CharDataEditor();                                                // 0x466560
    virtual ~AQ_CharDataEditor();                                       // 0 (0x4665c0, 0x467c00)

    virtual void ReleaseId();                                           // 2 (0x466780)
    virtual bool Execute();                                             // 6 (0x4667b0)
    virtual bool OnComplete();                                          // 7 (0x4667e0)
    virtual bool OnFail();                                              // 8 (0x4668c0)
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9 (0x466630)
    virtual bool OnInitFailed(WORD wOpcode);                            // 10 (0x4668a0)

public:
    BYTE                    m_byMode;       // +50  0xFF none
    CBaseCharDataEditor*    m_pEditor;      // +54
};

C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor, m_byMode) == 0x50);
C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor, m_pEditor) == 0x54);
C_ASSERT(sizeof(AQ_CharDataEditor) == 0x58);

enum
{
    AQ_RENAME_CHAR  = 1,
    AQ_RENAME_GUILD = 2,
};

// AQ_CharDataEditor2 (0x84 bytes): renames a character or a guild for the shard integration
// (CMainProcess::OnMsg_7450); the result goes back to the main process.
class AQ_CharDataEditor2 : public CAQJobT<AQ_CharDataEditor2, ServerFramework::COverlapJob_Local>
{
public:
    AQ_CharDataEditor2();                                               // 0x466960
    virtual ~AQ_CharDataEditor2() {}                                    // 0 (0x466a10, 0x467c60)

    virtual void ReleaseId();                                           // 2 (0x466c60)
    virtual bool Execute();                                             // 6 (0x466ca0)
    virtual bool OnComplete();                                          // 7 (0x466d10)
    virtual bool OnFail();                                              // 8 (0x466d40)

    // AQ_CharDataEditor2::Init (0x466ae0): the request and the statement text. TRUE when a
    // statement was built (FALSE for an unknown type, asserted).
    BOOL Init(const std::string& strNewName, CMsg* pMsg, DWORD dwParam, BYTE byType,
              DWORD dwCharID, const std::string& strOldName);

public:
    BYTE            m_byType;       // +20  AQ_RENAME_*, 0xFF none
    DWORD           m_dwParam;      // +24
    DWORD           m_dwCharID;     // +28
    DWORD           m_dwSessionID;  // +2C
    std::string     m_strOldName;   // +30  guild name (type 2)
    std::string     m_strNewName;   // +4C
    std::string     m_strQuery;     // +68
};

C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor2, m_byType) == 0x20);
C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor2, m_strOldName) == 0x30);
C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor2, m_strNewName) == 0x4C);
C_ASSERT(FIELD_OFFSET(AQ_CharDataEditor2, m_strQuery) == 0x68);
C_ASSERT(sizeof(AQ_CharDataEditor2) == 0x84);

// Checks of CCharDataEditor before an edit (result codes in the editor).
// 0x466430: result 14 while the character data is not loaded.
bool CharDataEditor_CheckCharMgrReady(CBaseCharDataEditor* pEditor);
// 0x466470: result 15 unless this server is in state 4 and every SR_GameServer of the shard
// is stopped (state 0, 4 or 5).
bool CharDataEditor_CheckGameServersStopped(CBaseCharDataEditor* pEditor);
