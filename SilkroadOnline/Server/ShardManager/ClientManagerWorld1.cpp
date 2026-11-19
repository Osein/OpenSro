#include "ShardManager/ClientManagerWorld1.h"

#include <list>
#include <map>
#include <vector>

#include "BSNet/Msg.h"
#include "BSNet/NetEngine.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/ShardScheduleExtern.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    MSG_SHARD_GS_LOGOUT_USER = 0x7804,      // to the game server: DWORD JID
    MSG_SHARD_GS_SWITCH_SESSION = 0x7803,   // to the game server: DWORD JID, WORD play time
    MSG_SHARD_AGENT_KICK = 0x2206,          // to the agent: WORD count, DWORD client sessions
    MSG_SHARD_PLAY_TIME = 0x3122,           // to the game server: DWORD JID, WORD play time
    MSG_SHARD_START_PLAY_TIME = 0x2320,     // to the global manager: DWORD JID

    // The agent sessions of one 0x2206 message.
    SHARD_KICK_PER_MSG = 500,
};

// ---------------------------------------------------------------------------------------------
// GameWorldContext, sUserLoginInfo

void GameWorldContext_Reset(GameWorldContext* pContext)
{
    pContext->fZ = 0.0f;
    pContext->wWorldID = 1;
    pContext->wLayerID = 1;
    pContext->fY = 0.0f;
    pContext->fX = 0.0f;
    pContext->wRegionID = 0;
    pContext->dwCreateTime = 0;
    pContext->dw24 = 0;
}

void GameWorldContext_SetFromCharPos(GameWorldContext* pContext,
                                     const GameWorldCharRecordPos* pChar)
{
    pContext->wWorldID = pChar->wWorldID;
    pContext->wLayerID = pChar->wLayerID;
    pContext->wRegionID = pChar->wRegionID;
    pContext->fX = (float)pChar->nPosX;
    pContext->fY = (float)pChar->nPosY;
    pContext->fZ = (float)pChar->nPosZ;
    RefObj_PackTypeID(pContext->dwCreateTime, pChar->awCreateTime);
}

BOOL sUserLoginInfo_BindServerBody(sUserLoginInfo* pInfo, WORD wBodyID)
{
    if (!wBodyID)
        return FALSE;
    DWORD dwOldSession = pInfo->dwGameServerSession;
    pInfo->wGameServerBodyID = wBodyID;
    pInfo->dw150 = dwOldSession;
    pInfo->dwGameServerSession = MainProcess_GetBodySession(wBodyID);
    return pInfo->dwGameServerSession != 0;
}

// ---------------------------------------------------------------------------------------------
// CUser

int CUser::GetStateTimeoutMs() const
{
    WORD wStep = HIWORD(m_dwState);
    switch (LOWORD(m_dwState))
    {
    case 0:
        switch (wStep)
        {
        case 0:
        case 6:
        case 8:
            return -1;
        case 5:
            return 180000;
        }
        break;
    case 1:
        switch (wStep)
        {
        case 1:
            return 180000;
        case 2:
            return 420000;
        case 4:
            return 60000;
        }
        return -1;
    case 2:
        return -1;
    case 3:
        if (wStep == 3)
            return 180000;
        break;
    }
    BSAssertFail();
    return 0;
}

// The meter rate level of a play time: 0 up to the first threshold (hours), 1 up to the second,
// 2 above.
static int User_GetMeterRateLevel(WORD wPlayTime)
{
    const BYTE* pbyHours = MeterRateConfig_GetStatic();
    if ((int)wPlayTime <= 3600 * pbyHours[0])
        return 0;
    pbyHours = MeterRateConfig_GetStatic();
    return (int)wPlayTime > 3600 * pbyHours[1] ? 2 : 1;
}

void CUser::AddPlayTime(BYTE bySeconds)
{
    if (GetState() != USER_STATE_IN_GAME_WORLD1)
        return;
    WORD& wPlayTime = m_LoginInfo.w176;
    int nLevel = User_GetMeterRateLevel(wPlayTime);
    // Above the second threshold the play time stops counting.
    if (nLevel != 2)
        wPlayTime = (WORD)(wPlayTime + bySeconds);
    if (User_GetMeterRateLevel(wPlayTime) != nLevel)
        SendPlayTime();
}

BYTE CUser::SendPlayTime()
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_PLAY_TIME);
    DWORD dwJID = m_LoginInfo.dwJID;
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&m_LoginInfo.w176, 2);
    if (!g_pMainProcess)
        BSAssertFail();
    return (BYTE)MainProcess_SendToServerBody(pMsg, m_LoginInfo.wGameServerBodyID, 0);
}

void CUser::NotifyStartPlayTime()
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_START_PLAY_TIME);
    DWORD dwJID = m_LoginInfo.dwJID;
    pMsg->WriteBytes(&dwJID, 4);
    if (!g_pMainProcess)
        BSAssertFail();
    if (!MainProcess_RelayToGlobalManager(pMsg))
    {
        LogPrintf(0, "FRAMEWORKMSG_START_PLAYTIME_INGAME_NOTIFY : Invalid GlobalServer");
        DWORD dwJIDToClose = m_LoginInfo.dwJID;
        if (!g_pMainProcess)
            BSAssertFail();
        MainProcess_DisconnectUser(dwJIDToClose, 0xFF);
    }
}

// ShardMgr8A894C_CheckByte141 (0x42ab90, CGameWorld::CheckLayerCapacity): the GM level of the
// account on the GM level manager.
BOOL User_HasGMLevelForIP(CUser* pUser)
{
    return GMLevelManager_HasGMLevelForIP(pUser->m_LoginInfo.b015);
}

// The accessors the other groups declared for the CUser owner.
DWORD User_GetCharID(const CUser* pUser)
{
    return pUser->m_LoginInfo.dwCharID;
}

UserGameWorldPos* User_GetGameWorldPos(CUser* pUser)
{
    return (UserGameWorldPos*)&pUser->m_GameWorld;
}

const char* User_GetCharName(CUser* pUser)
{
    return pUser->m_LoginInfo.strCharName.c_str();
}

// ---------------------------------------------------------------------------------------------
// CClientManager

CClientManager::~CClientManager()
{
    // The original clears the pointer after the members are gone.
    g_pClientManager = NULL;
}

CUser* ClientManager_FindUserByJID(DWORD dwJID)
{
    if (!g_pClientManager)
        BSAssertFail();
    return g_pClientManager->FindUserByJID(dwJID);
}

CUser* CClientManager::FindUserByJID(DWORD dwJID)
{
    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.find(dwJID);
    if (it == m_hmapUserByJID.end())
        return NULL;
    return it->second;
}

CUser* CClientManager::FindUserByCharName(const std::string& strName)
{
    stdext::hash_map<std::string, CUser*>::iterator it = m_hmapUserByName.find(strName);
    if (it == m_hmapUserByName.end())
        return NULL;
    return it->second;
}

int CClientManager::GetCharCountByJID(DWORD dwJID)
{
    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.find(dwJID);
    if (it == m_hmapUserByJID.end())
        return -1;
    return (int)it->second->m_dwCharCount;
}

BOOL CClientManager::DeleteCharTemporal(DWORD dwJID, const char* pszCharName)
{
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        BSAssertFail();
        return FALSE;
    }
    return pUser->DeleteCharTemporal(pszCharName);
}

BOOL CClientManager::RegisterCharacter(CUser* pUser, const char* pszCharName, BYTE byMode,
                                       CMsg* pExtra, WORD* pwResult)
{
    if (!pExtra)
        BSAssertFail();
    *pwResult = 0x401;
    BOOL bResult = pUser->SelectCharacter(pszCharName, pExtra, byMode, pwResult);
    if (bResult && byMode == 1)
    {
        m_hmapUserByName.insert(std::make_pair(std::string(pszCharName), pUser));
        m_hmapUserByCharID.insert(std::make_pair(pUser->m_LoginInfo.dwCharID, pUser));
    }
    return bResult;
}

BOOL CClientManager::RemoveUserByJID(DWORD dwJID)
{
    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.find(dwJID);
    if (it == m_hmapUserByJID.end())
        return FALSE;
    ReleaseUser(it->second);
    m_hmapUserByJID.erase(it);
    return TRUE;
}

void CClientManager::ReleaseUser(CUser* pUser)
{
    if (!pUser)
    {
        BSAssertFail();
        return;
    }
    if (pUser->m_LoginInfo.dwGameServerSession)
        DB_AddLogChar((int)pUser->m_LoginInfo.dwCharID, 0, 0, 6, (int)pUser->m_LoginInfo.dw018);

    std::string strName(pUser->m_LoginInfo.strCharName.c_str());
    stdext::hash_map<std::string, CUser*>::iterator itName = m_hmapUserByName.find(strName);
    if (itName != m_hmapUserByName.end())
        m_hmapUserByName.erase(itName);
    stdext::hash_map<DWORD, CUser*>::iterator itChar =
        m_hmapUserByCharID.find(pUser->m_LoginInfo.dwCharID);
    if (itChar != m_hmapUserByCharID.end())
        m_hmapUserByCharID.erase(itChar);

    pUser->Reset();
    m_UserPool.Free(pUser);

    std::set<CUser*>::iterator itUser = m_setUser.lower_bound(pUser);
    if (itUser == m_setUser.end())
        BSAssertFail();
    else
        m_setUser.erase(itUser);
}

void CClientManager::OnServerDisconnected(SERVER_CORD* pCord, SERVER_BODY* pPeer)
{
    if (pPeer->pModule == CertModule_FindByName("SR_GameServer"))
        OnGameServerDown(pCord);
    else if (pPeer->pModule == CertModule_FindByName("AgentServer"))
        OnAgentServerDown(pCord);
}

void CClientManager::OnGameServerDown(SERVER_CORD* pCord)
{
    // The client sessions of the users of the game server, by agent session.
    typedef std::map<DWORD, std::vector<DWORD> > SessionMap;
    SessionMap mapKick;
    DWORD dwSession = pCord->dwSessionID;

    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.begin();
    while (it != m_hmapUserByJID.end())
    {
        stdext::hash_map<DWORD, CUser*>::iterator itNext = it;
        ++itNext;
        CUser* pUser = it->second;
        if (pUser->m_LoginInfo.dwGameServerSession == dwSession ||
            (pUser->m_LoginInfo.dw150 == dwSession && pUser->GetState() == 1))
        {
            DWORD dwAgentSession = pUser->m_LoginInfo.dwAgentSessionID;
            SessionMap::iterator itAgent = mapKick.lower_bound(dwAgentSession);
            if (itAgent == mapKick.end() || dwAgentSession < itAgent->first)
            {
                std::vector<DWORD> vecNew;
                vecNew.reserve(1000);
                mapKick.insert(std::make_pair(dwAgentSession, vecNew));
                itAgent = mapKick.lower_bound(dwAgentSession);
            }
            if (itAgent == mapKick.end())
                BSAssertFail();
            std::vector<DWORD>& vecClient = itAgent->second;
            DWORD dwClientSession = pUser->m_LoginInfo.dwClientSessionID;
            std::vector<DWORD>::iterator itFound = vecClient.begin();
            for (; itFound != vecClient.end(); ++itFound)
            {
                if (*itFound == dwClientSession)
                    break;
            }
            if (itFound != vecClient.end())
                BSAssertFail();
            vecClient.push_back(dwClientSession);
            ReleaseUser(pUser);
            m_hmapUserByJID.erase(it);
        }
        it = itNext;
    }

    for (SessionMap::iterator itAgent = mapKick.begin(); itAgent != mapKick.end(); ++itAgent)
    {
        DWORD dwAgentSession = itAgent->first;
        std::vector<DWORD>& vecClient = itAgent->second;
        DWORD dwCount;
        for (DWORD i = 0; i < (DWORD)vecClient.size(); i += dwCount)
        {
            CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
            pMsg->SetOpcode(MSG_SHARD_AGENT_KICK);
            dwCount = (DWORD)vecClient.size() - i;
            if (dwCount > SHARD_KICK_PER_MSG)
            {
                dwCount = SHARD_KICK_PER_MSG;
            }
            else
            {
                dwCount = (WORD)dwCount;
                if (!dwCount)
                    BSAssertFail();
            }
            pMsg->WriteBytes(&dwCount, 2);
            pMsg->WriteBytes(&vecClient.at(i), (WORD)(4 * LOWORD(dwCount)));
            g_pNetEngine->SendMsg(dwAgentSession, pMsg);
            g_pNetEngine->ReleaseMsg(pMsg);
        }
    }
}

void CClientManager::OnAgentServerDown(SERVER_CORD* pCord)
{
    std::list<DWORD> listJID;   // filled and dropped, as in the original
    DWORD dwSession = pCord->dwSessionID;

    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.begin();
    while (it != m_hmapUserByJID.end())
    {
        stdext::hash_map<DWORD, CUser*>::iterator itNext = it;
        ++itNext;
        CUser* pUser = it->second;
        if (pUser->m_LoginInfo.dwAgentSessionID == dwSession)
        {
            pUser->m_dwState = USER_STEP_LOGOUT_WORLD1;
            pUser->m_dwStateTime = 0;
            CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
            pMsg->SetOpcode(MSG_SHARD_GS_LOGOUT_USER);
            DWORD dwJID = pUser->m_LoginInfo.dwJID;
            pMsg->WriteBytes(&dwJID, 4);
            if (!g_pMainProcess)
                BSAssertFail();
            MainProcess_SendToServerBody(pMsg, pUser->m_LoginInfo.wGameServerBodyID,
                                         pUser->m_LoginInfo.dwJID);
            listJID.push_back(pUser->m_LoginInfo.dwJID);
            ReleaseUser(pUser);
            m_hmapUserByJID.erase(it);
        }
        it = itNext;
    }
}

void CClientManager::TickUserTimeouts()
{
    stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.begin();
    while (it != m_hmapUserByJID.end())
    {
        stdext::hash_map<DWORD, CUser*>::iterator itNext = it;
        ++itNext;
        CUser* pUser = it->second;
        int nTimeout = pUser->GetStateTimeoutMs();
        if (nTimeout != -1)
        {
            pUser->m_dwStateTime += 5000;
            if (pUser->m_dwStateTime > (DWORD)nTimeout)
            {
                if (!g_pMainProcess)
                    BSAssertFail();
                MainProcess_DisconnectUser(it->first, 5);
            }
        }
        it = itNext;
    }
}

bool CClientManager::SerializeUserList(ServerFramework::CMassiveMsg* pMsg)
{
    WORD wCount = (WORD)m_hmapUserByJID.size();
    if (!wCount)
        return false;
    pMsg->Write(Shard_GetServerBodyOfMyself()->pShard, 2);
    pMsg->Write(&wCount, 2);
    for (stdext::hash_map<DWORD, CUser*>::iterator it = m_hmapUserByJID.begin();
         it != m_hmapUserByJID.end(); ++it)
    {
        sUserLoginInfo& info = it->second->m_LoginInfo;
        pMsg->Write(&info.dw018, 4);
        pMsg->Write(&info.dwJID, 4);
        pMsg->WriteString(info.strAccountID.c_str());
        pMsg->Write(&info.wAgentBodyID, 2);
        pMsg->Write(&info.dw170, 6);
        pMsg->Write(&info.w176, 2);
    }
    return true;
}

void CClientManager::SetPlayTimeFlag(WORD wFlag)
{
    for (std::set<CUser*>::iterator it = m_setUser.begin(); it != m_setUser.end(); ++it)
        CUser_SetPlayTimeFlagAndSend(wFlag, *it);
}

void CClientManager::AddPlayTime(BYTE bySeconds)
{
    for (std::set<CUser*>::iterator it = m_setUser.begin(); it != m_setUser.end(); ++it)
        (*it)->AddPlayTime(bySeconds);
}

// ---------------------------------------------------------------------------------------------
// The main process on the users

void MainProcess_DisconnectUser(DWORD dwJID, BYTE byFlags)
{
    if (!g_pClientManager)
        BSAssertFail();
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    if (!pUser)
        return;
    DWORD dwAgentSession = pUser->m_LoginInfo.dwAgentSessionID;
    DWORD dwClientSession = pUser->m_LoginInfo.dwClientSessionID;
    if (byFlags & 1)
    {
        pUser->m_dwState = USER_STEP_LOGOUT_WORLD1;
        pUser->m_dwStateTime = 0;
        CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
        pMsg->SetOpcode(MSG_SHARD_GS_LOGOUT_USER);
        pMsg->WriteBytes(&dwJID, 4);
        if (!MainProcess_SendToServerBody(pMsg, pUser->m_LoginInfo.wGameServerBodyID, dwJID))
        {
            // The game server is gone: the user is removed whatever the flags say.
            if (!g_pClientManager)
                BSAssertFail();
            g_pClientManager->RemoveUserByJID(dwJID);
            return;
        }
    }
    if (byFlags & 4)
    {
        if (!g_pClientManager)
            BSAssertFail();
        g_pClientManager->RemoveUserByJID(dwJID);
    }
    if (byFlags & 2)
    {
        CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
        pMsg->SetOpcode(MSG_SHARD_AGENT_KICK);
        WORD wCount = 1;
        pMsg->WriteBytes(&wCount, 2);
        pMsg->WriteBytes(&dwClientSession, 4);
        g_pNetEngine->SendMsg(dwAgentSession, pMsg);
        g_pNetEngine->ReleaseMsg(pMsg);
    }
}

void MainProcess_SendSwitchSession(CUser* pUser)
{
    DWORD dwJID = pUser->m_LoginInfo.dwJID;
    pUser->m_dwState = USER_STEP_SWITCHED_WORLD1;
    pUser->m_dwStateTime = 0;
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_SHARD_GS_SWITCH_SESSION);
    pMsg->WriteBytes(&dwJID, 4);
    pMsg->WriteBytes(&pUser->m_LoginInfo.w176, 2);
    if (!MainProcess_SendToServerBody(pMsg, pUser->m_LoginInfo.wGameServerBodyID, dwJID))
        MainProcess_DisconnectUser(dwJID, 0xFF);
}

void MainProcess_OnMsg_A207(CMsg* pMsg)
{
    DWORD dwJID;
    BYTE byResult;
    BYTE byError = 0xFF;
    *pMsg >> dwJID;
    *pMsg >> byResult;
    if (!g_pClientManager)
        BSAssertFail();
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    if (!pUser)
        return;
    if (pUser->m_dwState != (DWORD)USER_STEP_SWITCH_WORLD1)
    {
        LogPrintf(0,
                  "OnSR_PREPARE_USER_ACK() - login sequence of [%s] is broken. will be banned "
                  "[desired: %d, cur_state: %d]",
                  pUser->m_LoginInfo.strCharName.c_str(), 4, HIWORD(pUser->m_dwState));
        MainProcess_DisconnectUser(dwJID, 0xFF);
        return;
    }
    BOOL bOk;
    if (byResult == 2)
    {
        *pMsg >> byError;
        bOk = byError == 3;
    }
    else
    {
        bOk = byResult == 1;
    }
    if (bOk)
    {
        MainProcess_SendSwitchSession(pUser);
        return;
    }
    LogPrintf(0,
              "CHANGE_CONTENT_SERVER_ACK() - agent returned error. session will be closed... "
              "[%s >> error_code: %d]",
              pUser->m_LoginInfo.strCharName.c_str(), byError);
    MainProcess_DisconnectUser(dwJID, 0xFF);
}
