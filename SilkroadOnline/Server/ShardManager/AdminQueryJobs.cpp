#include "ShardManager/AdminQueryJobs.h"

#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

// -------------------------------------------------------------------------------------------
// AQ_SiegeDataUpdater

AQ_SiegeDataUpdater::AQ_SiegeDataUpdater()
{
    m_byQueryTarget = 0;
    m_byIntroPermission = 0;
    m_dwFortressID = 0;
}

void AQ_SiegeDataUpdater::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_strDesc.clear();
    m_strIntroduction.clear();
    m_byQueryTarget = 0;
    m_dwFortressID = 0;
}

bool AQ_SiegeDataUpdater::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;

    try
    {
        m_strDesc.clear();
        *pMsg >> m_byQueryTarget;
        *pMsg >> m_dwFortressID;
        if (m_dwFortressID != (DWORD)m_Fortress.FortressID)
        {
            // The key setter of the record (bit 0 of 0x8A63CC is set once the record class is
            // initialised; otherwise it asserts and keeps the old key).
            m_Fortress.FortressID = m_dwFortressID;
        }

        if (m_byQueryTarget == 0)
        {
            *pMsg >> m_strIntroduction;
            std::string strFound;
            if (ShardDB_CheckSqlInjection(g_pShardDB, m_strIntroduction, strFound) == 1)
            {
                LogPrintf(0x2000001, "SMC AQ_SiegeDataUpdater QueryTarget[%d] Injection[%s]",
                          m_byQueryTarget, m_strIntroduction.c_str());
                return false;
            }
            // CInstanceSiegeFortress::SetIntroduction (0x4011b0) and its dirty bit
            m_Fortress.Introduction = std::string(m_strIntroduction);
            m_Fortress.m_qwDirty |= 0x80;
        }
        else if (m_byQueryTarget == 1)
        {
            *pMsg >> m_byIntroPermission;
            // CInstanceSiegeFortress::SetIntroModifyPermission (0x401260) and its dirty bit
            m_Fortress.IntroductionModificationPermission = m_byIntroPermission;
            m_Fortress.m_qwDirty |= 0x400;
        }
    }
    catch (...)
    {
        LogPrintf(0x2000001, "UpdateSiegeData Request Message is invalid");
        return false;
    }

    m_strDesc.assign("Siege Data", 10);
    if (m_byQueryTarget == 0)
        m_strDesc.append(" (Introduction)", 15);
    else if (m_byQueryTarget == 1)
        m_strDesc.append(" (Introduction Modification Permission)", 0x27);
    else
        m_strDesc.append(" (INVALID!!!)", 13);
    return true;
}

bool AQ_SiegeDataUpdater::Execute()
{
    BeginResponse(AQ_OPCODE_SIEGE_DATA_UPDATE);
    AQ_WriteString(m_pResponse, m_strDesc);
    if (!g_pShardDB)
    {
        m_byResult = AQ_RESULT_FAILED;
        return false;
    }
    if (m_byQueryTarget > 1)
    {
        m_byResult = AQ_RESULT_UNKNOWN_TARGET;
        LogPrintf(0x2000001, "Unknown UpdateSiegeData QueryTarget [%d]", m_byQueryTarget);
        return false;
    }
    return ExecuteAndReply();
}

// -------------------------------------------------------------------------------------------
// AQ_AssociationReputation

bool AQ_AssociationReputation::Execute()
{
    int nResult = 0;
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    if (!g_pShardDB)
        BSAssertFail();
    query.m_pSession = ShardDB_GetSession(g_pShardDB);
    query.m_pTrigger = NULL;
    query.m_pBuffer = (BYTE*)&nResult;
    query.m_nBufferSize = 4;
    query.m_nRowSize = 4;
    query.m_nSession = 0;
    // BUG(SM-300): the statement text is passed as the format string.
    return query.ExecuteFormat(m_strQuery.c_str()) >= 0;
}

BOOL DB_UpdateAssociationReputation(AQ_AssociationReputation* pJob, const int* pnAnubis,
                                    const int* pnHaroeris)
{
    if (!g_pShardMainProcess)
        BSAssertFail();
    CTask* pOwner = MainProcess_GetJobOwnerTask();
    if (!pOwner)
        BSAssertFail();
    pJob->Init(pOwner);
    pJob->m_strQuery.clear();

    char szQuery[0xC00];
    if (StringCchPrintfA(szQuery, 0xC00, "exec _UpdateAssociationReputation %d, %d, %d, %d",
                         pnAnubis[0], pnAnubis[1], pnHaroeris[0], pnHaroeris[1]) != S_OK)
    {
        BSAssertFail();
        return FALSE;
    }
    pJob->m_strQuery.assign(szQuery, strlen(szQuery));
    return TRUE;
}

// -------------------------------------------------------------------------------------------
// AQ_CharDataEditor

AQ_CharDataEditor::AQ_CharDataEditor()
{
    m_byMode = 0xFF;
    m_pEditor = NULL;
}

AQ_CharDataEditor::~AQ_CharDataEditor()
{
    if (m_pEditor)
        delete m_pEditor;
}

void AQ_CharDataEditor::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_byMode = 0xFF;
    if (m_pEditor)
    {
        delete m_pEditor;
        m_pEditor = NULL;
    }
}

bool AQ_CharDataEditor::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;

    CBaseCharDataEditor* pEditor;
    try
    {
        *pMsg >> m_byMode;
        if (m_byMode == 0)
            pEditor = new CCharDataSelector;
        else if (m_byMode == 1)
            pEditor = new CCharDataEditor;
        else
            throw 0;
    }
    catch (...)
    {
        pEditor = NULL;
    }

    m_pEditor = pEditor;
    if (!pEditor)
        BSAssertFail();
    // BUG(SM-301): an unknown mode asserts and then calls through the NULL editor.
    if (!m_pEditor->ReadRequest(pMsg, m_dwRequestID, &m_strOperator))
    {
        m_byResult = m_pEditor->m_byResult;
        return false;
    }
    return true;
}

bool AQ_CharDataEditor::Execute()
{
    BeginResponse(AQ_OPCODE_CHAR_DATA_EDIT);
    if (m_pEditor->Execute())
        return true;
    m_byResult = m_pEditor->m_byResult;
    return false;
}

bool AQ_CharDataEditor::OnComplete()
{
    if (!m_pEditor->BuildReply())
        return false;

    CMassiveMsg* pResponse = m_pResponse;
    AQ_WriteString(pResponse, m_pEditor->m_strMessage);
    CMassiveMsg* pReply = m_pEditor->m_pReply;
    BYTE byOK = 1;
    BYTE byFlag = m_pEditor->m_byQueryType;
    pResponse->Write(&byOK, 1);
    pResponse->Write(&m_byMode, 1);
    pResponse->Write(&byFlag, 1);
    m_pResponse->AppendFrom(pReply, FALSE);
    return AQ_Base::OnComplete();
}

bool AQ_CharDataEditor::OnFail()
{
    AQ_WriteString(m_pResponse, m_pEditor->m_strMessage);
    return AQ_Base::OnFail();
}

bool AQ_CharDataEditor::OnInitFailed(WORD wOpcode)
{
    BeginResponse(wOpcode);
    return OnFail();
}

// -------------------------------------------------------------------------------------------
// AQ_CharDataEditor2

AQ_CharDataEditor2::AQ_CharDataEditor2()
{
    m_byType = 0xFF;
    m_dwParam = 0;
    m_dwCharID = 0;
    m_dwSessionID = 0;
}

void AQ_CharDataEditor2::ReleaseId()
{
    COverlapJob_Local::ReleaseId();
    m_dwParam = 0;
    m_dwCharID = 0;
    m_dwSessionID = 0;
    m_byType = 0xFF;
    m_strNewName.clear();
    m_strOldName.clear();
    m_strQuery.clear();
}

void AQ_CharDataEditor2::Setup(const std::string& strNewName, CMsg* pMsg, DWORD dwParam,
                               BYTE byType, DWORD dwCharID, const std::string& strOldName)
{
    if (!g_pShardMainProcess)
        BSAssertFail();
    CTask* pOwner = MainProcess_GetJobOwnerTask();
    if (!pOwner)
        BSAssertFail();
    Init(pOwner);

    m_dwParam = dwParam;
    m_byType = byType;
    m_dwSessionID = pMsg->m_dwSessionID;
    m_dwCharID = dwCharID;
    m_strNewName.assign(strNewName, 0, std::string::npos);
    m_strOldName.assign(strOldName, 0, std::string::npos);
    m_strQuery.clear();

    char szQuery[0xC00];
    memset(szQuery, 0, sizeof(szQuery));
    if (m_byType == AQ_RENAME_CHAR)
    {
        StringCchPrintfA(szQuery, 0xC00, "{?=CALL _RenameCharNameByID (%d,'%s')}", m_dwCharID,
                         m_strNewName.c_str());
    }
    else if (m_byType == AQ_RENAME_GUILD)
    {
        StringCchPrintfA(szQuery, 0xC00, "{?=CALL _RenameGuildName ('%s','%s')}",
                         m_strOldName.c_str(), m_strNewName.c_str());
    }
    else
    {
        BSAssertFail();
    }
    if (lstrlenA(szQuery))
        m_strQuery.assign(szQuery, strlen(szQuery));
}

bool AQ_CharDataEditor2::Execute()
{
    if (m_strQuery.empty())
        return false;
    int anReturn[2] = { 0, 0 };
    if (!g_pShardDB)
        BSAssertFail();
    int nResult = CDatabase::ExecDirectWithReturnValue(ShardDB_GetSession(g_pShardDB), 5,
                                                       m_strQuery.c_str(), 3, anReturn);
    return (nResult & ~1) == 0 && anReturn[0] >= 0;
}

bool AQ_CharDataEditor2::OnComplete()
{
    if (m_byType != AQ_RENAME_CHAR && m_byType != AQ_RENAME_GUILD)
        BSAssertFail();
    if (!g_pShardMainProcess)
        BSAssertFail();
    ShardIntegrate_OnQueryResult(this, TRUE);
    return true;
}

bool AQ_CharDataEditor2::OnFail()
{
    if (m_byType != AQ_RENAME_CHAR && m_byType != AQ_RENAME_GUILD)
        BSAssertFail();
    if (!g_pShardMainProcess)
        BSAssertFail();
    ShardIntegrate_OnQueryResult(this, FALSE);
    return true;
}

// -------------------------------------------------------------------------------------------
// CCharDataEditor checks

bool CharDataEditor_CheckCharMgrReady(CBaseCharDataEditor* pEditor)
{
    if (CharMgr_IsReady())
        return true;
    pEditor->m_byResult = 14;
    return false;
}

bool CharDataEditor_CheckGameServersStopped(CBaseCharDataEditor* pEditor)
{
    if (!CharDataEditor_CheckCharMgrReady(pEditor))
        return false;

    // The body of this server (g_pSelfBody 0x8588B4) and every body (GetGlobal_8588D0).
    if (g_pSelfBody->nState != 4)
    {
        pEditor->m_byResult = 15;
        return false;
    }

    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody)
            continue;
        if (pBody->pModule != CertModule_FindByName("SR_GameServer"))
            continue;
        int nState = pBody->nState;
        if (nState && nState != 4 && nState != 5)
        {
            pEditor->m_byResult = 15;
            return false;
        }
    }
    return true;
}
