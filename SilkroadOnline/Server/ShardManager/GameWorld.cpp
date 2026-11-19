#include "ShardManager/GameWorld.h"

#include <algorithm>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerCommon/ReferenceData.h"

enum
{
    OP_GAMEWORLD_STATE          = 0x3C15,   // 1 opened, 2 banned, 3 closed
    OP_GAMEWORLD_REMAIN_TIME    = 0x3CE8,
    OP_GAMEWORLD_LIST_ACK       = 0xBC10,
    OP_GAMEWORLD_OPEN_ACK       = 0xBC14
};

// ---------------------------------------------------------------------------------------------
// sGameWorldCreateParam

sGameWorldCreateParam::sGameWorldCreateParam()
{
    m_dw0 = 0;
    m_b4 = 1;
    m_dw8 = 0;
    m_b12 = 0;
    m_b13 = 0;
    m_dwDuration = 0;
    m_dwEmptyRemainTime = 0;
    m_b24 = 0;
    m_vecParam.clear();
}

void sGameWorldCreateParam::CopyTo(sGameWorldCreateParam& dst) const
{
    dst.m_dw0 = m_dw0;
    dst.m_b4 = m_b4;
    dst.m_dw8 = m_dw8;
    dst.m_b12 = m_b12;
    dst.m_b13 = m_b13;
    dst.m_dwDuration = m_dwDuration;
    dst.m_dwEmptyRemainTime = m_dwEmptyRemainTime;
    dst.m_b24 = m_b24;
    // BUG(SM-102): nothing is copied for an empty vector, dst keeps its old values.
    size_t nCount = m_vecParam.size();
    if (nCount)
    {
        dst.m_vecParam.resize(nCount);
        for (size_t i = 0; i < nCount; ++i)
            dst.m_vecParam.at(i) = m_vecParam.at(i);
    }
}

void sGameWorldCreateParam::ReadFromMsg(CMsg* pMsg)
{
    *pMsg >> m_dw0 >> m_b4 >> m_dw8 >> m_b12 >> m_b13 >> m_dwDuration >> m_dwEmptyRemainTime;
    WORD wCount;
    *pMsg >> wCount;
    if (wCount)
    {
        m_vecParam.resize(wCount);
        for (DWORD i = 0; i < wCount; ++i)
            *pMsg >> m_vecParam.at(i);
    }
}

void sGameWorldCreateParam::WriteToMsg(CMsg* pMsg) const
{
    pMsg->WriteBytes(&m_dw0, 4);
    pMsg->WriteBytes(&m_b4, 1);
    pMsg->WriteBytes(&m_dw8, 4);
    pMsg->WriteBytes(&m_b12, 1);
    pMsg->WriteBytes(&m_b13, 1);
    pMsg->WriteBytes(&m_dwDuration, 4);
    pMsg->WriteBytes(&m_dwEmptyRemainTime, 4);
    WORD wCount = (WORD)m_vecParam.size();
    pMsg->WriteBytes(&wCount, 2);
    for (DWORD i = 0; i < wCount; ++i)
        pMsg->WriteBytes(&m_vecParam.at(i), 4);
}

// ---------------------------------------------------------------------------------------------
// GameWorldLayer

GameWorldLayer::GameWorldLayer(WORD wLayerID)
{
    m_dwCreateTime = 0;
    m_dwInstanceID = 0;
    m_wLayerID = wLayerID;
    m_dwCreateTime = 0;
    m_dwEmptySince = 0;
    m_dwCloseTick = 0;
    // m_dwWorldID is set by Init only.
}

GameWorldLayer::~GameWorldLayer()
{
}

void GameWorldLayer::Init(DWORD dwWorldID, const sGameWorldCreateParam* pParam,
                          DWORD dwInstanceID, const std::vector<int>& vecNotify)
{
    m_dwInstanceID = dwInstanceID;
    PackedDateTime_GetLocal(&m_dwCreateTime);
    pParam->CopyTo(m_Param);
    DWORD dwNow = GetTickCount();
    m_dwEmptySince = dwNow;
    m_dwCloseTick = dwNow + m_Param.m_dwDuration;
    m_dwWorldID = dwWorldID;
    m_vecNotify.assign(vecNotify.begin(), vecNotify.end());
    std::sort(m_vecNotify.begin(), m_vecNotify.end());
}

void GameWorldLayer::WriteToMsg(CMsg* pMsg) const
{
    pMsg->WriteBytes(&m_wLayerID, 2);
    pMsg->WriteBytes(&m_dwInstanceID, 4);
    pMsg->WriteBytes(&m_dwCreateTime, 4);
    m_Param.WriteToMsg(pMsg);
}

WORD GameWorldLayer::CountRecentReservations() const
{
    WORD wCount = 0;
    for (ReservationMap::const_iterator it = m_mapReservation.begin();
         it != m_mapReservation.end(); ++it)
    {
        if (it->second.dwTick + 10000 > GetTickCount())
            ++wCount;
    }
    return wCount;
}

// ---------------------------------------------------------------------------------------------
// CGameWorld

CGameWorld::CGameWorld()
{
    m_pRef = NULL;
}

CGameWorld::~CGameWorld()
{
}

void CGameWorld::Init(RefGameWorld* pRef)
{
    m_pRef = pRef;
    ResizeLayers();
    CollectStartPositions();
}

void CGameWorld::Tick()
{
    if (!m_pRef->Type)
        return;

    DWORD dwNow = GetTickCount();
    WORD wCount = (WORD)m_vecLayer.size();
    for (int i = 0; i < (int)wCount; ++i)
    {
        GameWorldLayer* pLayer = m_vecLayer.at(i);
        if (!pLayer)
            continue;

        if (!pLayer->m_vecNotify.empty() && pLayer->m_dwCloseTick)
        {
            DWORD dwRemain = pLayer->m_vecNotify.front();
            if (dwRemain >= pLayer->m_dwCloseTick - dwNow)
            {
                pLayer->m_vecNotify.erase(pLayer->m_vecNotify.begin());
                NotifyLayerRemainTime((WORD)m_pRef->ID, (WORD)i, dwRemain);
            }
        }

        if (pLayer->m_dwCloseTick && pLayer->m_dwCloseTick <= dwNow)
        {
            WORD wLayerID = pLayer->m_wLayerID;
            CGameWorldMgr::GetInstance();
            CGameWorldMgr::BroadcastClosed((WORD)m_pRef->ID, wLayerID);
            pLayer->m_dwCloseTick = 0;
        }
        else if (!pLayer->m_mapMember.size()
                 && dwNow - pLayer->m_dwEmptySince >= pLayer->m_Param.m_dwEmptyRemainTime)
        {
            CloseLayer((WORD)i);
            WORD wLayerID = pLayer->m_wLayerID;
            CGameWorldMgr::GetInstance();
            CGameWorldMgr::BroadcastBanned((WORD)m_pRef->ID, wLayerID);
        }
    }
}

void CGameWorld::ResizeLayers()
{
    m_vecLayer.resize((WORD)m_pRef->WorldMaxCount + 1);
}

void CGameWorld::CollectStartPositions()
{
    CReferenceData* pRefData = CReferenceData::GetInstance();
    CReferenceData::RefInstanceWorldStartPosVector& vec = pRefData->m_vecInstanceWorldStartPos;
    for (CReferenceData::RefInstanceWorldStartPosVector::iterator it = vec.begin();
         it != vec.end(); ++it)
    {
        if ((*it)->WorldID == m_pRef->ID)
            m_vecStartPos.push_back(*it);
    }
}

void CGameWorld::CloseAllLayers()
{
    WORD wCount = (WORD)m_vecLayer.size();
    for (int i = 0; i < (int)wCount; ++i)
    {
        GameWorldLayer* pLayer = m_vecLayer.at(i);
        if (pLayer)
            delete pLayer;
    }
    m_vecLayer.erase(m_vecLayer.begin(), m_vecLayer.end());

    for (std::list<GameWorldLayer*>::iterator it = m_listFree.begin(); it != m_listFree.end();
         ++it)
    {
        if (*it)
            delete *it;
    }
    m_listFree.clear();
}

GameWorldLayer* CGameWorld::CreateLayer(const sGameWorldCreateParam* pParam, DWORD dwInstanceID,
                                        BYTE* pbyResult)
{
    // a world without instances has layer 1 only
    if (!m_pRef->Type && m_vecLayer.at(1))
        return m_vecLayer.at(1);

    if (m_listFree.empty())
    {
        if (!m_pRef->WorldMaxCount)
        {
            // BUG(SM-103): the slot vector has one entry here, LayerAt below throws
            // out_of_range.
            m_listFree.push_back(new GameWorldLayer((WORD)(m_vecLayer.size() + 1)));
        }
        else
        {
            for (DWORD i = 1; i < m_vecLayer.size(); ++i)
            {
                if (!m_vecLayer.at(i))
                {
                    m_listFree.push_back(new GameWorldLayer((WORD)i));
                    break;
                }
            }
            if (m_listFree.empty())
            {
                *pbyResult = 7;
                return NULL;
            }
        }
    }

    GameWorldLayer* pLayer = m_listFree.front();
    m_listFree.erase(m_listFree.begin());
    if (LayerAt(pLayer->m_wLayerID) || pLayer->m_mapMember.size())
        BSAssertFail();

    std::vector<int> vecNotify;
    RefGameWorld* pRef = m_pRef;
    if (pRef)
        GetDisappearanceTime(CReferenceData::GetInstance(), pRef->ConfigGroupCodeName128,
                             vecNotify);
    pLayer->Init((DWORD)pRef->ID, pParam, dwInstanceID, vecNotify);
    LayerAt(pLayer->m_wLayerID) = pLayer;
    return pLayer;
}

BYTE CGameWorld::EnterLayer(WORD wLayerID, CUser* pUser)
{
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
    {
        BSAssertFail();
        return 6;
    }
    DWORD dwCharID = pUser->GetCharID();
    GameWorldContext* pPos = User_GetGameWorldPos(pUser);
    if (pLayer->m_mapMember.find(dwCharID) != pLayer->m_mapMember.end() || !pPos)
    {
        BSAssertFail();
        return 2;
    }
    pPos->wWorldID = (WORD)m_pRef->ID;
    pPos->wLayerID = pLayer->m_wLayerID;
    pPos->pGameWorld = this;
    pLayer->m_dwEmptySince = 0;
    pLayer->m_mapMember.insert(std::make_pair(pUser->GetCharID(), pUser));
    return 1;
}

BYTE CGameWorld::LeaveLayer(CUser* pUser, WORD wLayerID)
{
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
        return 6;
    GameWorldLayer::MemberMap::iterator it = pLayer->m_mapMember.find(pUser->GetCharID());
    if (it == pLayer->m_mapMember.end())
        return 6;
    pLayer->m_mapMember.erase(it);
    if (!pLayer->m_mapMember.size())
        pLayer->m_dwEmptySince = GetTickCount();
    return 1;
}

void CGameWorld::AddReservation(WORD wLayerID, const GameWorldReservation* pReservation)
{
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
    {
        BSAssertFail();
        return;
    }
    GameWorldLayer::ReservationMap::iterator it =
        pLayer->m_mapReservation.find(pReservation->dwCharID);
    if (it != pLayer->m_mapReservation.end())
        pLayer->m_mapReservation.erase(it);
    pLayer->m_mapReservation.insert(std::make_pair(pReservation->dwCharID, *pReservation));
}

void CGameWorld::RemoveReservation(WORD wLayerID, const DWORD* pdwCharID)
{
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
        return;
    GameWorldLayer::ReservationMap::iterator it = pLayer->m_mapReservation.find(*pdwCharID);
    if (it != pLayer->m_mapReservation.end())
        pLayer->m_mapReservation.erase(it);
}

BYTE CGameWorld::CloseLayer(WORD wLayerID)
{
    if (!wLayerID || wLayerID >= m_vecLayer.size())
        return 3;
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
    {
        BSAssertFail();
        return 0;
    }

    WORD wWorldID = (WORD)m_pRef->ID;
    if (wWorldID >= 0xFFFF)
        BSAssertFail();
    DWORD dwKey = MAKELONG(wWorldID, pLayer->m_wLayerID);
    CGameWorldMgr::GetInstance()->UnregisterInstance(&dwKey);

    LayerAt(wLayerID) = NULL;
    pLayer->m_dwCreateTime = 0;
    pLayer->m_mapMember.clear();
    for (GameWorldLayer::ReservationMap::iterator it = pLayer->m_mapReservation.begin();
         it != pLayer->m_mapReservation.end(); ++it)
    {
        CGameWorldMgr::GetInstance()->EraseReservation(it->second.dwCharID);
    }
    pLayer->m_mapReservation.clear();
    m_listFree.push_back(pLayer);
    return 1;
}

void CGameWorld::WriteLayerList(CMsg* pMsg) const
{
    WORD wWorldID = (WORD)m_pRef->ID;
    pMsg->WriteBytes(&wWorldID, 2);
    WORD wSlots = (WORD)m_vecLayer.size();
    WORD wOpen = 0;
    for (WORD i = 1; i < wSlots; ++i)
    {
        if (m_vecLayer.at(i))
            ++wOpen;
    }
    pMsg->WriteBytes(&wOpen, 2);
    for (WORD i = 1; i < wSlots; ++i)
    {
        const GameWorldLayer* pLayer = m_vecLayer.at(i);
        if (pLayer)
            pLayer->WriteToMsg(pMsg);
    }
}

BYTE CGameWorld::SetLayerCloseTime(WORD wLayerID, DWORD dwRemain)
{
    if (!m_pRef->Type)
        return 8;
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
    {
        BSAssertFail();
        return 6;
    }
    pLayer->m_dwCloseTick = dwRemain + GetTickCount();
    return 1;
}

BYTE CGameWorld::CheckLayerCapacity(WORD wLayerID, CUser* pUser)
{
    if (!wLayerID)
        return 1;
    if (!pUser)
        return 2;
    GameWorldLayer* pLayer = LayerAt(wLayerID);
    if (!pLayer)
    {
        BSAssertFail();
        return 6;
    }
    if (!m_pRef->WorldMaxUserCount)
        return 1;
    if (User_HasGMLevelForIP(pUser) == TRUE)
        return 1;
    WORD wMembers = (WORD)pLayer->m_mapMember.size();
    WORD wRecent = pLayer->CountRecentReservations();
    if ((int)(WORD)m_pRef->WorldMaxUserCount <= (int)wRecent + (int)wMembers)
        return 14;
    return 1;
}

void CGameWorld::NotifyLayerRemainTime(WORD wWorldID, WORD wLayerID, DWORD dwRemain)
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CMainProcess::CountReadyGameServers() || !m_pRef)
        return;
    BYTE byMinutes = (BYTE)(dwRemain / 1000 / 60);
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_GAMEWORLD_REMAIN_TIME);
    pMsg->WriteBytes(&wWorldID, 2);
    pMsg->WriteBytes(&wLayerID, 2);
    pMsg->WriteBytes(&byMinutes, 1);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToAllGameServers(pMsg);
}

BOOL CGameWorld::GetDisappearanceTime(CReferenceData* pRefData, const std::string& strGroup,
                                      std::vector<int>& vecValue)
{
    std::string strDesc("GAMEWORLD_DISAPPEARANCE_TIME");
    return pRefData->GetShardContentConfigInt32(strGroup, strDesc, vecValue);
}

// ---------------------------------------------------------------------------------------------
// CGameWorldMgr

CGameWorldMgr* g_pGameWorldMgr = NULL;
CGameWorldMgr g_GameWorldMgr;

CGameWorldMgr::CGameWorldMgr()
{
    if (g_pGameWorldMgr)
        BSAssertFail();
    g_pGameWorldMgr = this;
    m_dwInstanceCounter = 0;
}

CGameWorldMgr::~CGameWorldMgr()
{
    Clear();
    m_mapReservation.clear();
    m_mapInstance.clear();
    m_mapWorld.clear();
    g_pGameWorldMgr = NULL;
}

CGameWorldMgr* CGameWorldMgr::GetInstance()
{
    if (!g_pGameWorldMgr)
        BSAssertFail();
    return g_pGameWorldMgr;
}

void CGameWorldMgr::TickAll()
{
    for (WorldMap::iterator it = m_mapWorld.begin(); it != m_mapWorld.end(); ++it)
        it->second->Tick();
}

CGameWorld* CGameWorldMgr::FindWorld(WORD wWorldID)
{
    WorldMap::iterator it = m_mapWorld.find(wWorldID);
    if (it == m_mapWorld.end())
        return NULL;
    return it->second;
}

void CGameWorldMgr::Clear()
{
    for (WorldMap::iterator it = m_mapWorld.begin(); it != m_mapWorld.end(); ++it)
    {
        CGameWorld* pWorld = it->second;
        pWorld->CloseAllLayers();
        delete pWorld;
    }
    m_mapWorld.clear();
}

bool CGameWorldMgr::IsCharInLayer(const GameWorldCharPos* pChar)
{
    if (!pChar)
    {
        BSAssertFail();
        return false;
    }
    WORD wWorldID = pChar->wWorldID;
    WORD wLayerID = pChar->wLayerID;
    if (wWorldID >= 0xFFFF)
        BSAssertFail();
    CGameWorld* pWorld = GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
        return false;
    GameWorldLayer* pLayer = pWorld->LayerAt(wLayerID);
    if (!pLayer)
        return false;
    DWORD dwCreateTime = 0;
    RefObj_PackTypeID(dwCreateTime, pChar->awCreateTime);
    return pLayer->m_dwCreateTime == dwCreateTime;
}

GameWorldLayer* CGameWorldMgr::OpenLayer(BYTE* pbyResult, WORD wWorldID,
                                         const sGameWorldCreateParam* pParam)
{
    CGameWorld* pWorld = FindWorld(wWorldID);
    if (!pWorld)
    {
        *pbyResult = 6;
        return NULL;
    }
    ++m_dwInstanceCounter;
    *pbyResult = 1;
    GameWorldLayer* pLayer = pWorld->CreateLayer(pParam, m_dwInstanceCounter, pbyResult);
    if (*pbyResult != 1)
        return NULL;

    WORD wLayerID = pLayer->m_wLayerID;
    if (wWorldID >= 0xFFFF)
        BSAssertFail();
    DWORD dwKey = MAKELONG(wWorldID, wLayerID);
    if (RegisterInstance(&dwKey, pLayer->m_dwInstanceID))
        return pLayer;

    LogPrintf(0x2000000, "Register Instance World Failed!!");
    *pbyResult = 2;
    pWorld->CloseLayer(wLayerID);
    return NULL;
}

bool CGameWorldMgr::RegisterInstance(const DWORD* pdwKey, DWORD dwInstanceID)
{
    if (m_mapInstance.find(*pdwKey) != m_mapInstance.end())
    {
        BSAssertFail();
        return false;
    }
    m_mapInstance.insert(std::make_pair(*pdwKey, dwInstanceID));
    return true;
}

void CGameWorldMgr::UnregisterInstance(const DWORD* pdwKey)
{
    InstanceMap::iterator it = m_mapInstance.find(*pdwKey);
    if (it == m_mapInstance.end())
        BSAssertFail();
    else
        m_mapInstance.erase(it);
}

BYTE CGameWorldMgr::EnterWorld(CUser* pUser, DWORD dwWorldLayer)
{
    CGameWorld* pWorld = FindWorld(LOWORD(dwWorldLayer));
    if (!pWorld)
        return 6;
    CancelReservation(pUser);
    return pWorld->EnterLayer(HIWORD(dwWorldLayer), pUser);
}

BYTE CGameWorldMgr::OnUserLogout(CUser* pUser)
{
    GameWorldContext* pPos = User_GetGameWorldPos(pUser);
    if (!pPos->pGameWorld)
        return 2;
    CGameWorld* pWorld = FindWorld(pPos->wWorldID);
    if (!pWorld)
        return 6;
    return pWorld->LeaveLayer(pUser, pPos->wLayerID);
}

void CGameWorldMgr::ReserveEnter(const WORD* pwWorldLayer, CUser* pUser)
{
    CGameWorld* pWorld = FindWorld(pwWorldLayer[0]);
    if (!pWorld)
        return;
    if (m_mapReservation.find(pUser->GetCharID()) != m_mapReservation.end())
        CancelReservation(pUser);

    GameWorldReservation r;
    r.dwCharID = pUser->GetCharID();
    *(DWORD*)&r.wWorldID = *(const DWORD*)pwWorldLayer;
    r.dwTick = GetTickCount();
    m_mapReservation.insert(std::make_pair(r.dwCharID, r));
    pWorld->AddReservation(pwWorldLayer[1], &r);
}

void CGameWorldMgr::CancelReservation(CUser* pUser)
{
    if (!pUser)
        return;
    ReservationMap::iterator it = m_mapReservation.find(pUser->GetCharID());
    if (it == m_mapReservation.end())
        return;
    GameWorldReservation r = it->second;
    CGameWorld* pWorld = FindWorld(r.wWorldID);
    if (!pWorld)
        return;
    m_mapReservation.erase(it);
    pWorld->RemoveReservation(r.wLayerID, &r.dwCharID);
}

void CGameWorldMgr::EraseReservation(DWORD dwCharID)
{
    ReservationMap::iterator it = m_mapReservation.find(dwCharID);
    if (it != m_mapReservation.end())
        m_mapReservation.erase(it);
}

void CGameWorldMgr::OnMsg_7C14(CMsg* pMsg)
{
    switch (pMsg->ReadForwardOnly<BYTE>())
    {
    case 1:
        OnOpenWorldRequest(pMsg);
        break;
    case 3:
        OnBanCloseWorld(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CGameWorldMgr::OnMsg_7C16(CMsg* pMsg)
{
    if (pMsg->ReadForwardOnly<BYTE>() == 2)
    {
        OnCloseWorldRequest(pMsg);
        return;
    }
    BSAssertFail();
    if (pMsg)
        pMsg->SkipRemaining();
}

void CGameWorldMgr::OnOpenWorldRequest(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwJID = pMsg->ReadForwardOnly<DWORD>();
    WORD wWorldID = pMsg->ReadForwardOnly<WORD>();
    sGameWorldCreateParam param;
    param.ReadFromMsg(pMsg);

    BYTE byResult = 1;
    CMsg* pReply;
    GameWorldLayer* pLayer = NULL;
    bool bOpened = false;
    if (GetSingleton_CClientManager()->FindUserByJID(dwJID) && wWorldID != 1)
    {
        pLayer = OpenLayer(&byResult, wWorldID, &param);
        if (byResult == 1)
        {
            if (pLayer)
                bOpened = true;
            else
            {
                BSAssertFail();
                byResult = 2;
            }
        }
    }
    else
    {
        byResult = 2;
    }

    if (bOpened)
    {
        BroadcastOpened(wWorldID, pLayer);
        pReply = AllocReply_BC14(dwRequestID);
        WORD wLayerID = pLayer->m_wLayerID;
        DWORD dwInstanceID = pLayer->m_dwInstanceID;
        BYTE byOk = 1;
        pReply->WriteBytes(&byOk, 1);
        pReply->WriteBytes(&wLayerID, 2);
        pReply->WriteBytes(&dwInstanceID, 4);
        pReply->WriteBytes(&pLayer->m_dwCreateTime, 4);
    }
    else
    {
        pMsg->SkipRemaining();
        pReply = AllocReply_BC14(dwRequestID);
        BYTE byFailed = 2;
        pReply->WriteBytes(&byFailed, 1);
        pReply->WriteBytes(&byResult, 1);
    }
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

void CGameWorldMgr::OnBanCloseWorld(CMsg* pMsg)
{
    WORD wWorldID = 0;
    WORD wLayerID = 0;
    *pMsg >> wWorldID >> wLayerID;
    if (!wWorldID || !wLayerID)
        return;
    CGameWorld* pWorld = FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000001, "Banned Completely!! But Failed CloseWorld Because pGameWorld is NULL ");
        return;
    }
    if (!pWorld->LayerAt(wLayerID))
    {
        LogPrintf(0x2000001, "Banned Completely!! But LayerID is NULL ==> WorldID = %d, LayerID = %d",
                  (WORD)pWorld->m_pRef->ID, wLayerID);
        return;
    }
    if (pWorld->CloseLayer(wLayerID) == 1)
        BroadcastBanned((WORD)pWorld->m_pRef->ID, wLayerID);
    else
        LogPrintf(0x2000001, "Banned Completely!! But Failed CloseWorld ==> WorldID = %d, LayerID = %d",
                  (WORD)pWorld->m_pRef->ID, wLayerID);
}

void CGameWorldMgr::OnCloseWorldRequest(CMsg* pMsg)
{
    DWORD dwWorldLayer = MAKELONG(1, 1);
    *pMsg >> dwWorldLayer;
    CGameWorld* pWorld = FindWorld(LOWORD(dwWorldLayer));
    if (!pWorld)
    {
        BSAssertFail();
        return;
    }
    if (pWorld->LayerAt(HIWORD(dwWorldLayer)))
    {
        BroadcastClosed(LOWORD(dwWorldLayer), HIWORD(dwWorldLayer));
        return;
    }
    // BUG(SM-104): the whole DWORD (world | layer << 16) is logged as the world ID.
    LogPrintf(0x2000000, "Already Closed World ( World ID = %d , Layer ID = %d ) ", dwWorldLayer,
              HIWORD(dwWorldLayer));
}

void CGameWorldMgr::OnWorldListRequest(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(OP_GAMEWORLD_LIST_ACK);
    WORD wCount = (WORD)m_mapWorld.size();
    pReply->WriteBytes(&wCount, 2);
    for (WorldMap::iterator it = m_mapWorld.begin(); it != m_mapWorld.end(); ++it)
        it->second->WriteLayerList(pReply);
    g_pBSNet->SendMsg(dwSessionID, pReply);
    g_pBSNet->ReleaseMsg(pReply);
}

static CMsg* AllocStateMsg(BYTE byState, WORD wWorldID)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_GAMEWORLD_STATE);
    pMsg->WriteBytes(&byState, 1);
    pMsg->WriteBytes(&wWorldID, 2);
    return pMsg;
}

void CGameWorldMgr::BroadcastOpened(WORD wWorldID, const GameWorldLayer* pLayer)
{
    CMsg* pMsg = AllocStateMsg(1, wWorldID);
    pLayer->WriteToMsg(pMsg);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, 0);
}

void CGameWorldMgr::BroadcastBanned(WORD wWorldID, WORD wLayerID)
{
    CMsg* pMsg = AllocStateMsg(2, wWorldID);
    pMsg->WriteBytes(&wLayerID, 2);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, 0);
}

void CGameWorldMgr::BroadcastClosed(WORD wWorldID, WORD wLayerID)
{
    CMsg* pMsg = AllocStateMsg(3, wWorldID);
    pMsg->WriteBytes(&wLayerID, 2);
    if (!g_pMainProcess)
        BSAssertFail();
    CMainProcess::BroadcastToGameServers(pMsg, 0);
}

CMsg* CGameWorldMgr::AllocReply_BC14(DWORD dwRequestID)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(OP_GAMEWORLD_OPEN_ACK);
    pMsg->WriteBytes(&dwRequestID, 4);
    return pMsg;
}
