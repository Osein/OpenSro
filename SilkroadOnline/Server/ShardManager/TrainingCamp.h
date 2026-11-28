#pragma once

#include <time.h>

#include <map>
#include <string>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "ServerCommon/TrainingCampCommon.h"
#include "ShardManager/SocialExtern.h"

// Training camps (academies) of SR_ShardManager: the camps and their members, the recruit board
// (CRegMatchMgr) and the mentor applications (CTrainingCampManager, static object 0x89a660,
// g_pTrainingCampManager 0x8a8930). Requests come from the game servers with 0x7C80 (answered
// with 0xBC80), 0x7C81 and 0x7C00 (0x747E); changes go to the game servers as 0x3C80.

enum
{
    MSG_TRAINING_CAMP_NOTIFY        = 0x3C80,
    MSG_TRAINING_CAMP_ACK           = 0xBC80,
    MSG_TRAINING_CAMP_APPLY_REQ     = 0x747E,

    // result codes
    TRAINING_CAMP_ERR_NO_CAMP           = 0x680B,
    TRAINING_CAMP_ERR_NO_LEADER         = 0x680C,
    TRAINING_CAMP_ERR_NO_USER           = 0x680D,
    TRAINING_CAMP_ERR_NAME              = 0x680F,
    TRAINING_CAMP_ERR_MENTOR_FULL       = 0x6810,
    TRAINING_CAMP_ERR_IN_CAMP           = 0x6812,
    TRAINING_CAMP_ERR_DB                = 0x6813,
    TRAINING_CAMP_ERR_DB_STATE          = 0x6814,
    TRAINING_CAMP_ERR_NO_MEMBER         = 0x6815,
    TRAINING_CAMP_ERR_NOT_LEADER        = 0x6816,
    TRAINING_CAMP_ERR_TEXT_LENGTH       = 0x6817,
    TRAINING_CAMP_ERR_IS_LEADER         = 0x6819,
    TRAINING_CAMP_ERR_REGISTERED        = 0x6825,
    TRAINING_CAMP_ERR_NO_REG            = 0x6826,
    TRAINING_CAMP_ERR_REG_PENDING       = 0x6827,
    TRAINING_CAMP_ERR_TRAINEE_FULL      = 0x6828,
    TRAINING_CAMP_ERR_LEADER_RECRUITING = 0x6829,
    TRAINING_CAMP_ERR_REG_CLASS         = 0x682A,
};

class CTrainingCamp;

// ---------------------------------------------------------------------------------------------
// Recruit board

// An entry of the recruit board (0x38 bytes, pooled).
struct TrainingCampRegData
{
    // TrainingCampRegData::ctor (0x45d630, ConstructItem 0x45ce60); the destructor (0x458200,
    // DestroyItem 0x45c740) only frees the text.
    TrainingCampRegData()
    {
        m_dwID = 0;
        m_dw04 = 0;
        m_byFlags = 0;
        m_strText.clear();
    }

    // 0x45d4c0 / 0x45d5d0: ID, DWORD, flags, text.
    void ReadFromMsg(CMsg* pMsg);
    void WriteToMsg(CMsg* pMsg) const;
    // 0x45aea0: the entry, its time and the camp summary, nothing when the registrant is
    // offline.
    void WriteEntryToMsg(CMsg* pMsg) const;
    // The inlined reset of Unregister and RebuildListMsgs.
    void Reset()
    {
        m_dwID = 0;
        m_dw04 = 0;
        m_byFlags = 0;
        m_strText.clear();
        m_dwRegTime = 0;
        m_pCamp = NULL;
        m_dwJID = 0;
        m_bPending = 0;
    }

    DWORD           m_dwID;         // +00
    DWORD           m_dw04;         // +04
    BYTE            m_byFlags;      // +08 1 trainees wanted, 2 mentors wanted
    std::string     m_strText;      // +0C
    DWORD           m_dwRegTime;    // +28 time()
    CTrainingCamp*  m_pCamp;        // +2C
    DWORD           m_dwJID;        // +30 the registrant
    DWORD           m_bPending;     // +34 an application waits for the answer
};

C_ASSERT(FIELD_OFFSET(TrainingCampRegData, m_dwRegTime) == 0x28);
C_ASSERT(sizeof(TrainingCampRegData) == 0x38);

// Registration board with list pages (0x88 bytes). The entry count of a page is kept in the
// opcode field of its message.
template <class TCamp, class TData>
class CRegMatchMgr
{
public:
    typedef std::map<DWORD, TData*> DataMap;

    // CRegMatchMgr_CTrainingCamp_TrainingCampRegData::ctor (0x45a840)
    explicit CRegMatchMgr(const char* pszPoolName)
        : m_Pool(NULL, 0, 500, pszPoolName)
    {
        m_byPageSize = 100;
        m_dwExpireMinutes = 60;
        m_dwSerial = 0;
    }
    virtual ~CRegMatchMgr() {}                                              // 0

    // 0x45aa10
    TData* Find(DWORD dwID)
    {
        typename DataMap::iterator it = m_mapData.find(dwID);
        if (it == m_mapData.end())
            return NULL;
        return it->second;
    }

    // 0x45aa60: a new entry with the next serial as ID (*pdwID).
    BOOL Register(TCamp* pCamp, DWORD dwJID, const TData* pData, DWORD* pdwID)
    {
        if (++m_dwSerial == 0)
            m_dwSerial = 1;
        *pdwID = m_dwSerial;
        TData* p = m_Pool.Alloc();
        if (!p)
            return FALSE;
        p->m_dwJID = dwJID;
        p->m_pCamp = pCamp;
        p->m_bPending = 0;
        p->m_dwID = pData->m_dwID;
        p->m_dw04 = pData->m_dw04;
        p->m_byFlags = pData->m_byFlags;
        p->m_strText.assign(pData->m_strText, 0, std::string::npos);
        _time32((__time32_t*)&p->m_dwRegTime);
        p->m_dwID = *pdwID;
        m_mapData.insert(typename DataMap::value_type(*pdwID, p));
        return TRUE;
    }

    // 0x45ab50: only the registrant (dwJID) can remove an entry.
    BOOL Unregister(DWORD dwJID, DWORD dwID)
    {
        typename DataMap::iterator it = m_mapData.find(dwID);
        if (it == m_mapData.end())
            return FALSE;
        TData* p = it->second;
        if (dwJID != p->m_dwJID)
            return FALSE;
        p->Reset();
        m_mapData.erase(it);
        m_Pool.Free(p);
        return TRUE;
    }

    // 0x45ac00: the ID of the entry is overwritten with the one of pData as well.
    BOOL Update(const TData* pData, DWORD dwJID, DWORD dwID)
    {
        typename DataMap::iterator it = m_mapData.find(dwID);
        if (it == m_mapData.end())
            return FALSE;
        TData* p = it->second;
        if (!p || p->m_dwJID != dwJID)
            return FALSE;
        p->m_dwID = pData->m_dwID;
        p->m_dw04 = pData->m_dw04;
        p->m_byFlags = pData->m_byFlags;
        p->m_strText.assign(pData->m_strText, 0, std::string::npos);
        _time32((__time32_t*)&p->m_dwRegTime);
        return TRUE;
    }

    // 0x45aca0: entries of offline registrants are dropped, the others are written in pages of
    // m_byPageSize - 1.
    void RebuildListMsgs();
    // 0x45b670
    void ClearListMsgs();

public:
    BYTE                        m_byPageSize;       // +04
    DWORD                       m_dwExpireMinutes;  // +08
    DWORD                       m_dwSerial;         // +0C
    DataMap                     m_mapData;          // +10
    std::vector<CMsg*>          m_vecListMsg;       // +1C
    CChunkAllocatorST<TData>    m_Pool;             // +2C
};

// ---------------------------------------------------------------------------------------------
// Camp

class CTrainingCamp : public CTrainingCampCommon
{
public:
    CTrainingCamp() {}                                                      // 0x455fd0
    // 0x456050 (deleting 0x456030)
    virtual ~CTrainingCamp() {}

    // 0x4560a0: the leader's levels, RefObjID and name, graduates, rank and the member counts.
    void WriteSummaryToMsg(CMsg* pMsg);
    // CTrainingCamp::UpdateMemberStats_Notify3C80 (0x456350): 0x3C80 subtype 13 to the game
    // servers except dwExceptSession.
    void UpdateMemberStats(DWORD dwExceptSession, DWORD dwCharID, DWORD dwValue, WORD w1,
                           WORD w2, WORD w3, WORD w4);
    // 0x456470: 1 online state, 2 maximum level, 3 honor, 6 current level, 7 RefObjID; always
    // 0x3C80 subtype 5.
    void RefreshMemberStatus(DWORD dwCharID, BYTE byType, DWORD dwValue, CUser* pUser);
    // 0x4566d0: _TRAINING_CAMP_DELMEMBER unless byReason is 3, then the member is freed.
    WORD DBDelMember(BYTE byReason, DWORD dwCampID, DWORD dwCharID);
    // 0x4568a0: _TRAINING_CAMP_MEMBER_GRADUATE, the camp's and the members' honor, then the
    // member is freed.
    WORD DBMemberGraduate(DWORD dwCampID, DWORD dwCharID, int nHonor);
    // 0x456ca0
    WORD UpdateTextFields(std::string strTitle, std::string strComment);
};

C_ASSERT(sizeof(CTrainingCamp) == 0x18);

// ---------------------------------------------------------------------------------------------
// Mentor application job (Provided by the transaction group: trs_mentor_job, 0x4203b0)

// The application of a character (0x2C bytes, trs_mentor_job +0x3C).
struct TrainingCampApplyReq
{
    // 0x45d530: entry ID, DWORD, two BYTEs (the second is the level), DWORD, text.
    void ReadFromMsg(CMsg* pMsg);

    DWORD       m_dwRegID;      // +00
    DWORD       m_dw04;         // +04
    BYTE        m_by08;         // +08
    BYTE        m_byLevel;      // +09
    DWORD       m_dw0C;         // +0C
    std::string m_strText;      // +10
};

class trs_mentor_job : public transaction
{
public:
    TrainingCampApplyReq    m_Req;      // +3C
};

C_ASSERT(FIELD_OFFSET(trs_mentor_job, m_Req) == 0x3C);

// trs_mentor_job_PoolAlloc_4203B0
trs_mentor_job* trs_mentor_job_Alloc();

// ---------------------------------------------------------------------------------------------
// Manager

typedef CRegMatchMgr<CTrainingCamp, TrainingCampRegData> TrainingCampRegMgr;
typedef std::map<DWORD, CTrainingCamp*> TrainingCampCharMap;

class CTrainingCampManager : public CTrainingCampMgrCommon, public TrainingCampRegMgr
{
public:
    CTrainingCampManager();                                                 // 0x456e10
    virtual ~CTrainingCampManager();                                        // 0 (0x456f20)

    // 0x457190: the camps, the character map and the start-up honor ranking.
    virtual BOOL Load();                                                    // 1
    // 0x4572a0: everything again (the process exits when that fails), 0x3C80 subtype 8. Always
    // TRUE.
    virtual BOOL Reload();                                                  // 3
    // 0x457340: a CTrainingCamp for the record, NULL when it cannot be loaded.
    virtual CTrainingCampCommon* AddCamp(CInstanceTrainingCamp* pCamp);    // 4

    CTrainingCamp* FindTrainingCamp(DWORD dwCampID)
    {
        return (CTrainingCamp*)FindCamp(dwCampID);
    }
    // 0x455ef0 / 0x455f20 / 0x455f70
    void InsertCharCampMap(DWORD dwCharID, CTrainingCamp* pCamp);
    void EraseCharCampMap(DWORD dwCharID);
    CTrainingCamp* FindCampByChar(DWORD dwCharID);

    // CTrainingCampManager::IsCampLeaderOnline (0x457060): whether the camp's leader is online
    // with a recruit board entry.
    BOOL IsLeaderRecruiting(DWORD dwCampID);
    // 0x4574c0
    WORD DeleteCamp(DWORD dwCampID, DWORD dwCharID);
    // 0x456b50: _TRAINING_CAMP_DELCAMP and the members are freed.
    static WORD DBDeleteCamp(DWORD dwCharID, CTrainingCamp* pCamp);
    // 0x458a60 / 0x458ab0
    WORD KickMember(BYTE byReason, DWORD dwCampID, DWORD dwCharID);
    WORD GraduateMember(DWORD dwCampID, DWORD dwCharID, BYTE byReason, int nHonor);
    // 0x459400: the applicant of an accepted mentor job joins dwCampID.
    WORD AddMember(DWORD dwCampID, CUser* pUser);
    // 0x459b30: the leader's answer to an application (0 refused, 1 accepted, 2 timeout).
    void ProcessMentorJob(trs_mentor_job* pJob, BYTE byAnswer);

    // 0x7C80 (TrainingCamp_HandleReq7C80, 0x457610) and its subtypes
    void OnRequest7C80(CMsg* pMsg);
    void OnCreateCamp(CMsg* pMsg);                                          // 0  0x458e90
    void OnDisbandCamp(CMsg* pMsg);                                         // 1  0x457960
    void OnAddMember(CMsg* pMsg);                                           // 2  0x458b00
    void OnKickMember(CMsg* pMsg);                                          // 3  0x457ae0
    void OnGraduateMember(CMsg* pMsg);                                      // 4  0x457c30
    void OnUpdateTextFields(CMsg* pMsg);                                    // 7  0x457da0
    void OnRegisterRecruit(CMsg* pMsg);                                     // 16 0x457f90
    void OnUpdateRecruit(CMsg* pMsg);                                       // 17 0x458270
    void OnUnregisterRecruit(CMsg* pMsg);                                   // 18 0x458480
    void OnRecruitListPage(CMsg* pMsg);                                     // 19 0x458630
    // 0x7C81 (TrainingCamp_HandleReq7C81, 0x457720): 5, 6 and 13
    void OnRequest7C81(CMsg* pMsg);
    void OnRefreshMemberStatus(CMsg* pMsg);                                 // 0x457880
    void OnRelayCampText(CMsg* pMsg);                                       // 0x457930
    void OnUpdateMemberStats(CMsg* pMsg);                                   // 0x457760
    // CTrainingCampManager::RelayCampText_Notify3C80 (0x456180): 0x3C80 subtype 6.
    static void RelayCampText(CMsg* pMsg, CTrainingCamp* pCamp);
    // TrainingCamp_HandleReq7C00_ApplyToRecruit (0x45a010): starts a trs_mentor_job.
    WORD OnApplyToRecruit(DWORD dwJID, DWORD dwOpcode, CMsg* pMsg);
    // TrainingCamp_UnregisterOnLogout (0x459ad0)
    void UnregisterOnLogout(CUser* pUser);

public:
    TrainingCampCharMap     m_mapCharCamp;      // +98
};

C_ASSERT(sizeof(CTrainingCampManager) == 0xA4);

// TrainingCamp_DBCreateCamp (0x459090): _TRAINING_CAMP_CREATE, *pdwCampID on success.
WORD TrainingCamp_DBCreateCamp(DWORD* pdwCampID, DWORD dwCharID);
// TrainingCamp_DBAddMember (0x459240): _TRAINING_CAMP_ADDMEMBER
WORD TrainingCamp_DBAddMember(DWORD dwCampID, DWORD dwCharID);
// TrainingCamp_DBIsHonorUpdateNeeded (0x459990): TRUE when the procedure returns 0; FALSE also
// when the query fails.
BOOL TrainingCamp_DBIsHonorUpdateNeeded();
// TrainingCamp_DBUpdateHonorRank (0x459a00): _TRAINING_CAMP_UPDATEHONORRANK, 0x3C80 subtype 11.
void TrainingCamp_DBUpdateHonorRank();

// TrainingCampMgr_FindCampByChar_wrap (0x459980)
CTrainingCampCommon* TrainingCampMgr_FindCampByChar(DWORD dwCharID,
                                                    CTrainingCampManager* pManager);

// g_pTrainingCampManager (0x8a8930)
extern CTrainingCampManager* g_pTrainingCampManager;
