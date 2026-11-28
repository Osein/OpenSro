#include "ShardManager/Guild.h"
#include "ShardManager/CrestUploader.h"
#include "ShardManager/TrainingCamp.h"
#include "ShardManager/Vote.h"

#include <strsafe.h>

#include "BSLib/Util/TimeSpan.h"
#include "BSNet/NetEngine.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"

// Connection index of the guild statements (the original passes 7 everywhere).
#define GUILD_DB_CONN   7

const int g_anGuildLevelUpGP[6] = { 50, 5400, 50400, 135000, 378000, 0 };

static CDBSessionMgr* ShardDBSession()
{
    return GetSingleton_CShardDB()->GetSession();
}

// The UPDATE of one record group on the guild connection: 0 when nothing could be built, -1 when
// the statement failed, 1 on success.
static int ExecRecordUpdate(CDBRecord* pRecord, int nGroup)
{
    std::string strSql;
    if (!pRecord->BuildUpdate(strSql, nGroup))
        return 0;
    if (CDatabase::ExecDirectWithReturnValue(ShardDBSession(), GUILD_DB_CONN, strSql.c_str(), 0,
                                             NULL) & ~1)
        return -1;
    return 1;
}

// A stored procedure with an int return value: FALSE when the statement failed.
static BOOL ExecProcedure(const char* pszSql, int* pnResult)
{
    __int64 llValue = 0;
    if (CDatabase::ExecDirectWithReturnValue(ShardDBSession(), GUILD_DB_CONN, pszSql,
                                             DBEXEC_RETURN_INT | 1, &llValue) & ~1)
        return FALSE;
    *pnResult = (int)llValue;
    return TRUE;
}

static CMsg* AllocNotify(BYTE bSubType)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_NOTIFY);
    pMsg->WriteBytes(&bSubType, 1);
    return pMsg;
}

static int BroadcastToGameServers(CMsg* pMsg)
{
    if (!g_pMainProcess)
        BSAssertFail();
    return g_pMainProcess->BroadcastToGameServers(pMsg, 0);
}

// The ack goes to the session the request came from and is released.
static void SendAck(DWORD dwSessionID, CMsg* pAck)
{
    g_pNetEngine->SendMsg(dwSessionID, pAck);
    g_pNetEngine->ReleaseMsg(pAck);
}

static void SendAckResult(DWORD dwSessionID, DWORD dwRequestID, WORD wResult)
{
    CMsg* pAck = CGuildManager::AllocAck(dwRequestID);
    BYTE bResult;
    if (wResult == GUILD_OK)
    {
        bResult = 1;
        pAck->WriteBytes(&bResult, 1);
    }
    else
    {
        bResult = 2;
        pAck->WriteBytes(&bResult, 1);
        pAck->WriteBytes(&wResult, 2);
    }
    SendAck(dwSessionID, pAck);
}

// ---------------------------------------------------------------------------------------------
// Guild war record helpers

void GuildWarRecord_Update(CInstanceGuildWar* pWar, DWORD dwGuild1,
                           const GUILD_WAR_REQUEST& request, DWORD dwGuild2)
{
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->VictoryPointIndex,
                       request.bVictoryPointIndex, 2);
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->LodgedGold,
                       (int)(2 * request.dwLodgedGold), 2);
    CDateTime now = CDateTime::GetCurrent();
    TIMESTAMP_STRUCT& end = pWar->WarEndTime;
    if (request.dwDuration >= 0x7FFFFFFF)
    {
        end.second = 0;
        end.minute = 0;
        end.hour = 0;
        end.day = 0;
        end.month = 0;
        end.year = 0;
    }
    else
    {
        DWORD dw = request.dwDuration;
        CTimeSpan span((LONG)((dw >> 15) & 0x1F), (dw >> 10) & 0x1F, (dw >> 20) & 0x3F,
                       dw >> 26);
        CDateTime dtEnd = now + span;
        end.year = (SQLSMALLINT)dtEnd.GetYear();
        end.month = (SQLUSMALLINT)dtEnd.GetMonth();
        end.day = (SQLUSMALLINT)dtEnd.GetDay();
        end.hour = (SQLUSMALLINT)dtEnd.GetHour();
        end.minute = (SQLUSMALLINT)dtEnd.GetMinute();
        end.second = (SQLUSMALLINT)dtEnd.GetSecond();
    }
    const CDBTableInfo& info = CInstanceGuildWar::s_TableInfo;
    DBRecord_SetColumn(pWar, info, pWar->Guild[0], (int)dwGuild1, 3);
    DBRecord_SetColumn(pWar, info, pWar->Guild[1], (int)dwGuild2, 3);
    DBRecord_SetColumn(pWar, info, pWar->PointGain[0], 0, 3);
    DBRecord_SetColumn(pWar, info, pWar->PointGain[1], 0, 3);
    DBRecord_SetColumn(pWar, info, pWar->Data1, 0, 4);
    DBRecord_SetColumn(pWar, info, pWar->Data2, 0, 4);
}

void GuildWarRecord_SetPointGain(CInstanceGuildWar* pWar, DWORD nIndex, int nValue)
{
    if (nIndex >= 2)
        BSAssertFail();
    DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->PointGain[nIndex], nValue, 3);
}

void GuildWarRecord_WriteToMsg(const CInstanceGuildWar* pWar, CMsg* pMsg)
{
    pMsg->WriteBytes(&pWar->ID, 4);
    const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
    DWORD dwEnd = 0;
    if (end.year || end.month || end.day || end.hour || end.minute || end.second)
        RefObj_PackTypeID(dwEnd, (const WORD*)&end);
    else
        dwEnd = 0;
    pMsg->WriteBytes(&dwEnd, 4);
    pMsg->WriteBytes(&pWar->VictoryPointIndex, 1);
    pMsg->WriteBytes(&pWar->LodgedGold, 4);
    pMsg->WriteBytes(&pWar->Guild[0], 4);
    pMsg->WriteBytes(&pWar->Guild[1], 4);
    pMsg->WriteBytes(&pWar->PointGain[0], 4);
    pMsg->WriteBytes(&pWar->PointGain[1], 4);
}

DWORD CCharacter_GetTrainingCampID(CCharacter* pCharacter)
{
    DWORD dwCharID = pCharacter->GetPCRecord()->CharID;
    if (!g_pTrainingCampManager)
        BSAssertFail();
    CTrainingCampCommon* pCamp = TrainingCampMgr_FindCampByChar(dwCharID,
                                                                g_pTrainingCampManager);
    if (!pCamp)
        return 0;
    return pCamp->GetID();
}

// ---------------------------------------------------------------------------------------------
// CAlliance (DB side)

void CAlliance::ReleaseRecord()
{
    if (!m_pRecord)
        return;
    if (!g_pShardDB)
        BSAssertFail();
    CDBTable* pTable = g_pShardDB->GetTable(SHARD_DB_TABLE_ALLIEDCLANS);
    pTable->FreeRecord(m_pRecord);
    m_pRecord = NULL;
}

WORD CAlliance::RemoveGuildSlot(DWORD dwGuildID)
{
    CInstanceAlliedClan* pRecord = m_pRecord;
    BYTE i = 0;
    while (pRecord->Ally[i] != (int)dwGuildID)
    {
        if (++i >= 8)
            return GUILD_ERR_NOT_IN_UNION;
    }
    if (i >= 8)
        BSAssertFail();
    DBRecord_SetColumn(pRecord, CInstanceAlliedClan::s_TableInfo, pRecord->Ally[i], 0, 2);
    if (ExecRecordUpdate(m_pRecord, 2) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CAlliance::DBRemove()
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _AllianceRemove (%d)}", m_pRecord->ID))
    {
        BSAssertFail();
        return GUILD_ERR_NO_RECORD;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult) || nResult <= 0)
        return GUILD_ERR_FAILED;
    for (int i = 0; i < 8; ++i)
    {
        DWORD dwGuildID = m_pRecord->Ally[i];
        if (dwGuildID)
        {
            CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
            if (pGuild)
                pGuild->ClearAllianceID();
        }
    }
    ReleaseRecord();
    return GUILD_OK;
}

WORD CAlliance::SetCrestRev(int nCrestRev)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceAlliedClanData::CurCrestRev, nCrestRev);
    if (nCrestRev)
        m_pRecord->SetColumn(&InstanceAlliedClanData::LastCrestRev, nCrestRev);
    if (ExecRecordUpdate(m_pRecord, 3) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

// ---------------------------------------------------------------------------------------------
// CGuild

void CGuild::OnMemberMsgRelay(CMsg* pMsg)
{
    if (!m_pRecord)
    {
        BSAssertFail();
        return;
    }
    std::string strFrom;
    std::string strText;
    BYTE bType;
    *pMsg >> bType;
    *pMsg >> strFrom;
    *pMsg >> strText;
    CMsg* pNotify = AllocNotify(7);
    pNotify->WriteBytes(&m_pRecord->ID, 4);
    pNotify->WriteBytes(&bType, 1);
    *pNotify << strFrom.c_str();
    *pNotify << strText;
    BroadcastToGameServers(pNotify);
}

int CGuild::RefreshMemberStatus(DWORD dwCharID, BYTE bType, DWORD dwValue, CUser* pUser)
{
    if (bType == 1)
    {
        CGuildMember* pMember = FindMember(dwCharID);
        if (pMember)
        {
            char szSql[0xC00];
            if (StringCchPrintfA(szSql, 0xC00, "{CALL _GUILD_REFRESHMEMBERSTATUS (%d, %d, %d, %d)}",
                                 GetID(), dwCharID, 1, dwValue))
            {
                BSAssertFail();
            }
            if (!g_pShardDB)
                BSAssertFail();
            CDatabase::ExecDirectWithReturnValue(g_pShardDB->GetSession(), GUILD_DB_CONN, szSql,
                                                 1, NULL);
            pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::CharLevel, (BYTE)dwValue);
        }
    }
    else if (bType == 2)
    {
        CGuildMember* pMember = FindMember(dwCharID);
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
                if (m_pLockMember == pMember)
                {
                    ResetLockMember();
                    GetSingleton_CGuildManager();
                    CGuildManager::SendMemberOnlineState(GetID(), dwCharID, 2);
                }
                pMember->m_pUser = NULL;
            }
        }
    }
    CMsg* pNotify = AllocNotify(6);
    DWORD dwGuildID = GetID();
    pNotify->WriteBytes(&dwGuildID, 4);
    pNotify->WriteBytes(&dwCharID, 4);
    pNotify->WriteBytes(&bType, 1);
    pNotify->WriteBytes(&dwValue, 4);
    return BroadcastToGameServers(pNotify);
}

WORD CGuild::DBDeleteMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason)
{
    if (!dwGuildID || !dwCharID)
        return GUILD_ERR_ARGUMENT;
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (pMember->IsMaster())
        return GUILD_ERR_MASTER_CANNOT_LEAVE;
    if (bReason != 3)
    {
        char szSql[0xC00];
        if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _GUILD_DELMEMBER (%d, %d)}", dwGuildID,
                             dwCharID))
        {
            BSAssertFail();
            return GUILD_ERR_ARGUMENT;
        }
        int nResult;
        if (!ExecProcedure(szSql, &nResult))
            return GUILD_ERR_FAILED;
        if (nResult <= 0)
        {
            if (nResult == -1 || nResult == -1001)
                return GUILD_ERR_MEMBER_NOT_FOUND;
            if (nResult == -1000)
                return GUILD_ERR_GUILD_NOT_FOUND;
            return GUILD_ERR_FAILED;
        }
    }
    if (pMember->m_pUser)
        pMember->m_pUser->SetGuildID(0);
    m_mapMember.erase(m_mapMember.find(dwCharID));
    GuildMember_FreePtr(pMember);
    return GUILD_OK;
}

WORD CGuild::DBDeleteGuild(DWORD dwCharID)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!m_pRecord->ID)
        return GUILD_ERR_FAILED;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _GUILD_DELETEGUILD (%d, %d)}", m_pRecord->ID,
                         dwCharID))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult <= 0)
    {
        if (nResult == -2000)
            return GUILD_ERR_NOT_MASTER;
        return nResult != -1000 ? GUILD_ERR_FAILED : GUILD_ERR_GUILD_NOT_FOUND;
    }
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        if (pMember->m_pUser)
            pMember->m_pUser->SetGuildID(0);
        GuildMember_FreePtr(pMember);
    }
    m_mapMember.clear();
    return GUILD_OK;
}

WORD CGuild::DonateGP(int nAmount, DWORD dwCharID)
{
    if (nAmount <= 0)
        return GUILD_ERR_FAILED;
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!m_pRecord->ID)
        return GUILD_ERR_FAILED;
    CGuildMember* pMember = FindMember(dwCharID);
    if (!pMember)
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (m_pRecord->Lvl > 5)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, m_pRecord->GatheredSP + nAmount);
    pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::GP_Donation,
                                  pMember->m_pRecord->GP_Donation + nAmount);
    if (ExecRecordUpdate(m_pRecord, 3) != 1 || ExecRecordUpdate(pMember->m_pRecord, 2) != 1)
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    return GUILD_OK;
}

WORD CGuild::LevelUp()
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    BYTE bLevel = m_pRecord->Lvl;
    if (bLevel == 5)
        return GUILD_ERR_MAX_LEVEL;
    if (bLevel < 1)
        return GUILD_ERR_FAILED;
    int nCost = g_anGuildLevelUpGP[bLevel];
    if (m_pRecord->GatheredSP < nCost)
        return GUILD_ERR_NOT_ENOUGH_GP;
    m_pRecord->SetColumn(&InstanceGuildData::Lvl, (BYTE)(bLevel + 1));
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, m_pRecord->GatheredSP - nCost);
    if (ExecRecordUpdate(m_pRecord, -1) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::UpdateNotice(const std::string& strTitle, const std::string& strNotice)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    if (!strTitle.size() || strTitle.size() > 0x80)
        return GUILD_ERR_NOTICE_TITLE;
    if (!strNotice.size() || strNotice.size() > 0x800)
        return GUILD_ERR_NOTICE_BODY;
    m_pRecord->SetColumn(&InstanceGuildData::MasterCommentTitle, strTitle);
    m_pRecord->SetColumn(&InstanceGuildData::MasterComment, strNotice);
    if (ExecRecordUpdate(m_pRecord, 5) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::SetAllianceID(CAlliance* pAlliance, BYTE bSave)
{
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, pAlliance->m_pRecord->ID);
    if (bSave != 1)
        return GUILD_OK;
    if (ExecRecordUpdate(m_pRecord, 4) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::LeaveUnion(BYTE* pbResult)
{
    if (!m_pRecord)
        return GUILD_ERR_NO_RECORD;
    DWORD dwAllianceID = m_pRecord->Alliance;
    CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
    if (!pAlliance)
        return GUILD_ERR_NOT_IN_UNION;
    DWORD dwMasterGuildID = pAlliance->GetMasterGuildID();
    WORD wResult = pAlliance->RemoveGuildSlot(GetID());
    if (wResult != GUILD_OK)
        return wResult;
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, 0);
    if (dwMasterGuildID == GetID() || pAlliance->CountGuilds() <= 1)
    {
        *pbResult = 3;
        return GUILD_OK;
    }
    if (ExecRecordUpdate(m_pRecord, 4) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::ClearAllianceID()
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Alliance, 0);
    return GUILD_OK;
}

WORD CGuild::DelegateMaster(CInstanceGuildMember* pNewMaster,
                            CInstanceGuildMember* pOldMaster)
{
    if (!m_pRecord || !pOldMaster || !pNewMaster)
        return GUILD_ERR_FAILED;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00,
                         "{?=CALL _Guild_Delegate_Master (%d, %d, %d, %d, %d, %d, %d)}",
                         GetID(), pOldMaster->CharID, pNewMaster->CharID, 0, 10, 1, 0))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult <= 0)
    {
        if (nResult != -2)
            BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    if (pOldMaster->MemberClass)
        BSAssertFail();
    if (!pNewMaster->MemberClass)
        BSAssertFail();
    pOldMaster->SetColumn(&InstanceGuildMemberData::MemberClass, (BYTE)10);
    pNewMaster->SetColumn(&InstanceGuildMemberData::MemberClass, (BYTE)0);
    pOldMaster->SetColumn(&InstanceGuildMemberData::SiegeAuthority, (BYTE)0);
    pNewMaster->SetColumn(&InstanceGuildMemberData::SiegeAuthority, (BYTE)1);
    pOldMaster->SetColumn(&InstanceGuildMemberData::Permission, 0);
    pNewMaster->SetColumn(&InstanceGuildMemberData::Permission, -1);
    m_pMasterRecord = pNewMaster;
    return GUILD_OK;
}

WORD CGuild::UpdateMemberPermissions(const GuildPermissionVector& vecPermission)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    for (GuildPermissionVector::const_iterator it = vecPermission.begin();
         it != vecPermission.end(); ++it)
    {
        CGuildMember* pMember = FindMember(it->first);
        if (!pMember)
        {
            BSAssertFail();
            continue;
        }
        pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::Permission, (int)it->second);
        if (ExecRecordUpdate(pMember->m_pRecord, 5) != 1)
            BSAssertFail();
    }
    return GUILD_OK;
}

WORD CGuild::DBIsMasterCandidates(CVote* pVote)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    if (HasMasterCandidate(pVote->GetKind()) == 1)
        return GUILD_ERR_VOTE_RUNNING;
    if (!g_pShardDB)
        BSAssertFail();
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        DWORD dwCharID = pMember->GetCharID();
        if (pMember->m_pUser)
        {
            pVote->AddChoice(dwCharID);
            continue;
        }
        char szSql[0xC00];
        if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Guild_Is_Master_Candidators(%d)}",
                             dwCharID))
        {
            BSAssertFail();
            return GUILD_ERR_FAILED;
        }
        int nResult;
        if (!ExecProcedure(szSql, &nResult) || nResult == -1000)
            return GUILD_ERR_FAILED;
        if (nResult == 0)
            continue;
        if (nResult != 1)
            return GUILD_ERR_FAILED;
        if (m_pMasterRecord->CharID != (int)dwCharID)
            pVote->AddChoice(dwCharID);
    }
    PushCandidate(pVote);
    return GUILD_OK;
}

void CGuild::AddBooty(int nGold)
{
    m_pRecord->SetColumn(&InstanceGuildData::Booty, m_pRecord->Booty + nGold);
}

WORD CGuild::DBClearBooty()
{
    if (!g_pShardDB)
        BSAssertFail();
    if (!g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDWAR))
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Booty, 0);
    if (ExecRecordUpdate(m_pRecord, 6) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::Rename(const std::string& strName)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::Name, std::string(strName));
    m_pRecord->ClearAllDirty();
    if (!g_pGuildManager)
        BSAssertFail();
    CGuildManager::SendGuildRenamed(strName, GetID());
    return GUILD_OK;
}

WORD CGuild::MemberRename(DWORD dwCharID, const std::string& strName)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GuildMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    CGuildMember* pMember = it->second;
    pMember->m_pRecord->SetColumn(&InstanceGuildMemberData::CharName,
                                  std::string(strName.c_str()));
    pMember->m_pRecord->ClearAllDirty();
    if (!g_pGuildManager)
        BSAssertFail();
    CGuildManager::SendMemberRenamed(strName, GetID(), dwCharID);
    return GUILD_OK;
}

WORD CGuild::SetMemberOnline(DWORD dwCharID, BYTE bType)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    if (m_mapMember.find(dwCharID) == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (GetLevel() < 2)
        return GUILD_ERR_LEVEL_TOO_LOW;
    if (bType == 1)
    {
        if (m_pLockMember)
        {
            return (WORD)((m_pLockMember->GetCharID() != dwCharID) +
                          GUILD_ERR_LOCK_HELD_BY_SELF);
        }
        if (!MemberHasPermission8(dwCharID))
            return GUILD_ERR_NOT_MASTER;
        SetLockMember(dwCharID);
        return GUILD_OK;
    }
    if (bType == 2)
    {
        if (m_pLockMember && m_pLockMember->GetCharID() == dwCharID)
        {
            ResetLockMember();
            return GUILD_OK;
        }
        return GUILD_ERR_LOCK_NOT_HELD;
    }
    return GUILD_ERR_ARGUMENT;
}

WORD CGuild::DBUpdateMember(DWORD dwCharID, const std::string& strNickname)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GuildMemberMap::iterator it = m_mapMember.find(dwCharID);
    if (it == m_mapMember.end())
        return GUILD_ERR_MEMBER_NOT_FOUND;
    if (!g_pShardDB)
        BSAssertFail();
    if (!g_pShardDB->GetTable(SHARD_DB_TABLE_GUILDMEMBER))
        return GUILD_ERR_FAILED;
    CGuildMember* pMember = it->second;
    pMember->SetNickname(strNickname);
    if (ExecRecordUpdate(pMember->m_pRecord, 9) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::SetCrestRev(int nCrestRev)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    m_pRecord->SetColumn(&InstanceGuildData::CurCrestRev, nCrestRev);
    if (nCrestRev)
        m_pRecord->SetColumn(&InstanceGuildData::LastCrestRev, nCrestRev);
    if (ExecRecordUpdate(m_pRecord, 7) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

WORD CGuild::OffsetGP(int nOffset, BOOL bSave)
{
    if (nOffset + m_pRecord->GatheredSP < 0)
    {
        LogPrintf(0x2000001, "Offset GuildGP tryng to go minus!! GUILDID[%d] CurGP[%d] Offset[%d]",
                  m_pRecord->ID, m_pRecord->GatheredSP, nOffset);
        return GUILD_ERR_FAILED;
    }
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, nOffset + m_pRecord->GatheredSP);
    if (bSave)
    {
        if (ExecRecordUpdate(m_pRecord, 3) != 1)
            return GUILD_ERR_FAILED;
    }
    return GUILD_OK;
}

WORD CGuild::SetMercenaryAttr(BYTE bAbility, DWORD* pdwGold, DWORD* pdwGP)
{
    if (!m_pRecord)
        return GUILD_ERR_FAILED;
    GetMercenaryAbilityCost(pdwGP, bAbility, pdwGold);
    if ((DWORD)m_pRecord->GatheredSP < *pdwGP)
        return GUILD_ERR_NOT_ENOUGH_GP;
    BYTE bAttr;
    if (bAbility)
    {
        if (bAbility & m_pRecord->MercenaryAttr)
            return GUILD_ERR_ABILITY_SET;
        bAttr = bAbility | m_pRecord->MercenaryAttr;
    }
    else
    {
        bAttr = 0;
    }
    m_pRecord->SetColumn(&InstanceGuildData::MercenaryAttr, bAttr);
    m_pRecord->SetColumn(&InstanceGuildData::GatheredSP, (int)(m_pRecord->GatheredSP - *pdwGP));
    if (ExecRecordUpdate(m_pRecord, 3) != 1 || ExecRecordUpdate(m_pRecord, 8) != 1)
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

// Members whose record says guild master (GuildMember_IsOffline) are left out.
void CGuild::BroadcastToOnlineMembers(transaction* pTransaction)
{
    for (GuildMemberMap::iterator it = m_mapMember.begin(); it != m_mapMember.end(); ++it)
    {
        CGuildMember* pMember = it->second;
        if (pMember->m_pRecord && !pMember->IsMaster())
        {
            if (!g_pClientManager)
                BSAssertFail();
            CUser* pUser = g_pClientManager->FindUserByCharName(pMember->m_pRecord->CharName);
            if (pUser)
                pTransaction->AddParticipantJID(pUser->GetJID());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// CGuildManager

// The static instance (0x89A398) and its pointer.
CGuildManager* g_pGuildManager = NULL;

CGuildManager::CGuildManager()
{
    if (g_pGuildManager)
        BSAssertFail();
    g_pGuildManager = this;
}

CGuildManager::~CGuildManager()
{
    g_pGuildManager = NULL;
}

BOOL CGuildManager::Load()
{
    return CGuildMgrCommon::Load();
}

BOOL CGuildManager::Reset()
{
    // MgrCommon_Reset_441900 (shared with the training camp manager)
    Clear();
    if (!Load())
        exit(0);
    CMsg* pMsg = AllocNotify(32);
    if (!g_pMainProcess)
        BSAssertFail();
    g_pMainProcess->BroadcastToAllGameServers(pMsg);
    return TRUE;
}

CGuildCommon* CGuildManager::CreateGuild(CInstanceGuild* pRecord)
{
    if (!pRecord)
        return NULL;
    CGuild* pGuild = new CGuild;
    if (!pGuild->Initialize(pRecord))
    {
        delete pGuild;
        return NULL;
    }
    if (m_mapGuild.find(pRecord->ID) != m_mapGuild.end())
        BSAssertFail();
    m_mapGuild.insert(GuildMap::value_type(pRecord->ID, pGuild));
    return pGuild;
}

CMsg* CGuildManager::AllocAck(DWORD dwRequestID)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(GUILD_MSG_ACK);
    pMsg->WriteBytes(&dwRequestID, 4);
    return pMsg;
}

WORD CGuildManager::DeleteGuild(DWORD dwGuildID, DWORD dwCharID)
{
    GuildMap::iterator it = m_mapGuild.find(dwGuildID);
    if (it == m_mapGuild.end())
        return GUILD_ERR_GUILD_NOT_FOUND;
    WORD wResult = static_cast<CGuild*>(it->second)->DBDeleteGuild(dwCharID);
    if (wResult == GUILD_OK)
    {
        if (it->second)
            delete it->second;
        m_mapGuild.erase(it);
    }
    return wResult;
}

namespace
{
struct GUILD_REQUEST_HANDLER
{
    BYTE bSubType;
    void (CGuildManager::*pfnHandler)(CMsg* pMsg);
};

const GUILD_REQUEST_HANDLER s_aGuildRequestHandler[] =
{
    { 0, &CGuildManager::OnCreateGuild },
    { 1, &CGuildManager::OnDisbandGuild },
    { 2, &CGuildManager::OnJoinGuild },
    { 3, &CGuildManager::OnLeaveGuild },
    { 8, &CGuildManager::OnDonateSP },
    { 9, &CGuildManager::OnLevelUpGuild },
    { 10, &CGuildManager::OnUpdateNotice },
    { 11, &CGuildManager::OnUnionInvite },
    { 16, &CGuildManager::OnUnionLeave },
    { 17, &CGuildManager::OnUnionKick },
    { 19, &CGuildManager::OnDelegateMaster },
    { 20, &CGuildManager::OnUpdatePermissions },
    { 21, &CGuildManager::OnMasterVoteStart },
    { 30, &CGuildManager::OnGuildWarRecordUpdate },
    { 36, &CGuildManager::OnMemberOnlineState },
    { 37, &CGuildManager::OnMemberInfoUpdate },
    { 38, &CGuildManager::OnGuildCrestRequest38 },
    { 39, &CGuildManager::OnGuildCrestUpdate },
    { 40, &CGuildManager::OnUnionCrestRequest40 },
    { 41, &CGuildManager::OnUnionCrestUpdate },
    { 42, &CGuildManager::OnDonateSP42 },
    { 43, &CGuildManager::OnGiveMercenaryAbility },
    { 44, &CGuildManager::OnUpdateGuildSP },
    { 45, &CGuildManager::OnOffsetGP },
    { 46, &CGuildManager::OnSetSiegeAuthority },
    { 50, &CGuildManager::OnGuildWarBegin },
    { 53, &CGuildManager::OnOffsetGPAndLevel },
};
}

void CGuildManager::OnGuildRequest(CMsg* pMsg)
{
    BYTE bSubType = pMsg->ReadForwardOnly<BYTE>();
    for (int i = 0; i < _countof(s_aGuildRequestHandler); ++i)
    {
        if (s_aGuildRequestHandler[i].bSubType == bSubType)
        {
            (this->*s_aGuildRequestHandler[i].pfnHandler)(pMsg);
            return;
        }
    }
    BSAssertFail();
}

void CGuildManager::CheckGuildWarTimeouts()
{
    static DWORD s_dwNow = 0;
    static DWORD s_dwEnd = 0;
    PackedDateTime_GetLocal(&s_dwNow);
    GuildWarMap::iterator it = m_mapGuildWar.begin();
    while (it != m_mapGuildWar.end())
    {
        CInstanceGuildWar* pWar = it->second;
        const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
        if ((end.year || end.month || end.day || end.hour || end.minute || end.second) &&
            pWar->PointGain[0] != pWar->PointGain[1] &&
            (RefObj_PackTypeID(s_dwEnd, (const WORD*)&end),
             PackedDateTime_Compare(&s_dwEnd, &s_dwNow) >= 0))
        {
            ++it;
            DWORD dwWarID = pWar->ID;
            DWORD dwWinner;
            if (pWar->PointGain[0] <= pWar->PointGain[1])
                dwWinner = pWar->Guild[1];
            else
                dwWinner = pWar->Guild[0];
            if (DBEndGuildWar(dwWinner, dwWarID) == GUILD_OK)
                SendGuildWarEnded(dwWarID, dwWinner);
            else
                BSAssertFail();
        }
        else
        {
            ++it;
        }
    }
}

int CGuildManager::BroadcastGuildWarState()
{
    DWORD dwNow = 0;
    PackedDateTime_GetLocal(&dwNow);
    CMsg* pMsg = AllocNotify(31);
    pMsg->WriteBytes(&dwNow, 4);
    return BroadcastToGameServers(pMsg);
}

void CGuildManager::OnGuildNotify(CMsg* pMsg)
{
    BYTE bSubType = pMsg->ReadForwardOnly<BYTE>();
    if (bSubType == 6)
        OnRefreshMemberStatus(pMsg);
    else if (bSubType == 7)
        OnMemberRelay(pMsg);
    else if (bSubType == 29)
        OnGuildWarCombatResult(pMsg);
    else
        BSAssertFail();
}

void CGuildManager::OnRefreshMemberStatus(CMsg* pMsg)
{
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
        return;
    DWORD dwCharID;
    BYTE bType;
    DWORD dwValue;
    *pMsg >> dwCharID;
    *pMsg >> bType;
    *pMsg >> dwValue;
    pGuild->RefreshMemberStatus(dwCharID, bType, dwValue, NULL);
}

void CGuildManager::OnMemberRelay(CMsg* pMsg)
{
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (pGuild)
        pGuild->OnMemberMsgRelay(pMsg);
}

void CGuildManager::OnDisbandGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else if (!pGuild->FindMember(dwCharID))
    {
        wResult = GUILD_ERR_MEMBER_NOT_FOUND;
    }
    else if (!pGuild->FindMember(dwCharID)->IsMaster())
    {
        wResult = GUILD_ERR_NOT_MASTER;
    }
    else
    {
        wResult = GUILD_OK;
        if (pGuild->GetAllianceID())
        {
            DWORD dwAllianceID = pGuild->GetAllianceID();
            if (!GetSingleton_CGuildManager()->FindAlliance(dwAllianceID))
            {
                wResult = GUILD_ERR_NOT_IN_UNION;
            }
            else
            {
                BYTE bLeave = 1;
                wResult = pGuild->LeaveUnion(&bLeave);
                if (wResult == GUILD_OK)
                {
                    SendUnionGuildRemoved(dwAllianceID, dwGuildID, 1);
                    if (bLeave == 3)
                    {
                        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
                        SendUnionDisbanded(dwAllianceID);
                    }
                }
            }
        }
        if (wResult == GUILD_OK)
        {
            wResult = DeleteGuild(dwGuildID, dwCharID);
            if (wResult == GUILD_OK)
                SendDisbandResult(dwGuildID);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnLeaveGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    BYTE bReason;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> bReason;
    WORD wResult = RemoveMember(dwGuildID, dwCharID, bReason);
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnDonateSP(CMsg* pMsg)
{
    DWORD dwGuildID, dwCharID;
    int nAmount;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> nAmount;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->DonateGP(nAmount, dwCharID);
        if (wResult == GUILD_OK)
        {
            SendDonateSPResult(dwGuildID, dwCharID, nAmount);
            return;
        }
        if (wResult == GUILD_ERR_DONATE_SILENT)
            return;
    }
    LogPrintf(0x2000001, "SP DONATION FAILED!! GUILDID[%d] CHAR[%d] STUFF[%d] ERROR[%d]",
              dwGuildID, dwCharID, nAmount, wResult);
}

void CGuildManager::OnLevelUpGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    int nCost = g_anGuildLevelUpGP[pGuild->GetLevel()];
    WORD wResult = pGuild->LevelUp();
    if (wResult != GUILD_OK)
    {
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    SendLevelUpResult(dwGuildID, pGuild->GetLevel(), pGuild->m_pRecord->GatheredSP);
    // BUG(SM-200): without a master record or with the master offline the guild has levelled
    // up (and was told so), but the GP history is skipped and the requester gets error 1.
    CInstanceGuildMember* pMaster = pGuild->m_pMasterRecord;
    CUser* pUser = NULL;
    if (pMaster)
    {
        if (!g_pClientManager)
            BSAssertFail();
        pUser = g_pClientManager->FindUserByCharName(pMaster->CharName);
    }
    if (!pUser)
    {
        BSAssertFail();
        CMsg* pAck = AllocAck(dwRequestID);
        BYTE bResult = 2;
        pAck->WriteBytes(&bResult, 1);
        pAck->WriteBytes(&wResult, 2);
        SendAck(dwSessionID, pAck);
        return;
    }
    if (DBAddGPHistory(dwGuildID, pUser->GetJID(), nCost, 5) == GUILD_OK)
        SendGuildInfo53(dwGuildID);
    else
        BSAssertFail();
    if ((BYTE)pGuild->m_listGPHistory.size() > 100)
    {
        BYTE bCount = DBDeleteGPHistory(pGuild);
        if (bCount)
            SendGPHistory54(dwGuildID, bCount);
    }
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
}

void CGuildManager::OnUpdateNotice(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    std::string strTitle;
    std::string strNotice;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> strTitle;
    *pMsg >> strNotice;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->UpdateNotice(strTitle, strNotice);
        if (wResult == GUILD_OK)
            SendNoticeUpdated(dwGuildID, strTitle, strNotice);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnUnionInvite(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwTargetGuildID, dwTargetCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwTargetGuildID;
    *pMsg >> dwTargetCharID;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    CGuild* pTarget = NULL;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!(pTarget = FindGuildT(dwTargetGuildID)))
        wResult = GUILD_ERR_TARGET_NOT_FOUND;
    else if (pTarget->GetAllianceID())
        wResult = GUILD_ERR_ALREADY_IN_UNION;
    else if (!pTarget->IsMemberMaster(dwTargetCharID))
        wResult = GUILD_ERR_TARGET_NOT_MASTER;
    else
    {
        DWORD dwAllianceID = pGuild->GetAllianceID();
        if (!dwAllianceID)
        {
            wResult = DBCreateUnion(pGuild, pTarget, &dwAllianceID);
            if (wResult == GUILD_OK)
                SendUnionCreated(dwAllianceID, pGuild->GetID(), pTarget->GetID());
        }
        else
        {
            CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
            if (!pAlliance)
            {
                wResult = GUILD_ERR_UNION_NOT_FOUND;
            }
            else
            {
                BYTE bSlot = pAlliance->FindEmptySlot();
                if (!bSlot)
                {
                    wResult = GUILD_ERR_UNION_FULL;
                }
                else
                {
                    wResult = pAlliance->AddGuild(pTarget, bSlot);
                    if (wResult == GUILD_OK)
                        SendUnionGuildAdded(dwAllianceID, pTarget->GetID(), bSlot);
                }
            }
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnUnionLeave(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID;
    *pMsg >> dwRequestID;
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwCharID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    WORD wResult;
    DWORD dwAllianceID = 0;
    BYTE bLeave = 1;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!pGuild->IsMemberMaster(dwCharID))
        wResult = GUILD_ERR_NOT_MASTER;
    else if (!(dwAllianceID = pGuild->GetAllianceID()))
        wResult = GUILD_ERR_NOT_IN_UNION;
    else
    {
        wResult = pGuild->LeaveUnion(&bLeave);
        if (wResult == GUILD_OK)
            SendUnionGuildRemoved(dwAllianceID, dwGuildID, 1);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
    if (wResult == GUILD_OK && bLeave == 3)
    {
        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
        SendUnionDisbanded(dwAllianceID);
    }
}

void CGuildManager::OnUnionKick(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID;
    *pMsg >> dwRequestID;
    DWORD dwGuildID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwCharID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwTargetGuildID = pMsg->ReadForwardOnly<DWORD>();
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    CGuild* pTarget = NULL;
    WORD wResult;
    DWORD dwAllianceID = 0;
    BYTE bLeave = 2;
    if (!pGuild)
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    else if (!pGuild->IsMemberMaster(dwCharID))
        wResult = GUILD_ERR_NOT_MASTER;
    else if (!(pTarget = GetSingleton_CGuildManager()->FindGuildT(dwTargetGuildID)))
        wResult = GUILD_ERR_TARGET_NOT_FOUND;
    else
    {
        dwAllianceID = pTarget->GetAllianceID();
        if (pGuild->GetAllianceID() != dwAllianceID)
            BSAssertFail();
        if (!GetSingleton_CGuildManager()->FindAlliance(dwAllianceID))
        {
            wResult = GUILD_ERR_KICK_UNION_NOT_FOUND;
        }
        else
        {
            wResult = pTarget->LeaveUnion(&bLeave);
            if (wResult == GUILD_OK)
                SendUnionGuildRemoved(dwAllianceID, dwTargetGuildID, 2);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
    if (wResult == GUILD_OK && bLeave == 3)
    {
        GetSingleton_CGuildManager()->DisbandUnion(dwAllianceID);
        SendUnionDisbanded(dwAllianceID);
    }
}

void CGuildManager::OnDelegateMaster(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    WORD wResult = GUILD_ERR_FAILED;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    CInstanceGuildMember* pOldMaster = NULL;
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else if ((pOldMaster = pGuild->m_pMasterRecord) != NULL)
    {
        CGuildMember* pMember = pGuild->FindMember(dwCharID);
        if (!pMember || pMember->m_pRecord == pOldMaster)
        {
            wResult = GUILD_ERR_NO_RECORD;
        }
        else
        {
            wResult = pGuild->DelegateMaster(pMember->m_pRecord, pOldMaster);
            if (wResult == GUILD_OK)
                SendMasterDelegated(dwGuildID, dwCharID, pOldMaster->CharID);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

// ---------------------------------------------------------------------------------------------
// CGuildManager: notifications 0x38F5 (SM_social2)

void CGuildManager::SendCreateResult(DWORD dwGuildID)
{
    CMsg* pMsg = AllocNotify(0);
    pMsg->WriteBytes(&dwGuildID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendDisbandResult(DWORD dwGuildID)
{
    CMsg* pMsg = AllocNotify(1);
    pMsg->WriteBytes(&dwGuildID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendJoinResult(DWORD dwGuildID, const CInstanceGuildMember* pRecord)
{
    CMsg* pMsg = AllocNotify(2);
    pMsg->WriteBytes(&dwGuildID, 4);
    GuildMemberRecord_WriteToMsg(pMsg, pRecord);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendLeaveResult(DWORD dwGuildID, DWORD dwCharID, BYTE bReason)
{
    CMsg* pMsg = AllocNotify(3);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    pMsg->WriteBytes(&bReason, 1);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendDonateSPResult(DWORD dwGuildID, DWORD dwCharID, int nAmount)
{
    CMsg* pMsg = AllocNotify(8);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    pMsg->WriteBytes(&nAmount, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendLevelUpResult(DWORD dwGuildID, BYTE bLevel, int nGatheredSP)
{
    CMsg* pMsg = AllocNotify(9);
    BYTE bKind = 0x0C;
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bKind, 1);
    pMsg->WriteBytes(&bLevel, 1);
    pMsg->WriteBytes(&nGatheredSP, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendNoticeUpdated(DWORD dwGuildID, const std::string& strTitle,
                                      const std::string& strNotice)
{
    CMsg* pMsg = AllocNotify(10);
    BYTE bKind = 0x10;
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bKind, 1);
    *pMsg << strTitle.c_str();
    *pMsg << strNotice.c_str();
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendUnionCreated(DWORD dwAllianceID, DWORD dwMasterGuildID,
                                     DWORD dwMemberGuildID)
{
    CMsg* pMsg = AllocNotify(11);
    pMsg->WriteBytes(&dwAllianceID, 4);
    pMsg->WriteBytes(&dwMasterGuildID, 4);
    pMsg->WriteBytes(&dwMemberGuildID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendUnionGuildAdded(DWORD dwAllianceID, DWORD dwGuildID, BYTE bSlot)
{
    CMsg* pMsg = AllocNotify(13);
    pMsg->WriteBytes(&dwAllianceID, 4);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bSlot, 1);
    BroadcastToGameServers(pMsg);
}

// Reason 3 is the disbanding (SendUnionDisbanded).
void CGuildManager::SendUnionGuildRemoved(DWORD dwAllianceID, DWORD dwGuildID, BYTE bReason)
{
    if (bReason == 3)
        BSAssertFail();
    CMsg* pMsg = AllocNotify(18);
    pMsg->WriteBytes(&bReason, 1);
    pMsg->WriteBytes(&dwAllianceID, 4);
    pMsg->WriteBytes(&dwGuildID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendUnionDisbanded(DWORD dwAllianceID)
{
    CMsg* pMsg = AllocNotify(18);
    BYTE bReason = 3;
    pMsg->WriteBytes(&bReason, 1);
    pMsg->WriteBytes(&dwAllianceID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendMasterDelegated(DWORD dwGuildID, DWORD dwNewMasterCharID,
                                        DWORD dwOldMasterCharID)
{
    CMsg* pMsg = AllocNotify(19);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwNewMasterCharID, 4);
    pMsg->WriteBytes(&dwOldMasterCharID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendPermissionsUpdated(const GuildPermissionVector& vecPermission,
                                           DWORD dwGuildID)
{
    CMsg* pMsg = AllocNotify(20);
    BYTE bCount = (BYTE)vecPermission.size();
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bCount, 1);
    // Guild_PermVector_Size_43E620: the loop runs over the whole vector.
    for (DWORD i = 0; i < vecPermission.size(); ++i)
    {
        pMsg->WriteBytes(&vecPermission.at(i).first, 4);
        pMsg->WriteBytes(&vecPermission.at(i).second, 4);
    }
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGuildWarBegun(const CInstanceGuildWar* pWar)
{
    if (!pWar)
    {
        BSAssertFail();
        return;
    }
    DWORD dwNow = 0;
    PackedDateTime_GetLocal(&dwNow);
    CMsg* pMsg = AllocNotify(25);
    GuildWarRecord_WriteToMsg(pWar, pMsg);
    pMsg->WriteBytes(&dwNow, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendHostileResult(DWORD dwWarID, DWORD dwGuildID)
{
    CMsg* pMsg = AllocNotify(26);
    pMsg->WriteBytes(&dwWarID, 4);
    pMsg->WriteBytes(&dwGuildID, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGuildWarEnded(DWORD dwWarID, DWORD dwWinnerGuildID)
{
    CMsg* pMsg = AllocNotify(28);
    pMsg->WriteBytes(&dwWarID, 4);
    pMsg->WriteBytes(&dwWinnerGuildID, 4);
    BroadcastToGameServers(pMsg);
}

// War, points, then guild, CharID and name of the killer and of the victim.
void CGuildManager::SendGuildWarCombatResult(const std::string& strVictimName,
                                             CGuildMember* pKiller, DWORD dwWarID, int nPoint,
                                             CGuildMember* pVictim,
                                             const std::string& strKillerName)
{
    CMsg* pMsg = AllocNotify(29);
    DWORD dwVictimCharID = pVictim->GetCharID();
    DWORD dwKillerCharID = pKiller->GetCharID();
    pMsg->WriteBytes(&dwWarID, 4);
    pMsg->WriteBytes(&nPoint, 4);
    pMsg->WriteBytes(&pKiller->m_pRecord->GuildID, 4);
    pMsg->WriteBytes(&dwKillerCharID, 4);
    *pMsg << strKillerName.c_str();
    pMsg->WriteBytes(&pVictim->m_pRecord->GuildID, 4);
    pMsg->WriteBytes(&dwVictimCharID, 4);
    *pMsg << strVictimName.c_str();
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGuildWarRecordUpdated(DWORD dwGuildID, int nBooty)
{
    CMsg* pMsg = AllocNotify(30);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&nBooty, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGuildRenamed(const std::string& strName, DWORD dwGuildID)
{
    CMsg* pMsg = AllocNotify(33);
    pMsg->WriteBytes(&dwGuildID, 4);
    *pMsg << strName.c_str();
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendMemberRenamed(const std::string& strName, DWORD dwGuildID,
                                      DWORD dwCharID)
{
    CMsg* pMsg = AllocNotify(34);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    *pMsg << strName.c_str();
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendMemberOnlineState(DWORD dwGuildID, DWORD dwCharID, BYTE bState)
{
    CMsg* pMsg = AllocNotify(36);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    pMsg->WriteBytes(&bState, 1);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendMemberInfoUpdated(const std::string& strNickname, DWORD dwGuildID,
                                          DWORD dwCharID)
{
    CMsg* pMsg = AllocNotify(37);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&dwCharID, 4);
    *pMsg << strNickname.c_str();
    BroadcastToGameServers(pMsg);
}

// Subtype 38 for a new crest, 39 when it was removed (revision 0).
void CGuildManager::SendGuildCrestResult(DWORD dwGuildID, int nCrestRev)
{
    CMsg* pMsg = AllocNotify((BYTE)((nCrestRev == 0) + 38));
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&nCrestRev, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendUnionCrestResult(DWORD dwAllianceID, int nCrestRev)
{
    CMsg* pMsg = AllocNotify((BYTE)((nCrestRev == 0) + 40));
    pMsg->WriteBytes(&dwAllianceID, 4);
    pMsg->WriteBytes(&nCrestRev, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendMercenaryAbilityResult(DWORD dwGuildID, BYTE bAttr, int nGatheredSP)
{
    CMsg* pMsg = AllocNotify(43);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bAttr, 1);
    pMsg->WriteBytes(&nGatheredSP, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGPUpdated(DWORD dwGuildID, int nGatheredSP)
{
    CMsg* pMsg = AllocNotify(44);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&nGatheredSP, 4);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGuildInfo53(DWORD dwGuildID)
{
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
        return;
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    if (!pMsg)
    {
        BSAssertFail();
        return;
    }
    pMsg->SetOpcode(GUILD_MSG_NOTIFY);
    BYTE bSubType = 53;
    pMsg->WriteBytes(&bSubType, 1);
    pGuild->WriteLastGPHistory(pMsg);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendGPHistory54(DWORD dwGuildID, BYTE bCount)
{
    if (!FindGuild(dwGuildID))
    {
        BSAssertFail();
        return;
    }
    if (!bCount)
        return;
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    if (!pMsg)
    {
        BSAssertFail();
        return;
    }
    pMsg->SetOpcode(GUILD_MSG_NOTIFY);
    BYTE bSubType = 54;
    pMsg->WriteBytes(&bSubType, 1);
    pMsg->WriteBytes(&dwGuildID, 4);
    pMsg->WriteBytes(&bCount, 1);
    BroadcastToGameServers(pMsg);
}

void CGuildManager::SendVoteStarted(CVote* pVote)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(0x3908);
    BYTE bSubType = 1;
    pMsg->WriteBytes(&bSubType, 1);
    pVote->WriteToMsg(pMsg);
    BroadcastToGameServers(pMsg);
}

// ---------------------------------------------------------------------------------------------
// Guild, alliance and guild war DB functions (SM_social2)

const DWORD g_adwGuildWarVictoryPoint[8] =
{
    0, 5000, 10000, 50000, 100000, 150000, 250000, 300000
};

void GuildWarRequest_ReadFromMsg(GUILD_WAR_REQUEST& request, CMsg* pMsg)
{
    *pMsg >> request.b00;
    *pMsg >> request.dwDuration;
    *pMsg >> request.bVictoryPointIndex;
    *pMsg >> request.dwLodgedGold;
}

// Ability 0 resets the abilities; the single abilities cost the same.
void CGuild::GetMercenaryAbilityCost(DWORD* pdwGP, BYTE bAbility, DWORD* pdwGold)
{
    switch (bAbility)
    {
    case 0:
        *pdwGold = 500000;
        *pdwGP = 10000;
        break;
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
        *pdwGold = 500000;
        *pdwGP = 50000;
        break;
    default:
        *pdwGold = 100000000;
        *pdwGP = 100000000;
        break;
    }
}

WORD CAlliance::AddGuild(CGuild* pGuild, BYTE bSlot)
{
    CInstanceAlliedClan* pRecord = m_pRecord;
    if (pRecord->Ally[bSlot])
    {
        BSAssertFail();
        LogPrintf(0x2000001, "AddAlliance error!! allianceID[%d] slot[%d] is not null",
                  m_pRecord->ID, bSlot);
        return GUILD_ERR_FAILED;
    }
    DWORD dwGuildID = pGuild->GetID();
    if (bSlot >= 8)
        BSAssertFail();
    DBRecord_SetColumn(pRecord, CInstanceAlliedClan::s_TableInfo, pRecord->Ally[bSlot],
                       (int)dwGuildID, 2);
    if (ExecRecordUpdate(m_pRecord, 2) != 1)
        return GUILD_ERR_FAILED;
    return pGuild->SetAllianceID(this, 1);
}

WORD CGuildManager::RemoveMember(DWORD dwGuildID, DWORD dwCharID, BYTE bReason)
{
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
        return GUILD_ERR_GUILD_NOT_FOUND;
    WORD wResult = pGuild->DBDeleteMember(dwGuildID, dwCharID, bReason);
    if (wResult == GUILD_OK)
    {
        SendLeaveResult(dwGuildID, dwCharID, bReason);
        return GUILD_OK;
    }
    return wResult;
}

WORD CGuildManager::IsValidGuildName(const char* pszName)
{
    if ((unsigned int)(lstrlenA(pszName) - 2) > 10)
        return 0x4C18;
    std::string strName(pszName);
    if (!g_pMainProcess)
        BSAssertFail();
    if (!CharName_IsValid(strName))
        return 0x4C19;
    if (DBIsExistingGuildName(pszName))
        return 0x4C1A;
    return GUILD_OK;
}

BOOL CGuildManager::DBIsExistingGuildName(const char* pszName)
{
    if (!pszName || !lstrlenA(pszName))
        BSAssertFail();
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Guild_IsExistingGuildName ('%s')}", pszName))
    {
        BSAssertFail();
        return FALSE;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return FALSE;
    return nResult != 0;
}

WORD CGuildManager::DBCreateGuild(DWORD dwCharID, DWORD* pdwGuildID, const char* pszName)
{
    *pdwGuildID = 0;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Guild_Create ('%s',%d)}", pszName, dwCharID))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return 0x4C1B;
    if (nResult > 0)
    {
        *pdwGuildID = nResult;
        return GUILD_OK;
    }
    switch (nResult)
    {
    case -5:
        return 0x4C1B;
    case -1000:
        return 0x4C1A;
    case -3:
        return 0x4C1C;
    case -2:
        return 0x4C12;
    case -1:
        return GUILD_ERR_MEMBER_NOT_FOUND;
    default:
        LogPrintf(0x2000001, "Create Guild Failed!!! [UNKNOWN REASON:%d]", nResult);
        return GUILD_ERR_FAILED;
    }
}

WORD CGuildManager::DBAddMember(DWORD dwGuildID, DWORD dwCharID)
{
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Guild_AddMember (%d,%d,%d,%d)}", dwGuildID,
                         dwCharID, 10, 0))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return 0x4C1C;
    if (nResult > 0)
        return GUILD_OK;
    switch (nResult)
    {
    case -1:
        return GUILD_ERR_MEMBER_NOT_FOUND;
    case -2:
        return 0x4C12;
    case -3:
        return 0x4C1C;
    case -1000:
        return GUILD_ERR_GUILD_NOT_FOUND;
    default:
        return GUILD_ERR_FAILED;
    }
}

WORD CGuildManager::DBCreateUnion(CGuild* pMaster, CGuild* pMember, DWORD* pdwAllianceID)
{
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _AllianceCreate (%d, %d)}", pMaster->GetID(),
                         pMember->GetID()))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult > 0)
    {
        *pdwAllianceID = nResult;
        CInstanceAlliedClan* pRecord = CGuildMgrCommon::LoadAllianceById(nResult);
        if (!pRecord)
        {
            BSAssertFail();
            return GUILD_ERR_FAILED;
        }
        CAlliance* pAlliance = AddAlliance(pRecord);
        pMaster->SetAllianceID(pAlliance, 0);
        pMember->SetAllianceID(pAlliance, 0);
        return GUILD_OK;
    }
    if (nResult == -2)
    {
        LogPrintf(0x1000000, "DBJOB_UpdateGuildAlliance GUILD[%d,%d]", pMaster->GetID(),
                  pMember->GetID());
    }
    else if (nResult == -1)
    {
        LogPrintf(0x1000000, "DBJOB_InsertAlliance: GUILD[%d,%d]", pMaster->GetID(),
                  pMember->GetID());
    }
    return GUILD_ERR_FAILED;
}

WORD CGuildManager::DisbandUnion(DWORD dwAllianceID)
{
    CAlliance* pAlliance = FindAlliance(dwAllianceID);
    if (!pAlliance)
        return GUILD_ERR_NOT_IN_UNION;
    pAlliance->DBRemove();
    RemoveAlliance(dwAllianceID);
    return GUILD_OK;
}

WORD CGuildManager::DBBeginGuildWar(DWORD dwGuildID1, DWORD dwGuildID2,
                                    const GUILD_WAR_REQUEST& request, CInstanceGuildWar** ppWar)
{
    *ppWar = NULL;
    CGuild* pGuild1 = FindGuildT(dwGuildID1);
    CGuild* pGuild2 = FindGuildT(dwGuildID2);
    if (!pGuild1 || !pGuild2)
        return GUILD_ERR_FAILED;
    if (pGuild1->FindHostileEntry(pGuild2->GetID()))
        return 0x4C3A;
    if (pGuild2->FindHostileEntry(pGuild1->GetID()))
        return 0x4C3A;
    CDBTable* pTable = GetSingleton_CShardDB()->GetTable(SHARD_DB_TABLE_GUILDWAR);
    CInstanceGuildWar* pWar = static_cast<CInstanceGuildWar*>(pTable->CreateRecord());
    GuildWarRecord_Update(pWar, dwGuildID1, request, dwGuildID2);
    DWORD dwMaster2 = pGuild2->m_pMasterRecord->CharID;
    DWORD dwMaster1 = pGuild1->m_pMasterRecord->CharID;
    char szSql[0xC00];
    HRESULT hr;
    if (request.dwDuration == 0x7FFFFFFF)
    {
        hr = StringCchPrintfA(szSql, 0xC00,
                              "{?=CALL _GuildWar_Begin (%d, %d, %d,NULL, %d, %d, %d, %d)}",
                              pWar->WarType, pWar->VictoryPointIndex, pWar->LodgedGold,
                              pWar->Guild[0], pWar->Guild[1], dwMaster1, dwMaster2);
    }
    else
    {
        const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
        hr = StringCchPrintfA(szSql, 0xC00,
                              "{?=CALL _GuildWar_Begin (%d, %d, %d,'%d-%d-%d %d:%d:%d', %d, "
                              "%d, %d, %d)}",
                              pWar->WarType, pWar->VictoryPointIndex, pWar->LodgedGold,
                              end.year, end.month, end.day, end.hour, end.minute, end.second,
                              pWar->Guild[0], pWar->Guild[1], dwMaster1, dwMaster2);
    }
    if (hr)
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    __int64 llValue = 0;
    if (CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(),
                                             GUILD_DB_CONN, szSql, DBEXEC_RETURN_INT | 1,
                                             &llValue) & ~1)
    {
        GetSingleton_CShardDB()->GetTable(SHARD_DB_TABLE_GUILDWAR)->FreeRecord(pWar);
        return GUILD_ERR_FAILED;
    }
    int nResult = (int)llValue;
    if (nResult > 0)
    {
        // sub_4359B0: the ID without an update group.
        DBRecord_SetColumn(pWar, CInstanceGuildWar::s_TableInfo, pWar->ID, nResult, 0);
        *ppWar = pWar;
        AddGuildWar(pWar);
        return GUILD_OK;
    }
    GetSingleton_CShardDB()->GetTable(SHARD_DB_TABLE_GUILDWAR)->FreeRecord(pWar);
    if (nResult == -3 || nResult == -2)
        return 0x4C0C;
    if (nResult == -1)
    {
        LogPrintf(0x2000001, "GuildWar A[%d] B[%d] is already in war", dwGuildID1, dwGuildID2);
        return 0x4C3A;
    }
    LogPrintf(0x2000001, "DBJOB_Begin GuildWar FAILED: GUILD[%d,%d] error_code[%d]", dwGuildID1,
              dwGuildID2, nResult);
    return GUILD_ERR_FAILED;
}

WORD CGuildManager::DBEndGuildWar(DWORD dwWinnerGuildID, DWORD dwWarID)
{
    CInstanceGuildWar* pWar = FindGuildWar(dwWarID);
    if (!pWar)
        return GUILD_ERR_FAILED;
    CGuild* pWinner = FindGuildT(dwWinnerGuildID);
    char szSql[0xC00];
    if (!pWinner ||
        StringCchPrintfA(szSql, 0xC00, "{?=CALL _GuildWar_EndOfWar (%d,%d,%d)}", dwWarID,
                         dwWinnerGuildID, pWar->LodgedGold))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return GUILD_ERR_FAILED;
    if (nResult > 0)
    {
        pWinner->AddBooty(pWar->LodgedGold);
        RemoveGuildWar(pWar);
        return GUILD_OK;
    }
    if (nResult == 0)
    {
        LogPrintf(0x2000001, " GuildWar is not exists!! WARID[%d]", dwWarID);
        return GUILD_ERR_FAILED;
    }
    if (nResult == -1 || nResult == -12345 || nResult == -2)
        return GUILD_ERR_FAILED;
    BSAssertFail();
    return GUILD_ERR_FAILED;
}

BOOL CGuildManager::AddHostileGuilds(CInstanceGuildWar* pWar)
{
    for (int i = 0; i < 2; ++i)
    {
        CGuild* pGuild = FindGuildT(pWar->Guild[i]);
        if (!pGuild || !pGuild->AddHostileGuild(pWar))
            return FALSE;
    }
    return TRUE;
}

BOOL CGuildManager::RemoveHostileGuilds(CInstanceGuildWar* pWar)
{
    for (int i = 0; i < 2; ++i)
    {
        CGuild* pGuild = FindGuildT(pWar->Guild[i]);
        if (!pGuild || !pGuild->RemoveHostileGuild(pWar))
            return FALSE;
    }
    return TRUE;
}

WORD CGuildManager::OnCombatResult(CInstanceGuildWar* pWar, CGuildMember* pKiller,
                                   CGuildMember* pVictim, int nPoint,
                                   const std::string& strKillerName,
                                   const std::string& strVictimName)
{
    if (!pWar || !pKiller || !pVictim)
        return GUILD_ERR_FAILED;
    DWORD dwWarID = pWar->ID;
    BYTE bSide = (DWORD)pKiller->m_pRecord->GuildID != (DWORD)pWar->Guild[0];
    DWORD dwVictoryPoint = 0;
    if (pWar->VictoryPointIndex)
        dwVictoryPoint = g_adwGuildWarVictoryPoint[pWar->VictoryPointIndex];
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00,
                         "{?=CALL _GuildWar_AccountCombatResult (%d,%d,%d,%d,%d,%d,%d)}",
                         dwWarID, bSide, nPoint, pKiller->m_pRecord->GuildID,
                         pKiller->GetCharID(), pVictim->m_pRecord->GuildID,
                         pVictim->GetCharID()))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return 0x4C1C;
    if (nResult < 0)
        return GUILD_ERR_FAILED;
    GuildWarRecord_SetPointGain(pWar, bSide, pWar->PointGain[bSide] + nPoint);
    CInstanceGuildMember* pKillerRecord = pKiller->m_pRecord;
    pKillerRecord->SetColumn(&InstanceGuildMemberData::Contribution,
                             pKillerRecord->Contribution + nPoint);
    pKillerRecord->SetColumn(&InstanceGuildMemberData::GuildWarKill,
                             pKillerRecord->GuildWarKill + 1);
    CInstanceGuildMember* pVictimRecord = pVictim->m_pRecord;
    pVictimRecord->SetColumn(&InstanceGuildMemberData::GuildWarKilled,
                             pVictimRecord->GuildWarKilled + 1);
    SendGuildWarCombatResult(strVictimName, pKiller, dwWarID, nPoint, pVictim, strKillerName);

    DWORD dwWinner = 0;
    const TIMESTAMP_STRUCT& end = pWar->WarEndTime;
    if (end.year && end.month && end.day)
    {
        DWORD dwNow = 0;
        PackedDateTime_GetLocal(&dwNow);
        DWORD dwEnd = 0;
        RefObj_PackTypeID(dwEnd, (const WORD*)&end);
        if (PackedDateTime_Compare(&dwEnd, &dwNow) >= 0)
        {
            if ((DWORD)pWar->PointGain[0] <= (DWORD)pWar->PointGain[1])
                dwWinner = pWar->Guild[1];
            else
                dwWinner = pWar->Guild[0];
        }
    }
    if ((int)dwVictoryPoint > 0 && (DWORD)pWar->PointGain[bSide] >= dwVictoryPoint)
        dwWinner = pWar->Guild[bSide];
    if (dwWinner)
    {
        WORD wResult = DBEndGuildWar(dwWinner, dwWarID);
        if (wResult != GUILD_OK)
        {
            BSAssertFail();
            return wResult;
        }
        SendGuildWarEnded(dwWarID, dwWinner);
    }
    return GUILD_OK;
}

WORD CGuildManager::DBAddGPHistory(DWORD dwGuildID, DWORD dwJID, int nGP, BYTE bReason)
{
    if (bReason >= 9)
    {
        BSAssertFail();
        return 0x4C0B;
    }
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        BSAssertFail();
        return GUILD_ERR_GUILD_NOT_FOUND;
    }
    if (!g_pClientManager)
        BSAssertFail();
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    if (!pUser)
    {
        BSAssertFail();
        return GUILD_ERR_MEMBER_NOT_FOUND;
    }
    char szSql[0xC00];
    memset(szSql, 0, sizeof(szSql));
    if (StringCchPrintfA(szSql, 0xC00, "{CALL _AddGPHistory ( ?, ?, %d, %d, %d, %d )}",
                         dwGuildID, pUser->GetCharID(), nGP, bReason))
    {
        BSAssertFail();
        return 0x4C08;
    }
    // Two output parameters: the result and the ID of the new row.
    DBParamList params;
    DB_PARAM param;
    memset(&param, 0, sizeof(param));
    param.nType = DBPARAM_SMALLINT;
    param.nIOType = SQL_PARAM_OUTPUT;
    params.push_back(param);
    param.nType = DBPARAM_INTEGER;
    params.push_back(param);
    int nExec = CDatabase::ExecDirectWithParams(GetSingleton_CShardDB()->GetSession(),
                                                GUILD_DB_CONN, szSql, &params);
    if (nExec & ~1)
    {
        LogPrintf(0x2000001, "Failed WriteGPHistory!!! DB Query Error( %d ): %s", nExec, szSql);
        return 0x4C08;
    }
    DBParamList::iterator itParam = params.begin();
    if (itParam->nValue < 1)
    {
        BSAssertFail();
        return 0x4C08;
    }
    ++itParam;
    int nHistoryID = itParam->nValue;
    if (!nHistoryID)
    {
        BSAssertFail();
        return 0x4C08;
    }
    memset(szSql, 0, sizeof(szSql));
    CDBTable* pTable = NULL;
    if (StringCchPrintfA(szSql, 0xC00, "WHERE ID = %d", nHistoryID) ||
        !(pTable = GetSingleton_CShardDB()->GetTable(SHARD_DB_TABLE_GPHISTORY)))
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    CDBRecordList list;
    if (!pTable->SelectWhere(szSql, 6, &list, 0) || list.empty())
    {
        BSAssertFail();
        return GUILD_ERR_FAILED;
    }
    CInstanceGPHistory* pHistory = dynamic_cast<CInstanceGPHistory*>(list.front());
    if (!pGuild->AddGPHistory(pHistory, 1))
        return GUILD_ERR_FAILED;
    return GUILD_OK;
}

BYTE CGuildManager::DBDeleteGPHistory(CGuild* pGuild)
{
    if (!pGuild)
    {
        BSAssertFail();
        return 0;
    }
    char szSql[0xC00];
    memset(szSql, 0, sizeof(szSql));
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _DeleteGPHistory (%d)}", pGuild->GetID()))
    {
        BSAssertFail();
        return 0;
    }
    int nResult = 0;
    if (!ExecProcedure(szSql, &nResult))
        return 0;
    if (nResult >= -1001 && nResult <= -1000)
    {
        BSAssertFail();
        return 0;
    }
    if ((BYTE)nResult)
        pGuild->PopGPHistory((BYTE)nResult);
    return (BYTE)nResult;
}

// ---------------------------------------------------------------------------------------------
// CGuildManager: request handlers (SM_social2)

void CGuildManager::OnCreateGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID = 0;
    DWORD dwJID = 0;
    DWORD dwCharID = 0;
    std::string strName;
    *pMsg >> dwRequestID;
    *pMsg >> dwJID;
    *pMsg >> strName;
    *pMsg >> dwCharID;
    if (!g_pClientManager)
        BSAssertFail();
    WORD wResult;
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    DWORD dwGuildID = 0;
    CGuild* pGuild = NULL;
    if (!pUser)
    {
        wResult = 0x4C0E;
    }
    else if ((wResult = IsValidGuildName(strName.c_str())) == GUILD_OK &&
             (wResult = DBCreateGuild(dwCharID, &dwGuildID, strName.c_str())) == GUILD_OK)
    {
        CInstanceGuild* pRecord = NULL;
        if (!dwGuildID || !(pRecord = CGuildMgrCommon::LoadGuildById(dwGuildID)) ||
            !(pGuild = static_cast<CGuild*>(CreateGuild(pRecord))))
        {
            BSAssertFail();
            wResult = GUILD_ERR_FAILED;
        }
    }
    if (wResult != GUILD_OK)
    {
        pMsg->SkipRemaining();
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    if (!pGuild->FindMember(dwCharID))
        BSAssertFail();
    pUser->SetGuildID(dwGuildID);
    SendCreateResult(dwGuildID);
    CMsg* pAck = AllocAck(dwRequestID);
    BYTE bResult = 1;
    pAck->WriteBytes(&bResult, 1);
    pAck->WriteBytes(&dwGuildID, 4);
    SendAck(dwSessionID, pAck);
    pGuild->RefreshMemberStatus(dwCharID, 2, 0, pUser);
}

void CGuildManager::OnJoinGuild(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwJID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwJID;
    if (!g_pClientManager)
        BSAssertFail();
    WORD wResult;
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    CGuild* pGuild = NULL;
    DWORD dwCharID = 0;
    CInstanceGuildMember* pRecord = NULL;
    if (!pUser)
    {
        wResult = GUILD_ERR_MEMBER_NOT_FOUND;
    }
    else if (!(pGuild = FindGuildT(dwGuildID)))
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else if (pGuild->IsMemberCountFull())
    {
        wResult = 0x4C13;
    }
    else if (!(dwCharID = pUser->GetCharID()))
    {
        BSAssertFail();
        wResult = GUILD_ERR_FAILED;
    }
    else if ((wResult = DBAddMember(dwGuildID, dwCharID)) == GUILD_OK &&
             !(pRecord = CGuildCommon::LoadMemberRecord(dwCharID, dwGuildID)))
    {
        BSAssertFail();
        wResult = GUILD_ERR_FAILED;
    }
    if (wResult != GUILD_OK)
    {
        pMsg->SkipRemaining();
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    pUser->SetGuildID(dwGuildID);
    pGuild->AddMember(1, pRecord, pUser);
    SendJoinResult(dwGuildID, pRecord);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    pGuild->RefreshMemberStatus(dwCharID, 2, 0, pUser);
}

void CGuildManager::OnUpdatePermissions(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    BYTE bCount;
    *pMsg >> bCount;
    GuildPermissionVector vecPermission(bCount, std::pair<DWORD, DWORD>(0, 0));
    for (BYTE i = 0; i < bCount; ++i)
    {
        *pMsg >> vecPermission.at(i).first;
        *pMsg >> vecPermission.at(i).second;
    }
    WORD wResult = pGuild->UpdateMemberPermissions(vecPermission);
    if (wResult != GUILD_OK)
    {
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    SendPermissionsUpdated(vecPermission, dwGuildID);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
}

void CGuildManager::OnMasterVoteStart(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        if (!g_pVoteManager)
            BSAssertFail();
        CVote* pVote = g_pVoteManager->CreateVote(dwGuildID);
        if (!pVote)
        {
            wResult = 0x4C34;
        }
        else
        {
            wResult = pGuild->DBIsMasterCandidates(pVote);
            if (wResult == GUILD_OK)
            {
                SendVoteStarted(pVote);
                SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
                return;
            }
            if (!g_pVoteManager)
                BSAssertFail();
            g_pVoteManager->RemoveVote(pVote);
        }
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnGuildWarBegin(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwGuildID1 = pMsg->ReadForwardOnly<DWORD>();
    DWORD dwGuildID2 = pMsg->ReadForwardOnly<DWORD>();
    GUILD_WAR_REQUEST request;
    request.b00 = 0;
    request.bVictoryPointIndex = 0;
    request.dwDuration = 0;
    request.dwLodgedGold = 0;
    GuildWarRequest_ReadFromMsg(request, pMsg);
    CInstanceGuildWar* pWar = NULL;
    WORD wResult = DBBeginGuildWar(dwGuildID1, dwGuildID2, request, &pWar);
    // BUG(SM-203): "already at war" (0x4C0C) is answered as a success (with an empty 0x38F5
    // subtype 25 skipped by the assert), every other failure gets no answer at all.
    if (wResult != GUILD_OK && wResult != 0x4C0C)
    {
        LogPrintf(0x2000001, "failed to begin guild war!!! [error_code: %d]", wResult);
        return;
    }
    if (!pWar)
        BSAssertFail();
    SendGuildWarBegun(pWar);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
}

void CGuildManager::OnGuildWarCombatResult(CMsg* pMsg)
{
    std::string strKillerName;
    std::string strVictimName;
    DWORD dwWarID, dwKillerGuildID, dwKillerCharID, dwVictimGuildID, dwVictimCharID;
    *pMsg >> dwWarID;
    *pMsg >> dwKillerGuildID;
    *pMsg >> dwKillerCharID;
    *pMsg >> strKillerName;
    *pMsg >> dwVictimGuildID;
    *pMsg >> dwVictimCharID;
    *pMsg >> strVictimName;
    int nPoint = pMsg->ReadForwardOnly<int>();
    WORD wResult;
    CInstanceGuildWar* pWar = FindGuildWar(dwWarID);
    CGuild* pKillerGuild = NULL;
    CGuild* pVictimGuild = NULL;
    CGuildMember* pKiller = NULL;
    CGuildMember* pVictim = NULL;
    if (pWar && (pKillerGuild = FindGuildT(dwKillerGuildID),
                 pVictimGuild = FindGuildT(dwVictimGuildID), pKillerGuild) && pVictimGuild &&
        (pKiller = pKillerGuild->FindMember(dwKillerCharID),
         pVictim = pVictimGuild->FindMember(dwVictimCharID), pKiller) && pVictim)
    {
        wResult = OnCombatResult(pWar, pKiller, pVictim, nPoint, strKillerName, strVictimName);
        if (wResult == GUILD_OK)
            return;
    }
    else
    {
        wResult = GUILD_ERR_FAILED;
    }
    LogPrintf(0x2000001,
              "PROCESS GUILD_WAR_COMBAT_RESULT FAILED!!!! error_code[%d] KillerChar[%d] Victim[%d]",
              wResult, dwKillerCharID, dwVictimCharID);
}

void CGuildManager::OnGuildWarRecordUpdate(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    int nBooty;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> nBooty;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    int nOldBooty = pGuild->m_pRecord->Booty;
    if (nBooty != nOldBooty)
        BSAssertFail();
    WORD wResult = pGuild->DBClearBooty();
    if (wResult != GUILD_OK)
    {
        SendAckResult(dwSessionID, dwRequestID, wResult);
        return;
    }
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    SendGuildWarRecordUpdated(dwGuildID, nOldBooty);
}

void CGuildManager::OnMemberOnlineState(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    BYTE bType;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> bType;
    WORD wResult;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->SetMemberOnline(dwCharID, bType);
        if (wResult == GUILD_OK)
            SendMemberOnlineState(dwGuildID, dwCharID, bType);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnMemberInfoUpdate(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    std::string strNickname;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> strNickname;
    WORD wResult;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->DBUpdateMember(dwCharID, strNickname);
        if (wResult == GUILD_OK)
            SendMemberInfoUpdated(strNickname, dwGuildID, dwCharID);
    }
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

// The crest goes to the crest uploader with the next revision; the uploader's answer arrives
// at OnCrestRevUpdate / SendCrestError.
void CGuildManager::OnGuildCrestRequest38(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    CREST_DATA crest;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> crest;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    int nCrestRev = pGuild->m_pRecord->LastCrestRev + 1;
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    if (!g_pMainProcess)
        BSAssertFail();
    CCrestUploader_PostUploadRequest(dwRequestID, dwSessionID, 1, dwGuildID, nCrestRev, &crest);
}

void CGuildManager::OnGuildCrestUpdate(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwGuildID);
    if (!pGuild)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    if (!pGuild->m_pRecord->CurCrestRev)
    {
        SendAckResult(dwSessionID, dwRequestID, 0x4C4F);
        return;
    }
    pGuild->SetCrestRev(0);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    SendGuildCrestResult(dwGuildID, 0);
}

void CGuildManager::OnUnionCrestRequest40(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwAllianceID;
    CREST_DATA crest;
    *pMsg >> dwRequestID;
    *pMsg >> dwAllianceID;
    *pMsg >> crest;
    CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
    if (!pAlliance)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_NOT_IN_UNION);
        return;
    }
    int nCrestRev = pAlliance->GetLastCrestRev() + 1;
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    if (!g_pMainProcess)
        BSAssertFail();
    CCrestUploader_PostUploadRequest(dwRequestID, dwSessionID, 2, dwAllianceID, nCrestRev,
                                     &crest);
}

void CGuildManager::OnUnionCrestUpdate(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwAllianceID;
    *pMsg >> dwRequestID;
    *pMsg >> dwAllianceID;
    CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwAllianceID);
    if (!pAlliance)
    {
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_NOT_IN_UNION);
        return;
    }
    if (!pAlliance->GetCurCrestRev())
    {
        SendAckResult(dwSessionID, dwRequestID, 0x4C4F);
        return;
    }
    pAlliance->SetCrestRev(0);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    SendUnionCrestResult(dwAllianceID, 0);
}

void CGuildManager::OnDonateSP42(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    int nAmount;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> nAmount;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        wResult = pGuild->DonateGP(nAmount, dwCharID);
        if (wResult == GUILD_OK)
        {
            SendDonateSPResult(dwGuildID, dwCharID, nAmount);
            SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
            return;
        }
    }
    LogPrintf(0x2000001, "SP DONATION FAILED!! GUILDID[%d] CHAR[%d] STUFF[%d] ERROR[%d]",
              dwGuildID, dwCharID, nAmount, wResult);
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

// The answer carries the gold the ability costs.
void CGuildManager::OnGiveMercenaryAbility(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID, dwCharID;
    BYTE bAbility;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> dwCharID;
    *pMsg >> bAbility;
    WORD wResult;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        wResult = GUILD_ERR_GUILD_NOT_FOUND;
    }
    else
    {
        DWORD dwGold = 0;
        DWORD dwGP = 0;
        wResult = pGuild->SetMercenaryAttr(bAbility, &dwGold, &dwGP);
        if (wResult == GUILD_OK)
        {
            SendMercenaryAbilityResult(dwGuildID, pGuild->m_pRecord->MercenaryAttr,
                                       pGuild->m_pRecord->GatheredSP);
            CMsg* pAck = AllocAck(dwRequestID);
            BYTE bResult = 1;
            pAck->WriteBytes(&bResult, 1);
            pAck->WriteBytes(&dwGold, 4);
            SendAck(dwSessionID, pAck);
            return;
        }
    }
    LogPrintf(0x2000001, "GIVE MERCENARY ABILITY FAILED!! GUILDID[%d] CHAR[%d] ABILITY[%d] "
              "ERROR[%d]", dwGuildID, dwCharID, bAbility, wResult);
    SendAckResult(dwSessionID, dwRequestID, wResult);
}

void CGuildManager::OnUpdateGuildSP(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwGuildID;
    int nOffset;
    *pMsg >> dwRequestID;
    *pMsg >> dwGuildID;
    *pMsg >> nOffset;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        LogPrintf(0x2000001, "Update Guild SP Failed!! GUILDID[%d] Offset[%d] CurSP[%d]",
                  dwGuildID, nOffset, -1);
        SendAckResult(dwSessionID, dwRequestID, GUILD_ERR_GUILD_NOT_FOUND);
        return;
    }
    pGuild->OffsetGP(nOffset, TRUE);
    SendGPUpdated(dwGuildID, pGuild->m_pRecord->GatheredSP);
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
}

void CGuildManager::OnOffsetGP(CMsg* pMsg)
{
    DWORD dwGuildID;
    int nOffset;
    BYTE bSave;
    *pMsg >> dwGuildID;
    *pMsg >> nOffset;
    *pMsg >> bSave;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
        return;
    if (pGuild->OffsetGP(nOffset, bSave) == GUILD_OK)
    {
        int nGatheredSP = pGuild->m_pRecord->GatheredSP;
        GetSingleton_CGuildManager();
        SendGPUpdated(dwGuildID, nGatheredSP);
    }
}

void CGuildManager::SendCrestError(DWORD dwRequestID, DWORD dwSessionID)
{
    SendAckResult(dwSessionID, dwRequestID, 0x4C50);
}

void CGuildManager::OnCrestRevUpdate(BYTE bGuild, DWORD dwRequestID, DWORD dwSessionID,
                                     DWORD dwID, int nCrestRev)
{
    if (bGuild == 1)
    {
        CGuild* pGuild = GetSingleton_CGuildManager()->FindGuildT(dwID);
        if (pGuild)
            pGuild->SetCrestRev(nCrestRev);
    }
    else
    {
        CAlliance* pAlliance = GetSingleton_CGuildManager()->FindAlliance(dwID);
        if (pAlliance)
            pAlliance->SetCrestRev(nCrestRev);
    }
    SendAckResult(dwSessionID, dwRequestID, GUILD_OK);
    if (bGuild == 1)
        SendGuildCrestResult(dwID, nCrestRev);
    else
        SendUnionCrestResult(dwID, nCrestRev);
}

// GP spent by the game server (no answer): the history row gets the negated offset.
void CGuildManager::OnOffsetGPAndLevel(CMsg* pMsg)
{
    DWORD dwGuildID = 0;
    DWORD dwJID = 0;
    int nOffset = 0;
    BYTE bReason = 0;
    BYTE bSave = 0;
    *pMsg >> dwGuildID;
    *pMsg >> dwJID;
    *pMsg >> nOffset;
    *pMsg >> bReason;
    *pMsg >> bSave;
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        BSAssertFail();
        return;
    }
    if (pGuild->OffsetGP(nOffset, bSave) != GUILD_OK)
    {
        BSAssertFail();
        return;
    }
    SendGPUpdated(dwGuildID, pGuild->m_pRecord->GatheredSP);
    if (DBAddGPHistory(dwGuildID, dwJID, -nOffset, bReason) != GUILD_OK)
    {
        BSAssertFail();
        return;
    }
    SendGuildInfo53(dwGuildID);
    if ((BYTE)pGuild->m_listGPHistory.size() > 100)
    {
        BYTE bCount = DBDeleteGPHistory(pGuild);
        if (bCount)
            SendGPHistory54(dwGuildID, bCount);
    }
}

WORD CGuildManager::OnGuildWarHostileRequest(DWORD dwJID, WORD wOpcode, CMsg* pMsg)
{
    if (!g_pClientManager)
        BSAssertFail();
    CUser* pUser = g_pClientManager->FindUserByJID(dwJID);
    if (!pUser)
        return GUILD_ERR_FAILED;
    trs_guild_job* pJob = trs_guild_job::Alloc();
    if (wOpcode != 0x7112)
    {
        BSAssertFail();
        pJob->Release();
        return GUILD_ERR_FAILED;
    }
    DWORD dwGuildID = pUser->GetGuildID();
    CGuild* pGuild = FindGuildT(dwGuildID);
    if (!pGuild)
    {
        pJob->Release();
        return GUILD_ERR_GUILD_NOT_FOUND;
    }
    if (!pGuild->IsMemberMaster(pUser->GetCharID()))
    {
        pJob->Release();
        return GUILD_ERR_NOT_MASTER;
    }
    DWORD dwWarID;
    *pMsg >> dwWarID;
    CInstanceGuildWar* pWar = FindGuildWar(dwWarID);
    if (!pWar)
    {
        pJob->Release();
        return GUILD_ERR_FAILED;
    }
    if (pMsg->GetUnreadSize())
        BSAssertFail();
    pMsg->SkipRemaining();
    DWORD dwTargetGuildID;
    if ((DWORD)pWar->Guild[0] == dwGuildID)
        dwTargetGuildID = pWar->Guild[1];
    else
        dwTargetGuildID = pWar->Guild[0];
    pJob->Init(dwJID, wOpcode, 0, 60.0f, dwWarID, dwTargetGuildID);
    SendHostileResult(dwWarID, dwGuildID);
    if (!GetSingleton_COverlappedJobMgr()->StartJob(pJob, pUser, pMsg))
    {
        pJob->Release();
        return GUILD_ERR_FAILED;
    }
    return GUILD_OK;
}

void CGuildManager::OnUnjoinHostileGuild(DWORD dwWarID, DWORD dwGuildID)
{
    if (!FindGuildWar(dwWarID))
        return;
    if (DBEndGuildWar(dwGuildID, dwWarID) == GUILD_OK)
    {
        SendGuildWarEnded(dwWarID, dwGuildID);
        return;
    }
    BSAssertFail();
    // BUG(SM-201): three %d with two arguments: ERROR shows the war ID, GUILDWARID the guild
    // and RequestorGuildID whatever follows on the stack; the error code is never logged.
    LogPrintf(0x2000001, "UNJOIN_HOSTILE_GUILD ERROR[%d]!!! GUILDWARID[%d] RequestorGuildID[%d] ",
              dwWarID, dwGuildID);
}

void CGuildManager::BroadcastToGuild(DWORD dwGuildID, transaction* pTransaction)
{
    GuildMap::iterator it = m_mapGuild.find(dwGuildID);
    if (it != m_mapGuild.end())
        static_cast<CGuild*>(it->second)->BroadcastToOnlineMembers(pTransaction);
}
