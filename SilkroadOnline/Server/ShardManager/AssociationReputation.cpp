#include "ShardManager/AssociationReputation.h"

#include <strsafe.h>

#include "BSLib/DB/Database.h"
#include "ServerCommon/InstanceRecords.h"
#include "ShardManager/AdminQueryJobs.h"
#include "ShardManager/ShardScheduleExtern.h"
#include "ShardManager/ShardWorld1Extern.h"
#include "ShardManager/ShardWorldExtern.h"

enum
{
    SHARD_TABLE_ASSOCIATION_REPUTATION = 0x32,
};

static CAssociationReputationMgr g_AssociationReputationMgr;
CAssociationReputationMgr* g_pAssociationReputationMgr;

CAssociationReputationMgr::CAssociationReputationMgr()
{
    if (g_pAssociationReputationMgr)
        BSAssertFail();
    g_pAssociationReputationMgr = this;
    m_Anubis.nHunter = 0;
    m_Anubis.nRobber = 0;
    m_SavedAnubis.nHunter = 0;
    m_SavedAnubis.nRobber = 0;
    m_Haroeris.nHunter = 0;
    m_Haroeris.nRobber = 0;
    m_SavedHaroeris.nHunter = 0;
    m_SavedHaroeris.nRobber = 0;
}

CAssociationReputationMgr::~CAssociationReputationMgr()
{
    g_pAssociationReputationMgr = NULL;
}

CInstanceAssociatedReputation* CAssociationReputationMgr::SelectAssociation(
    const std::string& strTemple, const std::string& strCode)
{
    if (!g_pShardDB)
        BSAssertFail();
    std::vector<CDBTable*>& vecTables = g_pShardDB->GetInstanceTables();
    if (vecTables.empty() || vecTables.size() <= SHARD_TABLE_ASSOCIATION_REPUTATION)
    {
        BSAssertFail();
        BSAssertFail();
        return NULL;
    }
    CDBTable* pTable = vecTables[SHARD_TABLE_ASSOCIATION_REPUTATION];
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }

    CDBRecordList list;
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00,
                         "WHERE AssociationCodeName = '%s' and AssociationTypeName = '%s'",
                         strCode.c_str(), strTemple.c_str()) != S_OK)
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szClause, 6, &list, 0))
    {
        CDBObjectList_DetachAndClear(&list, pTable);
        BSAssertFail();
        return NULL;
    }
    if (list.empty())
        BSAssertFail();
    // BUG(SM-110): the record is not given back to the table; the list only forgets it.
    return static_cast<CInstanceAssociatedReputation*>(list.front());
}

void CAssociationReputationMgr::LoadAssociations()
{
    // The records are used without a check: a failed select ends here with a NULL access like
    // in the original.
    CInstanceAssociatedReputation* pRecord;
    pRecord = SelectAssociation(std::string("TEMPLE_ANUBIS"), std::string("TraderHunter"));
    m_Anubis.nHunter = pRecord->Reputation;
    pRecord = SelectAssociation(std::string("TEMPLE_ANUBIS"), std::string("Robber"));
    m_Anubis.nRobber = pRecord->Reputation;
    m_SavedAnubis = m_Anubis;
    pRecord = SelectAssociation(std::string("TEMPLE_HAROERIS"), std::string("TraderHunter"));
    m_Haroeris.nHunter = pRecord->Reputation;
    pRecord = SelectAssociation(std::string("TEMPLE_HAROERIS"), std::string("Robber"));
    m_Haroeris.nRobber = pRecord->Reputation;
    m_SavedHaroeris = m_Haroeris;
    m_bLoaded = TRUE;
    m_nAnubisWinner = ASSOC_WINNER_NONE;
    m_nHaroerisWinner = ASSOC_WINNER_NONE;
}

int CAssociationReputationMgr::TempleIndexFromCode(const std::string& strCode)
{
    if (strCode.compare("TEMPLE_ANUBIS") == 0)
        return ASSOC_TEMPLE_ANUBIS;
    // BUG(SM-111): every code that is not TEMPLE_HAROERIS gives the Anubis index.
    return strCode.compare("TEMPLE_HAROERIS") == 0 ? ASSOC_TEMPLE_HAROERIS : ASSOC_TEMPLE_ANUBIS;
}

void CAssociationReputationMgr::AddHunterPoint(const std::string& strCode, int nPoint)
{
    switch (TempleIndexFromCode(strCode))
    {
    case ASSOC_TEMPLE_ANUBIS:
        m_Anubis.nHunter += nPoint;
        break;
    case ASSOC_TEMPLE_HAROERIS:
        m_Haroeris.nHunter += nPoint;
        break;
    }
}

void CAssociationReputationMgr::AddRobberPoint(const std::string& strCode, int nPoint)
{
    switch (TempleIndexFromCode(strCode))
    {
    case ASSOC_TEMPLE_ANUBIS:
        m_Anubis.nRobber += nPoint;
        break;
    case ASSOC_TEMPLE_HAROERIS:
        m_Haroeris.nRobber += nPoint;
        break;
    }
}

int CAssociationReputationMgr::GetHunterPoint(const std::string& strCode)
{
    switch (TempleIndexFromCode(strCode))
    {
    case ASSOC_TEMPLE_ANUBIS:
        return m_Anubis.nHunter;
    case ASSOC_TEMPLE_HAROERIS:
        return m_Haroeris.nHunter;
    }
    return 0;
}

int CAssociationReputationMgr::GetRobberPoint(const std::string& strCode)
{
    switch (TempleIndexFromCode(strCode))
    {
    case ASSOC_TEMPLE_ANUBIS:
        return m_Anubis.nRobber;
    case ASSOC_TEMPLE_HAROERIS:
        return m_Haroeris.nRobber;
    }
    return 0;
}

BOOL CAssociationReputationMgr::ResetHunterPoint(int nTemple)
{
    if (nTemple == ASSOC_TEMPLE_ANUBIS)
        m_Anubis.nHunter = 0;
    else if (nTemple == ASSOC_TEMPLE_HAROERIS)
        m_Haroeris.nHunter = 0;
    return TRUE;
}

BOOL CAssociationReputationMgr::ResetRobberPoint(int nTemple)
{
    if (nTemple == ASSOC_TEMPLE_ANUBIS)
        m_Anubis.nRobber = 0;
    else if (nTemple == ASSOC_TEMPLE_HAROERIS)
        m_Haroeris.nRobber = 0;
    return TRUE;
}

void CAssociationReputationMgr::FlushIfChanged()
{
    if (m_bLoaded && m_Anubis.nHunter >= 0 && m_Anubis.nRobber >= 0 &&
        m_Haroeris.nHunter >= 0 && m_Haroeris.nRobber >= 0 && IsChanged())
    {
        QueueDBUpdate(&m_Haroeris, &m_Anubis);
    }
}

BOOL CAssociationReputationMgr::QueueDBUpdate(const ASSOC_TEMPLE_POINTS* pHaroeris,
                                              const ASSOC_TEMPLE_POINTS* pAnubis)
{
    AQ_AssociationReputation* pJob = AQ_AssociationReputation::Alloc();
    if (!DB_UpdateAssociationReputation(pJob, &pAnubis->nHunter, &pHaroeris->nHunter))
    {
        pJob->ReturnToPool();
        return FALSE;
    }
    MainProcess_PostDBJob(pJob);
    m_SavedAnubis = *pAnubis;
    m_SavedHaroeris = *pHaroeris;
    return TRUE;
}

BOOL CAssociationReputationMgr::IsChanged() const
{
    return m_Anubis.nHunter != m_SavedAnubis.nHunter || m_Anubis.nRobber != m_SavedAnubis.nRobber ||
           m_Haroeris.nHunter != m_SavedHaroeris.nHunter ||
           m_Haroeris.nRobber != m_SavedHaroeris.nRobber;
}

BOOL CAssociationReputationMgr::UpdateTemple(int nTemple)
{
    if (nTemple == ASSOC_TEMPLE_ANUBIS || nTemple == ASSOC_TEMPLE_HAROERIS)
    {
        if (!ResetHunterPoint(nTemple) || !ResetRobberPoint(nTemple))
            return FALSE;
    }
    return QueueDBUpdate(&m_Haroeris, &m_Anubis) != FALSE;
}

BOOL CAssociationReputationMgr::DecideTempleWinner(int nTemple)
{
    if (!g_pMainProcess)
        BSAssertFail();
    if (MainProcess_IsTeenage() == 1)
    {
        // Teenage service: no association war, the temple is a draw.
        if (nTemple == ASSOC_TEMPLE_ANUBIS)
            m_nAnubisWinner = ASSOC_WINNER_DRAW;
        else if (nTemple == ASSOC_TEMPLE_HAROERIS)
            m_nHaroerisWinner = ASSOC_WINNER_DRAW;
        return TRUE;
    }

    if (nTemple == ASSOC_TEMPLE_ANUBIS)
    {
        if (m_Anubis.nHunter > m_Anubis.nRobber)
            m_nAnubisWinner = ASSOC_WINNER_TRADER_HUNTER;
        else if (m_Anubis.nHunter < m_Anubis.nRobber)
            m_nAnubisWinner = ASSOC_WINNER_ROBBER;
        else
            m_nAnubisWinner = ASSOC_WINNER_DRAW;
    }
    else if (nTemple == ASSOC_TEMPLE_HAROERIS)
    {
        if (m_Haroeris.nHunter > m_Haroeris.nRobber)
            m_nHaroerisWinner = ASSOC_WINNER_TRADER_HUNTER;
        else if (m_Haroeris.nHunter < m_Haroeris.nRobber)
            m_nHaroerisWinner = ASSOC_WINNER_ROBBER;
        else
            m_nHaroerisWinner = ASSOC_WINNER_NONE;     // BUG(SM-112): a draw is 0, not 3
    }
    else
    {
        return TRUE;
    }
    return UpdateTemple(nTemple) ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------------------------
// The calls of the other groups

void AssociationReputationMgr_LoadAssociations()
{
    GetSingleton_CAssociationReputationMgr()->LoadAssociations();
}

// ---------------------------------------------------------------------------------------------
// CWebDB

static CWebDB g_WebDB;
CWebDB* g_pWebDB;

CWebDB::CWebDB()
{
    if (g_pWebDB)
        BSAssertFail();
    g_pWebDB = this;
    m_dw04 = 0;
}

CWebDB::~CWebDB()
{
    g_pWebDB = NULL;
}
