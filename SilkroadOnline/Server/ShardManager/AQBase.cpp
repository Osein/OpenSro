#include "ShardManager/AQBase.h"

#include "BSLib/BSDebug.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

AQ_Base::AQ_Base()
{
    m_pResponse = NULL;
    m_byResult = AQ_RESULT_FAILED;
    m_dwSessionID = 0;
    m_dwRelayID = 0;
    m_dwRequestID = 0;
    m_strOperator.clear();
}

AQ_Base::~AQ_Base()
{
    if (m_pResponse)
    {
        m_pResponse->Free();
        m_pResponse = NULL;
    }
}

void AQ_Base::ReleaseId()
{
    COverlapJob_Local::ReleaseId();
    m_byResult = AQ_RESULT_FAILED;
    m_dwSessionID = 0;
    m_dwRelayID = 0;
    if (m_pResponse)
    {
        m_pResponse->Free();
        m_pResponse = NULL;
    }
}

bool AQ_Base::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!g_pShardMainProcess)
        BSAssertFail();
    CTask* pOwner = MainProcess_GetJobOwnerTask();
    if (!pOwner)
        BSAssertFail();
    Init(pOwner);

    m_dwSessionID = pMsg->m_dwSessionID;
    m_dwRelayID = dwRelayID;
    if (!m_dwSessionID)
    {
        m_byResult = AQ_RESULT_NO_SESSION;
        return false;
    }
    if (!dwRelayID)
    {
        m_byResult = AQ_RESULT_NO_RELAY;
        return false;
    }
    *pMsg >> m_dwRequestID;
    *pMsg >> m_strOperator;
    return true;
}

void AQ_Base::BeginResponse(WORD wOpcode)
{
    if (m_pResponse)
    {
        BSAssertFail();
        m_pResponse->Free();
    }
    m_pResponse = CMassiveMsg::Alloc(ServerFramework::MASSIVE_MODE_WRITE);
    m_pResponse->SetOpcode(wOpcode);
}

bool AQ_Base::OnComplete()
{
    if (!m_pResponse)
    {
        BSAssertFail();
        return true;
    }
    if (!g_pShardMainProcess)
        BSAssertFail();
    g_pShardMainProcess->SendRelayReplyMassive(m_dwSessionID, m_dwRelayID, m_pResponse);
    m_pResponse = NULL;
    return true;
}

bool AQ_Base::OnFail()
{
    CMassiveMsg* pResponse = m_pResponse;
    if (!pResponse)
    {
        BSAssertFail();
        return true;
    }
    BYTE byFailed = 2;
    pResponse->Write(&byFailed, 1);
    pResponse->Write(&m_byResult, 1);
    if (!g_pShardMainProcess)
        BSAssertFail();
    g_pShardMainProcess->SendRelayReplyMassive(m_dwSessionID, m_dwRelayID, m_pResponse);
    m_pResponse = NULL;
    return true;
}

bool AQ_Base::OnInitFailed(WORD wOpcode)
{
    BeginResponse(wOpcode);
    std::string strText;
    strText.assign("Failed to Init", 14);
    AQ_WriteString(m_pResponse, strText);
    return OnFail();
}
