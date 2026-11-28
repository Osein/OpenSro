#include "ShardManager/TrainingCamp.h"

#include <stdlib.h>
#include <strsafe.h>

#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"

CTrainingCampManager* g_pTrainingCampManager = NULL;

// The manager is a static object (0x89a660).
static CTrainingCampManager s_TrainingCampManager;

namespace
{

void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pNetEngine->SendMsg(dwSessionID, pMsg);
    g_pNetEngine->ReleaseMsg(pMsg);
}

// TrainingCamp_NewAckMsg (0x456fe0)
CMsg* NewAckMsg(DWORD dwRequestID)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_TRAINING_CAMP_ACK);
    *pMsg << dwRequestID;
    return pMsg;
}

CMsg* NewNotifyMsg(BYTE bySubType)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(MSG_TRAINING_CAMP_NOTIFY);
    *pMsg << bySubType;
    return pMsg;
}

// BroadcastToGameServers releases the message.
void Broadcast(CMsg* pMsg, DWORD dwExceptSession)
{
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToGameServers(pMsg, dwExceptSession);
}

void SendAckError(DWORD dwSessionID, DWORD dwRequestID, WORD wCode)
{
    CMsg* pAck = NewAckMsg(dwRequestID);
    *pAck << (BYTE)2 << wCode;
    SendAndRelease(dwSessionID, pAck);
}

void SendAckErrorTo(DWORD dwSessionID, CMsg* pAck, WORD wCode)
{
    *pAck << (BYTE)2 << wCode;
    SendAndRelease(dwSessionID, pAck);
}

// TrainingCamp_NewNotifyMsg3C80 (0x457020): subtype 20, the result of a mentor application.
void SendMentorAnswer(CUser* pUser, BYTE byAnswer)
{
    CMsg* pMsg = NewNotifyMsg(20);
    *pMsg << pUser->GetJID() << (BYTE)1 << byAnswer;
    SendAndRelease(pUser->GetSessionID(), pMsg);
}

void SendMentorError(CUser* pUser, WORD wCode)
{
    CMsg* pMsg = NewNotifyMsg(20);
    *pMsg << pUser->GetJID() << (BYTE)2 << wCode;
    SendAndRelease(pUser->GetSessionID(), pMsg);
}

// _TRAINING_CAMP_* procedures run on connection 7 of the shard DB.
int ShardExec(const char* pszSql, DWORD dwFlags, int* pnValue)
{
    if (!g_pShardDB)
        BSAssertFail();
    return CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), 7, pszSql, dwFlags,
                                                pnValue);
}

// The inlined local time of the evaluation date (0x41b490): fraction 0.
void Timestamp_SetLocalTime(TIMESTAMP_STRUCT* pTime)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    pTime->year = st.wYear;
    pTime->month = st.wMonth;
    pTime->day = st.wDay;
    pTime->hour = st.wHour;
    pTime->minute = st.wMinute;
    pTime->second = st.wSecond;
    pTime->fraction = 0;
}

CUser* FindUserByJID(DWORD dwJID)
{
    return GetSingleton_CClientManager()->FindUserByJID(dwJID);
}

// TrainingCamp_Notify3C80_MemberAdded (0x459680, subtype 2) / _MemberAdded_Job (0x459910, 21)
void NotifyMemberAdded(BYTE bySubType, DWORD dwCampID, const CInstanceTrainingCampMember* pRec)
{
    CMsg* pMsg = NewNotifyMsg(bySubType);
    *pMsg << dwCampID;
    TrainingCampMemberRecord_WriteToMsg(pMsg, pRec);
    Broadcast(pMsg, 0);
}

// TrainingCamp_Notify3C80_CampCreated (0x459620, subtype 0) / _CampDisbanded (0x459810, 1)
void NotifyCamp(BYTE bySubType, DWORD dwCampID)
{
    CMsg* pMsg = NewNotifyMsg(bySubType);
    *pMsg << dwCampID;
    Broadcast(pMsg, 0);
}

// TrainingCamp_Notify3C80_MemberKicked (0x4596f0, subtype 3) / _MemberGraduated (0x459780, 4)
void NotifyMemberLeft(BYTE bySubType, DWORD dwCampID, DWORD dwCharID, BYTE byReason)
{
    CMsg* pMsg = NewNotifyMsg(bySubType);
    *pMsg << dwCampID << dwCharID << byReason;
    Broadcast(pMsg, 0);
}

// TrainingCamp_Notify3C80_TextUpdated (0x459870, subtype 7)
void NotifyTextUpdated(DWORD dwCampID, const std::string& strTitle,
                       const std::string& strComment)
{
    CMsg* pMsg = NewNotifyMsg(7);
    *pMsg << dwCampID << strTitle.c_str() << strComment.c_str();
    Broadcast(pMsg, 0);
}

}

// ---------------------------------------------------------------------------------------------
// TrainingCampRegData / TrainingCampApplyReq

void TrainingCampRegData::ReadFromMsg(CMsg* pMsg)
{
    *pMsg >> m_dwID >> m_dw04 >> m_byFlags >> m_strText;
}

void TrainingCampRegData::WriteToMsg(CMsg* pMsg) const
{
    *pMsg << m_dwID << m_dw04 << m_byFlags << m_strText.c_str();
}

void TrainingCampRegData::WriteEntryToMsg(CMsg* pMsg) const
{
    if (!FindUserByJID(m_dwJID))
        return;
    WriteToMsg(pMsg);
    *pMsg << m_dwRegTime;
    m_pCamp->WriteSummaryToMsg(pMsg);
}

void TrainingCampApplyReq::ReadFromMsg(CMsg* pMsg)
{
    *pMsg >> m_dwRegID >> m_dw04 >> m_by08 >> m_byLevel >> m_dw0C >> m_strText;
}

// ---------------------------------------------------------------------------------------------
// CRegMatchMgr

template <class TCamp, class TData>
void CRegMatchMgr<TCamp, TData>::ClearListMsgs()
{
    for (DWORD i = 0; i < m_vecListMsg.size(); ++i)
        g_pNetEngine->ReleaseMsg(m_vecListMsg[i]);
    m_vecListMsg.clear();
}

template <class TCamp, class TData>
void CRegMatchMgr<TCamp, TData>::RebuildListMsgs()
{
    ClearListMsgs();
    DWORD dwLeft = (DWORD)m_mapData.size();
    if (!dwLeft)
        return;
    int nCount = 0;
    CMsg* pPage = NULL;
    typename DataMap::iterator it = m_mapData.begin();
    while (it != m_mapData.end())
    {
        // BUG(SM-274): when every entry so far was dropped, the empty page is not released.
        if (!nCount)
        {
            pPage = g_pNetEngine->AllocMsg(FALSE);
            if (!pPage)
                BSAssertFail();
        }
        TData* p = it->second;
        if (!p)
            BSAssertFail();
        typename DataMap::iterator itNext = it;
        ++itNext;
        if (FindUserByJID(p->m_dwJID))
        {
            *pPage << p->m_dwID << p->m_dw04 << p->m_byFlags << p->m_strText.c_str()
                   << p->m_dwRegTime;
            p->m_pCamp->WriteSummaryToMsg(pPage);
            ++nCount;
        }
        else
        {
            // The registrant keeps the entry ID (CUser +0x74).
            p->Reset();
            m_mapData.erase(it);
            m_Pool.Free(p);
        }
        --dwLeft;
        it = itNext;
        if (nCount == m_byPageSize - 1 || !dwLeft)
        {
            pPage->SetOpcode((WORD)nCount);
            m_vecListMsg.push_back(pPage);
            nCount = 0;
        }
    }
}

template class CRegMatchMgr<CTrainingCamp, TrainingCampRegData>;

// ---------------------------------------------------------------------------------------------
// CTrainingCamp

void CTrainingCamp::WriteSummaryToMsg(CMsg* pMsg)
{
    if (!m_pCamp)
        BSAssertFail();
    if (!m_pLeader)
    {
        BSAssertFail();
        if (!m_pLeader)
            return;
    }
    DWORD dwTrainees = CountTrainees();
    DWORD dwMentors = CountMentors();
    *pMsg << m_pLeader->CharCurLevel << m_pLeader->CharMaxLevel << m_pLeader->RefObjID
          << m_pLeader->CharName << m_pCamp->GraduateCount << m_pCamp->Rank << dwMentors
          << dwTrainees;
}

void CTrainingCamp::UpdateMemberStats(DWORD dwExceptSession, DWORD dwCharID, DWORD dwValue,
                                      WORD w1, WORD w2, WORD w3, WORD w4)
{
    TrainingCampMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return;
    pMember->m_dw08 = dwValue;
    pMember->m_aw0C[0] = w1;
    pMember->m_aw0C[1] = w2;
    pMember->m_aw0C[2] = w3;
    pMember->m_aw0C[3] = w4;
    CMsg* pMsg = NewNotifyMsg(13);
    *pMsg << GetID() << dwCharID << dwValue << w1 << w2 << w3 << w4;
    Broadcast(pMsg, dwExceptSession);
}

void CTrainingCamp::RefreshMemberStatus(DWORD dwCharID, BYTE byType, DWORD dwValue,
                                        CUser* pUser)
{
    TrainingCampMember* pMember;
    char szSql[0xC00];
    switch (byType)
    {
    case 1:
        pMember = FindMember(dwCharID);
        if (pMember)
        {
            if (dwValue == 0)
            {
                if (!pUser)
                    BSAssertFail();
                pMember->m_pUser = pUser;
            }
            else if (dwValue == 1)
            {
                pMember->m_pUser = NULL;
            }
        }
        break;
    case 2:
    case 6:
        pMember = FindMember(dwCharID);
        if (pMember)
        {
            if (StringCchPrintfA(szSql, 0xC00,
                                 "{CALL _TRAINING_CAMP_REFRESHMEMBERSTATUS (%d, %d, %d, %d)}",
                                 GetID(), dwCharID, byType, dwValue) != 0)
            {
                BSAssertFail();
            }
            CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(), 7, szSql,
                                                 1, NULL);
            if (byType == 2)
                TrainingCampMemberRecord_SetMaxLevel(pMember->m_pRecord, (BYTE)dwValue);
            else
                TrainingCampMemberRecord_SetCurLevel(pMember->m_pRecord, (BYTE)dwValue);
        }
        break;
    case 3:
        pMember = FindMember(dwCharID);
        // BUG(SM-275): the honor points are cut to the low byte of the value.
        if (pMember)
            TrainingCampMemberRecord_SetHonorPoint(pMember->m_pRecord, (BYTE)dwValue);
        break;
    case 7:
        pMember = FindMember(dwCharID);
        if (pMember)
            TrainingCampMemberRecord_SetRefObjID(pMember->m_pRecord, dwValue);
        break;
    default:
        BSAssertFail();
        break;
    }

    CMsg* pMsg = NewNotifyMsg(5);
    *pMsg << GetID() << dwCharID << byType << dwValue;
    Broadcast(pMsg, 0);
}

WORD CTrainingCamp::DBDelMember(BYTE byReason, DWORD dwCampID, DWORD dwCharID)
{
    if (!dwCampID || !dwCharID)
        return 5;
    TrainingCampMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return TRAINING_CAMP_ERR_NO_MEMBER;
    if (pMember->IsLeader())
        return TRAINING_CAMP_ERR_IS_LEADER;

    int anRet[2] = { 0, 0 };
    if (byReason != 3)
    {
        BYTE byKicked = 0;
        if (byReason == 2)
        {
            CUser* pLeader =
                GetSingleton_CClientManager()->FindUserByCharName(m_pLeader->CharName);
            if (pLeader && pLeader->GetCampRegID())
                return TRAINING_CAMP_ERR_LEADER_RECRUITING;
            byKicked = 1;
        }
        char szSql[0xC00];
        if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _TRAINING_CAMP_DELMEMBER (%d, %d, %d)}",
                             dwCampID, dwCharID, byKicked) != 0)
        {
            BSAssertFail();
            return 5;
        }
        if (ShardExec(szSql, 3, anRet) & ~1)
            return 2;
        if (anRet[0] <= 0)
        {
            if (anRet[0] > -2000)
                return anRet[0] == -1 ? TRAINING_CAMP_ERR_NO_MEMBER : 2;
            if (anRet[0] == -2000)
                return TRAINING_CAMP_ERR_NO_CAMP;
            if (anRet[0] == -2001)
                return TRAINING_CAMP_ERR_NOT_LEADER;
            return 2;
        }
    }

    if (pMember->m_pUser)
        pMember->m_pUser->SetTrainingCampID(0);
    TrainingCampMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        BSAssertFail();
    m_mapMember.erase(it);
    PoolObject_FreePtr(pMember);
    return 1;
}

WORD CTrainingCamp::DBMemberGraduate(DWORD dwCampID, DWORD dwCharID, int nHonor)
{
    if (!dwCampID || !dwCharID)
        return 5;
    TrainingCampMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return TRAINING_CAMP_ERR_NO_MEMBER;
    if (pMember->IsLeader())
        return TRAINING_CAMP_ERR_IS_LEADER;

    int anRet[2] = { 0, 0 };
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _TRAINING_CAMP_MEMBER_GRADUATE (%d, %d, %d)}",
                         dwCampID, dwCharID, nHonor) != 0)
    {
        BSAssertFail();
        return 5;
    }
    if (CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(), 7, szSql, 3,
                                             anRet) & ~1)
    {
        return 2;
    }
    int nRet = anRet[0];
    if (nRet <= 0)
    {
        if (nRet > -2000)
            return nRet == -1 ? TRAINING_CAMP_ERR_NO_MEMBER : 2;
        if (nRet == -2000)
            return TRAINING_CAMP_ERR_NO_CAMP;
        if (nRet < -2003)
            return 2;
        if (nRet <= -2002)
            return TRAINING_CAMP_ERR_NO_MEMBER;
        if (nRet == -2001)
            return TRAINING_CAMP_ERR_NOT_LEADER;
        return 2;
    }

    if (pMember->IsTrainee())
    {
        TrainingCampRecord_SetGraduateCount(m_pCamp, m_pCamp->GraduateCount + 1);
        if (pMember->m_pRecord->CharJoinedLevel <= 30)
        {
            TrainingCampRecord_SetEvaluationPoint(m_pCamp, m_pCamp->EvaluationPoint + nHonor);
            Timestamp_SetLocalTime(&m_pCamp->LatestEvaluationDate);
            // The leader gets the full honor of the graduate, every mentor a tenth (float 0.1,
            // truncated).
            DWORD dwHonor = (DWORD)pMember->GetGraduateHonor();
            int nMentorHonor = (int)(__int64)((double)dwHonor * (double)0.1f);
            for (TrainingCampMemberMap::iterator it = m_mapMember.begin();
                 it != m_mapMember.end(); ++it)
            {
                TrainingCampMember* pOther = it->second;
                if (!pOther || pOther->GetCharID() == pMember->GetCharID())
                    continue;
                if (pOther->IsLeader())
                {
                    TrainingCampMemberRecord_SetHonorPoint(
                        pOther->m_pRecord, pOther->m_pRecord->HonorPoint + (int)dwHonor);
                }
                else if (pOther->IsMentor())
                {
                    TrainingCampMemberRecord_SetHonorPoint(
                        pOther->m_pRecord, pOther->m_pRecord->HonorPoint + nMentorHonor);
                }
            }
        }
    }

    if (pMember->m_pUser)
        pMember->m_pUser->SetTrainingCampID(0);
    TrainingCampMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        BSAssertFail();
    m_mapMember.erase(it);
    PoolObject_FreePtr(pMember);
    return 1;
}

WORD CTrainingCamp::UpdateTextFields(std::string strTitle, std::string strComment)
{
    if (!m_pCamp)
        return 3;
    if (strTitle.size() == 0 || strTitle.size() > 0x80 || strComment.size() == 0 ||
        strComment.size() > 0x800)
    {
        return TRAINING_CAMP_ERR_TEXT_LENGTH;
    }
    TrainingCampRecord_SetCommentTitle(m_pCamp, strTitle);
    TrainingCampRecord_SetComment(m_pCamp, strComment);
    std::string strSql;
    if (!m_pCamp->BuildUpdate(strSql, 4))
        return 2;
    if (CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(), 7,
                                             strSql.c_str(), 0, NULL) & ~1)
    {
        return 2;
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------
// CTrainingCampManager

CTrainingCampManager::CTrainingCampManager()
    : TrainingCampRegMgr("TrainingCampRegData")
{
    if (g_pTrainingCampManager)
        BSAssertFail();
    g_pTrainingCampManager = this;
}

CTrainingCampManager::~CTrainingCampManager()
{
    m_mapCharCamp.clear();
    g_pTrainingCampManager = NULL;
}

BOOL CTrainingCampManager::Load()
{
    if (!CTrainingCampMgrCommon::Load())
        return FALSE;
    for (TrainingCampMap::iterator itCamp = m_mapCamp.begin(); itCamp != m_mapCamp.end();
         ++itCamp)
    {
        CTrainingCamp* pCamp = (CTrainingCamp*)itCamp->second;
        if (!pCamp)
            continue;
        for (TrainingCampMemberMap::iterator it = pCamp->m_mapMember.begin();
             it != pCamp->m_mapMember.end(); ++it)
        {
            m_mapCharCamp.insert(TrainingCampCharMap::value_type(it->first, pCamp));
        }
    }
    if (!TrainingCamp_DBIsHonorUpdateNeeded())
        TrainingCamp_DBUpdateHonorRank();
    return TRUE;
}

BOOL CTrainingCampManager::Reload()
{
    m_mapCharCamp.clear();
    if (!ClearAndLoad())
        exit(0);
    CMsg* pMsg = NewNotifyMsg(8);
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToAllGameServers(pMsg);
    return TRUE;
}

CTrainingCampCommon* CTrainingCampManager::AddCamp(CInstanceTrainingCamp* pRecord)
{
    if (!pRecord)
        return NULL;
    CTrainingCamp* pCamp = new CTrainingCamp;
    if (!pCamp->Load(pRecord))
    {
        delete pCamp;
        return NULL;
    }
    if (m_mapCamp.find(pRecord->ID) != m_mapCamp.end())
        BSAssertFail();
    m_mapCamp.insert(TrainingCampMap::value_type(pRecord->ID, pCamp));
    for (TrainingCampMemberMap::iterator it = pCamp->m_mapMember.begin();
         it != pCamp->m_mapMember.end(); ++it)
    {
        m_mapCharCamp.insert(TrainingCampCharMap::value_type(it->first, pCamp));
    }
    return pCamp;
}

void CTrainingCampManager::InsertCharCampMap(DWORD dwCharID, CTrainingCamp* pCamp)
{
    m_mapCharCamp.insert(TrainingCampCharMap::value_type(dwCharID, pCamp));
}

void CTrainingCampManager::EraseCharCampMap(DWORD dwCharID)
{
    TrainingCampCharMap::iterator it = m_mapCharCamp.find(dwCharID);
    if (it != m_mapCharCamp.end())
        m_mapCharCamp.erase(it);
}

CTrainingCamp* CTrainingCampManager::FindCampByChar(DWORD dwCharID)
{
    TrainingCampCharMap::iterator it = m_mapCharCamp.find(dwCharID);
    if (it == m_mapCharCamp.end())
        return NULL;
    return it->second;
}

CTrainingCampCommon* TrainingCampMgr_FindCampByChar(DWORD dwCharID,
                                                    CTrainingCampManager* pManager)
{
    return pManager->FindCampByChar(dwCharID);
}

BOOL CTrainingCampManager::IsLeaderRecruiting(DWORD dwCampID)
{
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
        return FALSE;
    TrainingCampMember* pLeader = pCamp->FindMember(pCamp->m_pLeader->CharID);
    std::string strName(pLeader->GetName());
    CUser* pUser = GetSingleton_CClientManager()->FindUserByCharName(strName);
    if (!pUser)
        return FALSE;
    return pUser->GetCampRegID() != 0;
}

WORD CTrainingCampManager::DeleteCamp(DWORD dwCampID, DWORD dwCharID)
{
    TrainingCampMap::iterator itCamp = m_mapCamp.find(dwCampID);
    if (itCamp == m_mapCamp.end())
        return TRAINING_CAMP_ERR_NO_CAMP;
    CTrainingCamp* pCamp = (CTrainingCamp*)itCamp->second;
    // BUG(SM-276): the members leave the character map before the DB delete, also when it fails.
    for (TrainingCampMemberMap::iterator it = pCamp->m_mapMember.begin();
         it != pCamp->m_mapMember.end(); ++it)
    {
        TrainingCampCharMap::iterator itChar = m_mapCharCamp.find(it->first);
        if (itChar != m_mapCharCamp.end())
            m_mapCharCamp.erase(itChar);
    }
    WORD wResult = DBDeleteCamp(dwCharID, pCamp);
    if (wResult == 1)
    {
        delete itCamp->second;
        m_mapCamp.erase(itCamp);
    }
    return wResult;
}

WORD CTrainingCampManager::DBDeleteCamp(DWORD dwCharID, CTrainingCamp* pCamp)
{
    if (!pCamp->m_pCamp)
        return 3;
    DWORD dwCampID = pCamp->m_pCamp->ID;
    if (!dwCampID)
        return 2;
    int anRet[2] = { 0, 0 };
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _TRAINING_CAMP_DELCAMP (%d, %d)}", dwCampID,
                         dwCharID) != 0)
    {
        BSAssertFail();
        return 2;
    }
    if (ShardExec(szSql, 3, anRet) & ~1)
        return 2;
    if (anRet[0] <= 0)
    {
        if (anRet[0] == -2000)
            return TRAINING_CAMP_ERR_NOT_LEADER;
        return anRet[0] == -1000 ? TRAINING_CAMP_ERR_NO_CAMP : 2;
    }
    for (TrainingCampMemberMap::iterator it = pCamp->m_mapMember.begin();
         it != pCamp->m_mapMember.end(); ++it)
    {
        TrainingCampMember* pMember = it->second;
        if (pMember->m_pUser)
            pMember->m_pUser->SetTrainingCampID(0);
        PoolObject_FreePtr(pMember);
    }
    pCamp->m_mapMember.clear();
    return 1;
}

WORD CTrainingCampManager::KickMember(BYTE byReason, DWORD dwCampID, DWORD dwCharID)
{
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
        return TRAINING_CAMP_ERR_NO_CAMP;
    WORD wResult = pCamp->DBDelMember(byReason, dwCampID, dwCharID);
    if (wResult == 1)
    {
        EraseCharCampMap(dwCharID);
        NotifyMemberLeft(3, dwCampID, dwCharID, byReason);
    }
    return wResult;
}

WORD CTrainingCampManager::GraduateMember(DWORD dwCampID, DWORD dwCharID, BYTE byReason,
                                          int nHonor)
{
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
        return TRAINING_CAMP_ERR_NO_CAMP;
    WORD wResult = pCamp->DBMemberGraduate(dwCampID, dwCharID, nHonor);
    if (wResult == 1)
    {
        EraseCharCampMap(dwCharID);
        NotifyMemberLeft(4, dwCampID, dwCharID, byReason);
    }
    return wResult;
}

WORD CTrainingCampManager::AddMember(DWORD dwCampID, CUser* pUser)
{
    CDBRecordList listBuffs;
    if (!pUser)
        BSAssertFail();
    DWORD dwCharID = pUser->GetCharID();
    if (!dwCharID)
    {
        BSAssertFail();
        return 2;
    }
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
    {
        BSAssertFail();
        return TRAINING_CAMP_ERR_NO_CAMP;
    }
    WORD wResult = TrainingCamp_DBAddMember(dwCampID, dwCharID);
    if (wResult != 1)
        return wResult;
    CInstanceTrainingCampMember* pRecord = TrainingCamp_DBLoadMember(dwCharID, dwCampID);
    if (!pRecord)
    {
        BSAssertFail();
        return 2;
    }
    if (pRecord->MemberClass != TRAINING_CAMP_CLASS_MENTOR &&
        !TrainingCamp_DBLoadMemberBuffs(dwCampID, &listBuffs, dwCharID))
    {
        BSAssertFail();
    }
    pUser->SetTrainingCampID(dwCampID);
    pCamp->AddMember(0, pRecord, pUser, &listBuffs);
    InsertCharCampMap(dwCharID, pCamp);
    NotifyMemberAdded(21, dwCampID, pRecord);
    pCamp->RefreshMemberStatus(dwCharID, 1, 0, pUser);
    if (IsLeaderRecruiting(dwCampID))
        RebuildListMsgs();
    return 1;
}

void CTrainingCampManager::ProcessMentorJob(trs_mentor_job* pJob, BYTE byAnswer)
{
    CUser* pApplicant = FindUserByJID(pJob->m_dwRequesterJID);
    CUser* pLeader = FindUserByJID(pJob->m_dwTargetJID);
    TrainingCampRegData* pReg = Find(pJob->m_Req.m_dwRegID);

    if (byAnswer == 0 || byAnswer == 2)
    {
        if (pApplicant)
            SendMentorAnswer(pApplicant, byAnswer);
        if (pReg)
            pReg->m_bPending = 0;
        return;
    }
    if (byAnswer != 1)
    {
        BSAssertFail();
        return;
    }

    if (!pApplicant || !pLeader)
    {
        CUser* pOther = pApplicant ? pApplicant : pLeader;
        if (pOther)
            SendMentorError(pOther, TRAINING_CAMP_ERR_NO_USER);
        return;
    }
    // BUG(SM-273): the failures below leave the entry's pending flag set.
    if (!pReg)
    {
        SendMentorError(pApplicant, TRAINING_CAMP_ERR_NO_REG);
        SendMentorError(pLeader, TRAINING_CAMP_ERR_NO_REG);
        return;
    }
    CTrainingCamp* pCamp = FindTrainingCamp(pLeader->GetTrainingCampID());
    if (!pCamp)
    {
        SendMentorError(pApplicant, TRAINING_CAMP_ERR_NO_CAMP);
        SendMentorError(pLeader, TRAINING_CAMP_ERR_NO_CAMP);
        return;
    }
    WORD wResult = AddMember(pLeader->GetTrainingCampID(), pApplicant);
    if (wResult != 1)
    {
        SendMentorError(pApplicant, wResult);
        SendMentorError(pLeader, wResult);
        return;
    }

    pReg->m_bPending = 0;
    SendMentorAnswer(pApplicant, byAnswer);
    SendMentorAnswer(pLeader, byAnswer);
    BYTE byFlags = pReg->m_byFlags;
    BOOL bTraineeFull = TRUE;
    if (byFlags & 1)
        bTraineeFull = pCamp->IsTraineeFull();
    if ((!(byFlags & 2) || pCamp->IsMentorFull()) && bTraineeFull)
    {
        Unregister(pLeader->GetJID(), pJob->m_Req.m_dwRegID);
        pLeader->SetCampRegID(0);
    }
    RebuildListMsgs();
}

// ---------------------------------------------------------------------------------------------
// 0x7C80

void CTrainingCampManager::OnRequest7C80(CMsg* pMsg)
{
    switch (pMsg->ReadForwardOnly<BYTE>())
    {
    case 0:
        OnCreateCamp(pMsg);
        break;
    case 1:
        OnDisbandCamp(pMsg);
        break;
    case 2:
        OnAddMember(pMsg);
        break;
    case 3:
        OnKickMember(pMsg);
        break;
    case 4:
        OnGraduateMember(pMsg);
        break;
    case 7:
        OnUpdateTextFields(pMsg);
        break;
    case 16:
        OnRegisterRecruit(pMsg);
        break;
    case 17:
        OnUpdateRecruit(pMsg);
        break;
    case 18:
        OnUnregisterRecruit(pMsg);
        break;
    case 19:
        OnRecruitListPage(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CTrainingCampManager::OnCreateCamp(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID = 0, dwJID = 0, dwCharID = 0;
    *pMsg >> dwRequestID >> dwJID >> dwCharID;
    WORD wResult;
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        wResult = TRAINING_CAMP_ERR_NO_LEADER;
    }
    else
    {
        DWORD dwCampID = 0;
        wResult = TrainingCamp_DBCreateCamp(&dwCampID, dwCharID);
        if (wResult == 1)
        {
            CInstanceTrainingCamp* pRecord;
            CTrainingCamp* pCamp;
            if (!dwCampID || !(pRecord = TrainingCamp_DBLoadCamp(dwCampID)) ||
                !(pCamp = (CTrainingCamp*)AddCamp(pRecord)))
            {
                BSAssertFail();
                wResult = 2;
            }
            else
            {
                if (!pCamp->FindMember(dwCharID))
                    BSAssertFail();
                pUser->SetTrainingCampID(dwCampID);
                NotifyCamp(0, dwCampID);
                CMsg* pAck = NewAckMsg(dwRequestID);
                *pAck << (BYTE)1 << dwCampID;
                SendAndRelease(dwSessionID, pAck);
                pCamp->RefreshMemberStatus(dwCharID, 1, 0, pUser);
                return;
            }
        }
    }
    pMsg->SkipRemaining();
    SendAckError(dwSessionID, dwRequestID, wResult);
}

void CTrainingCampManager::OnDisbandCamp(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwCampID, dwCharID;
    *pMsg >> dwRequestID >> dwCampID >> dwCharID;
    WORD wResult;
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
    {
        wResult = TRAINING_CAMP_ERR_NO_CAMP;
    }
    else
    {
        TrainingCampMember* pMember = pCamp->FindMember(dwCharID);
        if (!pMember)
        {
            wResult = TRAINING_CAMP_ERR_NO_MEMBER;
        }
        else if (!pMember->IsLeader())
        {
            wResult = TRAINING_CAMP_ERR_NOT_LEADER;
        }
        else
        {
            if (IsLeaderRecruiting(dwCampID))
                RebuildListMsgs();
            wResult = DeleteCamp(dwCampID, dwCharID);
            if (wResult == 1)
            {
                NotifyCamp(1, dwCampID);
                CMsg* pAck = NewAckMsg(dwRequestID);
                *pAck << (BYTE)1;
                SendAndRelease(dwSessionID, pAck);
                return;
            }
        }
    }
    SendAckError(dwSessionID, dwRequestID, wResult);
}

void CTrainingCampManager::OnAddMember(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    CDBRecordList listBuffs;
    DWORD dwRequestID, dwCampID, dwJID;
    *pMsg >> dwRequestID >> dwCampID >> dwJID;
    WORD wResult;
    CUser* pUser = FindUserByJID(dwJID);
    CTrainingCamp* pCamp = NULL;
    CInstanceTrainingCampMember* pRecord = NULL;
    DWORD dwCharID = 0;
    if (!pUser)
    {
        wResult = TRAINING_CAMP_ERR_NO_MEMBER;
    }
    else if (!(pCamp = FindTrainingCamp(dwCampID)))
    {
        wResult = TRAINING_CAMP_ERR_NO_CAMP;
    }
    else if (!(dwCharID = pUser->GetCharID()))
    {
        BSAssertFail();
        wResult = 2;
    }
    else if ((wResult = TrainingCamp_DBAddMember(dwCampID, dwCharID)) == 1)
    {
        pRecord = TrainingCamp_DBLoadMember(dwCharID, dwCampID);
        if (!pRecord)
        {
            BSAssertFail();
            wResult = 2;
        }
    }
    if (!pRecord)
    {
        pMsg->SkipRemaining();
        SendAckError(dwSessionID, dwRequestID, wResult);
        return;
    }

    if (pRecord->MemberClass != TRAINING_CAMP_CLASS_MENTOR &&
        !TrainingCamp_DBLoadMemberBuffs(dwCampID, &listBuffs, dwCharID))
    {
        BSAssertFail();
    }
    pUser->SetTrainingCampID(dwCampID);
    pCamp->AddMember(1, pRecord, pUser, &listBuffs);
    InsertCharCampMap(dwCharID, pCamp);
    NotifyMemberAdded(2, dwCampID, pRecord);
    CMsg* pAck = NewAckMsg(dwRequestID);
    *pAck << (BYTE)1;
    SendAndRelease(dwSessionID, pAck);
    pCamp->RefreshMemberStatus(dwCharID, 1, 0, pUser);

    if (!IsLeaderRecruiting(dwCampID))
        return;
    // The leader's board entry goes when the camp has no room left for what it asks for.
    TrainingCampMember* pLeader = pCamp->FindMember(pCamp->m_pLeader->CharID);
    std::string strName(pLeader->GetName());
    CUser* pLeaderUser = GetSingleton_CClientManager()->FindUserByCharName(strName);
    if (pLeaderUser)
    {
        TrainingCampRegData* pReg = Find(pLeaderUser->GetCampRegID());
        if (pReg)
        {
            BYTE byFlags = pReg->m_byFlags;
            BOOL bTraineeFull = TRUE;
            if (byFlags & 1)
                bTraineeFull = pCamp->IsTraineeFull();
            if ((!(byFlags & 2) || pCamp->IsMentorFull()) && bTraineeFull)
            {
                Unregister(pLeaderUser->GetJID(), pLeaderUser->GetCampRegID());
                pLeaderUser->SetCampRegID(0);
            }
        }
    }
    RebuildListMsgs();
}

void CTrainingCampManager::OnKickMember(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwCampID, dwCharID;
    BYTE byReason;
    *pMsg >> dwRequestID >> dwCampID >> dwCharID >> byReason;
    CMsg* pAck = NewAckMsg(dwRequestID);
    WORD wResult = KickMember(byReason, dwCampID, dwCharID);
    if (wResult == 1)
    {
        *pAck << (BYTE)1;
        if (IsLeaderRecruiting(dwCampID))
            RebuildListMsgs();
    }
    else
    {
        *pAck << (BYTE)2 << wResult;
    }
    SendAndRelease(dwSessionID, pAck);
}

void CTrainingCampManager::OnGraduateMember(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwCampID, dwCharID;
    BYTE byReason;
    int nHonor;
    *pMsg >> dwRequestID >> dwCampID >> dwCharID >> byReason >> nHonor;
    CMsg* pAck = NewAckMsg(dwRequestID);
    WORD wResult = GraduateMember(dwCampID, dwCharID, byReason, nHonor);
    if (wResult == 1)
    {
        *pAck << (BYTE)1;
        if (IsLeaderRecruiting(dwCampID))
            RebuildListMsgs();
    }
    else
    {
        *pAck << (BYTE)2 << wResult;
    }
    SendAndRelease(dwSessionID, pAck);
}

void CTrainingCampManager::OnUpdateTextFields(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    std::string strTitle;
    std::string strComment;
    DWORD dwRequestID, dwCampID;
    *pMsg >> dwRequestID >> dwCampID >> strTitle >> strComment;
    WORD wResult;
    CTrainingCamp* pCamp = FindTrainingCamp(dwCampID);
    if (!pCamp)
    {
        wResult = TRAINING_CAMP_ERR_NO_CAMP;
    }
    else
    {
        wResult = pCamp->UpdateTextFields(strTitle, strComment);
        if (wResult == 1)
        {
            NotifyTextUpdated(dwCampID, strTitle, strComment);
            CMsg* pAck = NewAckMsg(dwRequestID);
            *pAck << (BYTE)1;
            SendAndRelease(dwSessionID, pAck);
            return;
        }
    }
    SendAckError(dwSessionID, dwRequestID, wResult);
}

void CTrainingCampManager::OnRegisterRecruit(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwJID;
    *pMsg >> dwRequestID >> dwJID;
    TrainingCampRegData data;
    DWORD dwRegID = 0;
    CMsg* pAck = NewAckMsg(dwRequestID);
    if (!pAck)
        BSAssertFail();

    WORD wResult;
    CTrainingCamp* pCamp;
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        wResult = TRAINING_CAMP_ERR_NO_USER;
    }
    else if (!pUser->GetTrainingCampID())
    {
        wResult = TRAINING_CAMP_ERR_NO_CAMP;
    }
    else if (pUser->GetCampRegID())
    {
        wResult = TRAINING_CAMP_ERR_REGISTERED;
    }
    else if (!(pCamp = FindTrainingCamp(pUser->GetTrainingCampID())))
    {
        wResult = TRAINING_CAMP_ERR_NO_CAMP;
    }
    else
    {
        data.ReadFromMsg(pMsg);
        if ((data.m_byFlags & 1) && pCamp->IsTraineeFull())
        {
            wResult = TRAINING_CAMP_ERR_TRAINEE_FULL;
        }
        else if ((data.m_byFlags & 2) && pCamp->IsMentorFull())
        {
            wResult = TRAINING_CAMP_ERR_MENTOR_FULL;
        }
        else if (Register(pCamp, dwJID, &data, &dwRegID))
        {
            pUser->SetCampRegID(dwRegID);
            *pAck << (BYTE)1 << dwRegID;
            SendAndRelease(dwSessionID, pAck);
            RebuildListMsgs();
            return;
        }
        else
        {
            wResult = 2;
        }
    }
    pMsg->SkipRemaining();
    SendAckErrorTo(dwSessionID, pAck, wResult);
}

void CTrainingCampManager::OnUpdateRecruit(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwJID;
    *pMsg >> dwRequestID >> dwJID;
    TrainingCampRegData data;
    CMsg* pAck = NewAckMsg(dwRequestID);
    if (!pAck)
        BSAssertFail();

    WORD wResult;
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        wResult = TRAINING_CAMP_ERR_NO_USER;
    }
    else if (!pUser->GetCampRegID())
    {
        wResult = TRAINING_CAMP_ERR_NO_REG;
    }
    else
    {
        data.ReadFromMsg(pMsg);
        if (Update(&data, dwJID, pUser->GetCampRegID()))
        {
            *pAck << (BYTE)1 << pUser->GetCampRegID();
            SendAndRelease(dwSessionID, pAck);
            RebuildListMsgs();
            return;
        }
        wResult = 2;
    }
    pMsg->SkipRemaining();
    SendAckErrorTo(dwSessionID, pAck, wResult);
}

void CTrainingCampManager::OnUnregisterRecruit(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwJID, dwRegID;
    *pMsg >> dwRequestID >> dwJID >> dwRegID;
    CMsg* pAck = NewAckMsg(dwRequestID);
    if (!pAck)
        BSAssertFail();

    WORD wResult;
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        wResult = TRAINING_CAMP_ERR_NO_USER;
    }
    else if (Unregister(dwJID, dwRegID))
    {
        pUser->SetCampRegID(0);
        *pAck << (BYTE)1 << dwRegID;
        SendAndRelease(dwSessionID, pAck);
        RebuildListMsgs();
        return;
    }
    else
    {
        wResult = 2;
    }
    pMsg->SkipRemaining();
    SendAckErrorTo(dwSessionID, pAck, wResult);
}

void CTrainingCampManager::OnRecruitListPage(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    BYTE byPage = 0;
    const TrainingCampRegData* pOwn = NULL;
    DWORD dwRequestID, dwJID;
    *pMsg >> dwRequestID >> dwJID >> byPage;
    CMsg* pAck = NewAckMsg(dwRequestID);
    if (!pAck)
        BSAssertFail();

    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
    {
        pMsg->SkipRemaining();
        SendAckErrorTo(dwSessionID, pAck, TRAINING_CAMP_ERR_NO_USER);
        return;
    }

    // The entry of the user, or the one of its camp's leader, goes in front of the page.
    DWORD dwRegID = pUser->GetCampRegID();
    if (!dwRegID && pUser->GetTrainingCampID())
    {
        CTrainingCamp* pCamp = FindTrainingCamp(pUser->GetTrainingCampID());
        if (pCamp)
        {
            std::string strName(pCamp->m_pLeader->CharName);
            CUser* pLeader = GetSingleton_CClientManager()->FindUserByCharName(strName);
            if (pLeader)
                dwRegID = pLeader->GetCampRegID();
        }
    }
    if (dwRegID)
    {
        DataMap::iterator it = m_mapData.find(dwRegID);
        if (it == m_mapData.end())
        {
            pMsg->SkipRemaining();
            SendAckErrorTo(dwSessionID, pAck, TRAINING_CAMP_ERR_NO_CAMP);
            return;
        }
        pOwn = it->second;
        if (!pOwn)
            BSAssertFail();
    }

    DWORD dwPages = (DWORD)m_vecListMsg.size();
    if (!dwPages)
    {
        *pAck << (BYTE)1 << (BYTE)0 << (BYTE)0 << (BYTE)0;
    }
    else
    {
        if (dwPages >= 0xFF)
            BSAssertFail();
        if (dwPages <= byPage)
            byPage = 0;
        CMsg* pPage = m_vecListMsg.at(byPage);
        if (pPage->m_nCapacity >= MSG_HEADER_SIZE)
            pPage->m_wReadPos = MSG_HEADER_SIZE;
        BYTE byCount = (BYTE)pPage->GetOpcode();
        if (pOwn)
        {
            ++byCount;
            *pAck << (BYTE)1 << (BYTE)dwPages << byPage << byCount;
            pOwn->WriteEntryToMsg(pAck);
        }
        else
        {
            *pAck << (BYTE)1 << (BYTE)dwPages << byPage << byCount;
        }
        pAck->CopyPayloadFrom(pPage, 1);
    }
    SendAndRelease(dwSessionID, pAck);
}

// ---------------------------------------------------------------------------------------------
// 0x7C81

void CTrainingCampManager::OnRequest7C81(CMsg* pMsg)
{
    switch (pMsg->ReadForwardOnly<BYTE>())
    {
    case 5:
        OnRefreshMemberStatus(pMsg);
        break;
    case 6:
        OnRelayCampText(pMsg);
        break;
    case 13:
        OnUpdateMemberStats(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CTrainingCampManager::OnRefreshMemberStatus(CMsg* pMsg)
{
    CTrainingCamp* pCamp = FindTrainingCamp(pMsg->ReadForwardOnly<DWORD>());
    if (!pCamp)
        return;
    DWORD dwCharID, dwValue;
    BYTE byType;
    *pMsg >> dwCharID >> byType >> dwValue;
    pCamp->RefreshMemberStatus(dwCharID, byType, dwValue, NULL);
}

void CTrainingCampManager::OnRelayCampText(CMsg* pMsg)
{
    CTrainingCamp* pCamp = FindTrainingCamp(pMsg->ReadForwardOnly<DWORD>());
    if (pCamp)
        RelayCampText(pMsg, pCamp);
}

void CTrainingCampManager::OnUpdateMemberStats(CMsg* pMsg)
{
    CTrainingCamp* pCamp = FindTrainingCamp(pMsg->ReadForwardOnly<DWORD>());
    if (!pCamp)
        return;
    DWORD dwCharID, dwValue;
    WORD w1, w2, w3, w4;
    *pMsg >> dwCharID >> dwValue >> w1 >> w2 >> w3 >> w4;
    pCamp->UpdateMemberStats(pMsg->m_dwSessionID, dwCharID, dwValue, w1, w2, w3, w4);
}

void CTrainingCampManager::RelayCampText(CMsg* pMsg, CTrainingCamp* pCamp)
{
    if (!pCamp->m_pCamp)
    {
        BSAssertFail();
        return;
    }
    std::string strFirst;
    std::string strSecond;
    BYTE byType;
    *pMsg >> byType >> strFirst >> strSecond;
    CMsg* pNotify = NewNotifyMsg(6);
    *pNotify << pCamp->m_pCamp->ID << byType << strFirst.c_str() << strSecond.c_str();
    Broadcast(pNotify, 0);
}

// ---------------------------------------------------------------------------------------------
// Applications and logout

WORD CTrainingCampManager::OnApplyToRecruit(DWORD dwJID, DWORD dwOpcode, CMsg* pMsg)
{
    CUser* pUser = FindUserByJID(dwJID);
    if (!pUser)
        return 2;
    trs_mentor_job* pJob = trs_mentor_job_Alloc();
    WORD wResult;
    if ((WORD)dwOpcode != MSG_TRAINING_CAMP_APPLY_REQ)
    {
        BSAssertFail();
        wResult = 2;
    }
    else if (pUser->GetTrainingCampID())
    {
        wResult = TRAINING_CAMP_ERR_IN_CAMP;
    }
    else
    {
        TrainingCampApplyReq* pReq = &pJob->m_Req;
        pReq->m_dwRegID = 0;
        pReq->m_dw04 = 0;
        pReq->m_by08 = 0;
        pReq->m_byLevel = 0;
        pReq->m_dw0C = 0;
        pReq->m_strText.clear();
        pReq->ReadFromMsg(pMsg);
        TrainingCampRegData* pReg = Find(pReq->m_dwRegID);
        if (!pReg)
            wResult = TRAINING_CAMP_ERR_NO_REG;
        else
        {
            if (!pReg->m_pCamp)
                BSAssertFail();
            CUser* pLeader;
            // Up to level 39 a trainee, 40..59 a mentor.
            if (pReq->m_byLevel <= 39)
            {
                if (pReg->m_pCamp->IsTraineeFull())
                    wResult = TRAINING_CAMP_ERR_TRAINEE_FULL;
                else if (!(pReg->m_byFlags & 1))
                    wResult = TRAINING_CAMP_ERR_REG_CLASS;
                else
                    wResult = 1;
            }
            else if (pReq->m_byLevel > 59)
            {
                wResult = 3;
            }
            else if (pReg->m_pCamp->IsMentorFull())
            {
                wResult = TRAINING_CAMP_ERR_MENTOR_FULL;
            }
            else if (!(pReg->m_byFlags & 2))
            {
                wResult = TRAINING_CAMP_ERR_REG_CLASS;
            }
            else
            {
                wResult = 1;
            }

            if (wResult == 1 && pReg->m_bPending)
            {
                wResult = TRAINING_CAMP_ERR_REG_PENDING;
            }
            else if (wResult == 1 &&
                     !(pLeader = GetSingleton_CClientManager()->FindUserByJID(pReg->m_dwJID)))
            {
                wResult = TRAINING_CAMP_ERR_NO_LEADER;
            }
            else if (wResult == 1)
            {
                pReg->m_bPending = 1;
                pJob->Init(dwJID, dwOpcode, pReg->m_dwJID, 15.0f, 0, 0);
                pJob->m_dwTargetSessionID = pLeader->GetSessionID();
                if (GetSingleton_COverlappedJobMgr()->StartJob(pJob, pUser, pMsg))
                    return 1;
                pJob->Release();
                return 2;
            }
        }
    }
    pJob->Release();
    return wResult;
}

void CTrainingCampManager::UnregisterOnLogout(CUser* pUser)
{
    if (Unregister(pUser->GetJID(), pUser->GetCampRegID()))
    {
        RebuildListMsgs();
        return;
    }
    LogPrintf(0x1000000, "Failed to unregister training camp by Char[%s] logout!",
              pUser->GetCharName());
}

// ---------------------------------------------------------------------------------------------
// DB procedures

WORD TrainingCamp_DBCreateCamp(DWORD* pdwCampID, DWORD dwCharID)
{
    *pdwCampID = 0;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _TRAINING_CAMP_CREATE (%d)}", dwCharID) != 0)
    {
        BSAssertFail();
        return 2;
    }
    int anRet[2] = { 0, 0 };
    if (ShardExec(szSql, 3, anRet) & ~1)
        return TRAINING_CAMP_ERR_DB;
    int nRet = anRet[0];
    if (nRet > 0)
    {
        *pdwCampID = nRet;
        return 1;
    }
    switch (nRet)
    {
    case -7:
        return TRAINING_CAMP_ERR_DB;
    case -6:
        return 2;
    case -5:
    case -3:
    case -2005:
        return TRAINING_CAMP_ERR_DB_STATE;
    case -2:
        return TRAINING_CAMP_ERR_NAME;
    case -1:
        return TRAINING_CAMP_ERR_NO_MEMBER;
    default:
        LogPrintf(0x2000001, "Create TrainingCamp Failed!!! [UNKNOWN REASON:%d]", nRet);
        return 2;
    }
}

WORD TrainingCamp_DBAddMember(DWORD dwCampID, DWORD dwCharID)
{
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _TRAINING_CAMP_ADDMEMBER (%d,%d)}", dwCampID,
                         dwCharID) != 0)
    {
        BSAssertFail();
        return 2;
    }
    int anRet[2] = { 0, 0 };
    if (ShardExec(szSql, 3, anRet) & ~1)
        return TRAINING_CAMP_ERR_DB_STATE;
    int nRet = anRet[0];
    if (nRet > 0)
        return 1;
    switch (nRet)
    {
    case -5:
    case -2005:
        return TRAINING_CAMP_ERR_DB_STATE;
    case -2:
        return TRAINING_CAMP_ERR_NAME;
    case -1:
    case -1001:
        return TRAINING_CAMP_ERR_NO_MEMBER;
    case -11:
        return TRAINING_CAMP_ERR_MENTOR_FULL;
    case -12:
        return TRAINING_CAMP_ERR_TRAINEE_FULL;
    case -1000:
        return TRAINING_CAMP_ERR_NO_CAMP;
    default:
        return 2;
    }
}

BOOL TrainingCamp_DBIsHonorUpdateNeeded()
{
    int anRet[2] = { 0, 0 };
    int nResult =
        ShardExec("{?=CALL _TRAINING_CAMP_STARTUP_HONOR_UPDATE_NEEDED}", 3, anRet);
    if (nResult < 0)
    {
        LogPrintf(0x1000000, "Failed To Check Last Honor Ranking Update Date! Query Error: %d!",
                  nResult);
        return FALSE;
    }
    return anRet[0] == 0;
}

void TrainingCamp_DBUpdateHonorRank()
{
    int anRet[2] = { 0, 0 };
    int nResult = ShardExec("{?=CALL _TRAINING_CAMP_UPDATEHONORRANK}", 3, anRet);
    if (nResult < 0)
    {
        LogPrintf(0x1000000, "Failed To Update Training Camp Honor Ranking! Query Error: %d!",
                  nResult);
        return;
    }
    if (anRet[0] <= 0)
    {
        LogPrintf(0x1000000, "Failed To Update Training Camp Honor Ranking! Result: %d",
                  anRet[0]);
        return;
    }
    Broadcast(NewNotifyMsg(11), 0);
}
