#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "BSLib/BSDebug.h"

class CInstanceAssociatedReputation;

// The reputation of the two associations (TraderHunter and Robber) in the Egyptian temples. The
// game servers add points (CMainProcess::OnMsg_74DB) and ask for them (OnMsg_7CDC); the points
// go to _AssociationReputation every 10 seconds when they changed, and the temple gate jobs
// decide the winner of a temple when it opens.

// Temple indices (AssocRep_TempleIndexFromCode).
enum
{
    ASSOC_TEMPLE_ANUBIS     = 0,    // "TEMPLE_ANUBIS"   (Anubis and Isis)
    ASSOC_TEMPLE_HAROERIS   = 1,    // "TEMPLE_HAROERIS" (Haroeris and Seth)
};

// Winners of a temple (+28 / +2C).
enum
{
    ASSOC_WINNER_NONE           = 0,
    ASSOC_WINNER_TRADER_HUNTER  = 1,
    ASSOC_WINNER_ROBBER         = 2,
    ASSOC_WINNER_DRAW           = 3,
};

// The points of one temple.
struct ASSOC_TEMPLE_POINTS
{
    int     nHunter;    // +0  TraderHunter
    int     nRobber;    // +4  Robber
};

// CAssociationReputationMgr (0x30 bytes; static object 0x89A364, g_pAssociationReputationMgr =
// dword_8A891C).
class CAssociationReputationMgr
{
public:
    // CAssociationReputationMgr::CreateSingleton (0x4344f0): asserts that there is no other
    // object and becomes the singleton.
    CAssociationReputationMgr();
    // CAssociationReputationMgr::dtor (0x4345b0; vf00 0x434590 is the deleting destructor).
    virtual ~CAssociationReputationMgr();

    // 0x434600 (CShardManager::vf11): the four reputations of _AssociationReputation.
    void LoadAssociations();
    // 0x434910: the record of strCode ("TraderHunter" / "Robber") in strTemple; NULL on the
    // failures (asserted).
    static CInstanceAssociatedReputation* SelectAssociation(const std::string& strTemple,
                                                            const std::string& strCode);

    // AssocRep_TempleIndexFromCode (0x434b70).
    static int TempleIndexFromCode(const std::string& strCode);
    // AssocRep_AddTempleCount1 / 2 (0x434af0 / 0x434b10, CMainProcess::OnMsg_74DB).
    void AddHunterPoint(const std::string& strCode, int nPoint);
    void AddRobberPoint(const std::string& strCode, int nPoint);
    // AssocRep_GetTempleCount1 / 2 (0x434b30 / 0x434b50, CMainProcess::OnMsg_7CDC).
    int GetHunterPoint(const std::string& strCode);
    int GetRobberPoint(const std::string& strCode);
    // AssocRep_ResetTempleCount1 / 2 (0x434bc0 / 0x434be0): always TRUE.
    BOOL ResetHunterPoint(int nTemple);
    BOOL ResetRobberPoint(int nTemple);

    // 0x434c00 (MainTimer_AssociationReputationFlush): saves the points when they changed.
    void FlushIfChanged();
    // 0x434c40: posts AQ_AssociationReputation with both temples and takes them as saved.
    BOOL QueueDBUpdate(const ASSOC_TEMPLE_POINTS* pHaroeris, const ASSOC_TEMPLE_POINTS* pAnubis);
    // 0x434cd0
    BOOL IsChanged() const;
    // 0x434d00: the points of the temple start again from 0 and everything is saved.
    BOOL UpdateTemple(int nTemple);
    // 0x434d60 (temple gate jobs): decides the winner of nTemple.
    BOOL DecideTempleWinner(int nTemple);

    // CMainProcess::GetField_0x28 / GetField_0x2C in the analysis (inlined).
    int GetAnubisWinner() const
    {
        return m_nAnubisWinner;
    }
    int GetHaroerisWinner() const
    {
        return m_nHaroerisWinner;
    }

public:
    ASSOC_TEMPLE_POINTS m_Anubis;           // +04
    ASSOC_TEMPLE_POINTS m_SavedAnubis;      // +0C  the values of the last update
    ASSOC_TEMPLE_POINTS m_Haroeris;         // +14
    ASSOC_TEMPLE_POINTS m_SavedHaroeris;    // +1C
    BOOL                m_bLoaded;          // +24
    int                 m_nAnubisWinner;    // +28  ASSOC_WINNER_*
    int                 m_nHaroerisWinner;  // +2C
};

C_ASSERT(FIELD_OFFSET(CAssociationReputationMgr, m_Anubis) == 0x04);
C_ASSERT(FIELD_OFFSET(CAssociationReputationMgr, m_Haroeris) == 0x14);
C_ASSERT(FIELD_OFFSET(CAssociationReputationMgr, m_bLoaded) == 0x24);
C_ASSERT(FIELD_OFFSET(CAssociationReputationMgr, m_nHaroerisWinner) == 0x2C);
C_ASSERT(sizeof(CAssociationReputationMgr) == 0x30);

// 0x8A891C
extern CAssociationReputationMgr* g_pAssociationReputationMgr;

inline CAssociationReputationMgr* GetSingleton_CAssociationReputationMgr()
{
    if (!g_pAssociationReputationMgr)
        BSAssertFail();
    return g_pAssociationReputationMgr;
}

// ---------------------------------------------------------------------------------------------
// CWebDB (8 bytes; static object 0x89A35C, g_pWebDB = dword_8A8918)

class CWebDB
{
public:
    // CWebDB::CreateSingleton (0x433e80)
    CWebDB();
    // CWebDB::dtor (0x433f10)
    virtual ~CWebDB();

public:
    DWORD   m_dw04;     // +04
};

C_ASSERT(sizeof(CWebDB) == 8);

extern CWebDB* g_pWebDB;
