#pragma once

#include <windows.h>

#include <map>
#include <vector>

#include "BSNet/Msg.h"
#include "ServerCommon/GuildCommon.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/PoolObject.h"

// Training camp (academy) classes shared with SR_GameServer: the camp with its members and the
// camp manager base (SR_ShardManager 0x455bd0..0x45ebf0). The camps live in _TrainingCamp, the
// members in _TrainingCampMember and the buffs of the members in _TrainingCampBuffStatus.

class CUser;
class CTrainingCampCommon;

// ---------------------------------------------------------------------------------------------
// Record setters (the guards are the tables' update flags)

// TrainingCampRecord::SetField48 (0x455bd0) / SetField52 (0x455c00) / SetString72 (0x455c30) /
// SetString100 (0x455ce0)
inline void TrainingCampRecord_SetGraduateCount(CInstanceTrainingCamp* p, int nValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCamp::s_TableInfo, p->GraduateCount, nValue, 3);
}

inline void TrainingCampRecord_SetEvaluationPoint(CInstanceTrainingCamp* p, int nValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCamp::s_TableInfo, p->EvaluationPoint, nValue, 3);
}

inline void TrainingCampRecord_SetCommentTitle(CInstanceTrainingCamp* p, std::string strValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCamp::s_TableInfo, p->CommentTitle, strValue, 4);
}

inline void TrainingCampRecord_SetComment(CInstanceTrainingCamp* p, std::string strValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCamp::s_TableInfo, p->Comment, strValue, 4);
}

// TrainingCamp_SetField20_Guarded_455D90 (0x455d90, RefObjID, no dirty group) /
// TrainingCampMemberRecord::SetByte82 (0x455db0) / SetByte83 (0x455de0) / SetField84 (0x455e10)
inline void TrainingCampMemberRecord_SetRefObjID(CInstanceTrainingCampMember* p, int nValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCampMember::s_TableInfo, p->RefObjID, nValue, 1);
}

inline void TrainingCampMemberRecord_SetCurLevel(CInstanceTrainingCampMember* p, BYTE byValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCampMember::s_TableInfo, p->CharCurLevel, byValue, 2);
}

inline void TrainingCampMemberRecord_SetMaxLevel(CInstanceTrainingCampMember* p, BYTE byValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCampMember::s_TableInfo, p->CharMaxLevel, byValue, 2);
}

inline void TrainingCampMemberRecord_SetHonorPoint(CInstanceTrainingCampMember* p, int nValue)
{
    DBRecord_SetColumn(p, CInstanceTrainingCampMember::s_TableInfo, p->HonorPoint, nValue, 3);
}

// TrainingCampMemberData::WriteToMsg (0x455e40): CampID, CharID, RefObjID, name, class, joined
// level, join date (16 bytes), current and maximum level, honor points.
void TrainingCampMemberRecord_WriteToMsg(CMsg* pMsg, const CInstanceTrainingCampMember* p);

// CTrainingCampCommon::WriteCampDataToMsg (0x45d7a0): 1 ID, 2 creation date, 4 rank, 8 comment
// title and comment.
void TrainingCampRecord_WriteToMsg(BYTE byMask, const CInstanceTrainingCamp* p, CMsg* pMsg);

// ---------------------------------------------------------------------------------------------
// Members

// One buff slot of a member (12 bytes, pooled).
class TrainingCampMemberBuffData
{
public:
    // TrainingCampMemberBuffData::ctor (0x45d6a0)
    TrainingCampMemberBuffData()
        : m_pRecord(NULL)
    {
    }
    ~TrainingCampMemberBuffData()
    {
        DetachDBObject();
    }

    // TrainingCampMemberBuffData::vf00 (0x45d8f0)
    virtual void Release();                                                 // 0

    // 0x45d930: the record back to _TrainingCampBuffStatus.
    void DetachDBObject();
    void ResetForPool()
    {
        DetachDBObject();
    }

public:
    BYTE                                m_bInUse;   // +04
    CInstanceTrainingCampBuffStatus*    m_pRecord;  // +08

    static CChunkAllocatorST<TrainingCampMemberBuffData> s_Pool;
};

C_ASSERT(sizeof(TrainingCampMemberBuffData) == 12);

enum
{
    TRAINING_CAMP_CLASS_LEADER  = 0,
    TRAINING_CAMP_CLASS_MENTOR  = 1,
    TRAINING_CAMP_CLASS_TRAINEE = 2,
};

// One member of a camp (0x4C bytes, pooled).
class TrainingCampMember
{
public:
    TrainingCampMember();                                                   // 0x45d6c0
    ~TrainingCampMember();                                                  // 0x45d730

    // TrainingCampMember::vf00 (0x45d9c0)
    virtual void Release();                                                 // 0

    // 0x45da00: buff slots by class, then the buff records.
    void Init(DWORD dwStatus, CUser* pUser, CInstanceTrainingCampMember* pRecord,
              CDBRecordList* pBuffs);
    // 0x45da80
    void Cleanup();
    void ResetForPool()
    {
        Cleanup();
    }
    // 0x45dad0: the record, the status byte, the DWORD and the four WORDs.
    void WriteToMsg(CMsg* pMsg);

    // TrainingCampMember_IsRank0 / _IsMentor / _IsTrainee (0x45db50 / 0x45db70 / 0x45db90)
    BOOL IsLeader() const
    {
        return m_pRecord ? m_pRecord->MemberClass == TRAINING_CAMP_CLASS_LEADER : FALSE;
    }
    BOOL IsMentor() const
    {
        return m_pRecord ? m_pRecord->MemberClass == TRAINING_CAMP_CLASS_MENTOR : FALSE;
    }
    BOOL IsTrainee() const
    {
        return m_pRecord ? m_pRecord->MemberClass == TRAINING_CAMP_CLASS_TRAINEE : FALSE;
    }
    // TrainingCampMember_GetCharID (0x45dbb0) / _GetNameCStr (0x45dbc0)
    DWORD GetCharID() const
    {
        return m_pRecord ? m_pRecord->CharID : 0;
    }
    const char* GetName() const
    {
        return m_pRecord ? m_pRecord->CharName.c_str() : NULL;
    }
    // TrainingCampMember::IsTraineeBelowLv31 (0x45dbe0): 40 - joined level for a trainee that
    // joined at level 30 or below, otherwise 0.
    int GetGraduateHonor() const;

    // 0x45dc10
    void ClearBuffs();
    // 0x45dcd0: 6 for the leader, 2 for a trainee, 0 otherwise.
    int GetBuffSlotCount() const;
    // 0x45dd00
    void LoadBuffs(CDBRecordList* pBuffs);

public:
    DWORD                                       m_dwStatus;     // +04 -1 when free
    DWORD                                       m_dw08;         // +08
    WORD                                        m_aw0C[4];      // +0C
    DWORD                                       m_nBuffType4;   // +14 buffs of type 4
    DWORD                                       m_dwBuffType12; // +18 1: a buff of type 1 or 2
    BYTE                                        m_pad1C[0x14];  // +1C
    BYTE                                        m_bInUse;       // +30
    CInstanceTrainingCampMember*                m_pRecord;      // +34
    CUser*                                      m_pUser;        // +38 while online
    std::vector<TrainingCampMemberBuffData*>    m_vecBuff;      // +3C

    static CChunkAllocatorST<TrainingCampMember> s_Pool;
};

C_ASSERT(FIELD_OFFSET(TrainingCampMember, m_bInUse) == 0x30);
C_ASSERT(FIELD_OFFSET(TrainingCampMember, m_vecBuff) == 0x3C);
C_ASSERT(sizeof(TrainingCampMember) == 0x4C);

// ---------------------------------------------------------------------------------------------
// Camp

typedef std::map<DWORD, TrainingCampMember*> TrainingCampMemberMap;

class CTrainingCampCommon
{
public:
    CTrainingCampCommon();                                                  // 0x45de80
    // 0x45df50 (deleting 0x45dee0): Clear, then no records.
    virtual ~CTrainingCampCommon();                                         // 0
    // 0x45e0d0: the camp record and its members (with their buffs) from the DB.
    virtual BOOL Load(CInstanceTrainingCamp* pCamp);                       // 1
    // 0x45dfc0: frees the members and the camp record.
    virtual void Clear();                                                   // 2
    // 0x45e340
    virtual void AddMember(DWORD dwStatus, CInstanceTrainingCampMember* pRecord, CUser* pUser,
                           CDBRecordList* pBuffs);                          // 3
    // 0x45e460: the camp data selected by byMask, then the members.
    virtual void WriteToMsg(CMsg* pMsg, BYTE byMask);                      // 4
    // 0x45e3d0: member count byte and the members.
    virtual void WriteMembersToMsg(CMsg* pMsg);                            // 5

    // Guild_GetID_45E4F0 (0x45e4f0, the same function as CGuildCommon::GetID)
    DWORD GetID() const
    {
        return Guild_GetRecordID(m_pCamp);
    }
    // 0x45e490: NULL for 0 or an unknown character.
    TrainingCampMember* FindMember(DWORD dwCharID);
    // CTrainingCampCommon::IsMemberFlagSet (0x45e500)
    BOOL IsMemberLeader(DWORD dwCharID);
    // 0x45e530 / 0x45e5b0
    DWORD CountMentors();
    DWORD CountTrainees();
    // TrainingCamp_IsMentorFull (0x45e5a0, 2 mentors) / TrainingCamp_IsTraineeFull (0x45e620, 5)
    BOOL IsMentorFull()
    {
        return CountMentors() >= 2;
    }
    BOOL IsTraineeFull()
    {
        return CountTrainees() >= 5;
    }

public:
    CInstanceTrainingCamp*          m_pCamp;        // +04
    CInstanceTrainingCampMember*    m_pLeader;      // +08
    TrainingCampMemberMap           m_mapMember;    // +0C
};

C_ASSERT(FIELD_OFFSET(CTrainingCampCommon, m_mapMember) == 0x0C);
C_ASSERT(sizeof(CTrainingCampCommon) == 0x18);

// ---------------------------------------------------------------------------------------------
// Camp manager

typedef std::map<DWORD, CTrainingCampCommon*> TrainingCampMap;

class CTrainingCampMgrCommon
{
public:
    // CTrainingCampMgrCommon::StaticCtor (0x45e830, part of the manager's static constructor)
    CTrainingCampMgrCommon() {}
    // 0x45e900 (deleting 0x45e890): the camps are not freed.
    virtual ~CTrainingCampMgrCommon() {}                                    // 0
    // 0x45e950: every camp of _TrainingCamp ("WHERE ID <> 0") through AddCamp.
    virtual BOOL Load();                                                    // 1
    // 0x45eb30: clears and deletes every camp.
    virtual void Clear();                                                   // 2
    // Reloads everything; TRUE (the shard manager's vf03 always returns 1).
    virtual BOOL Reload() = 0;                                              // 3
    virtual CTrainingCampCommon* AddCamp(CInstanceTrainingCamp* pCamp) = 0; // 4

    // MgrCommon_Reset_441900 (0x441900): Clear, then Load.
    BOOL ClearAndLoad()
    {
        Clear();
        return Load();
    }

    // CTrainingCampMgrCommon::FindCamp (0x40be70): NULL for ID 0 or an unknown camp.
    CTrainingCampCommon* FindCamp(DWORD dwCampID)
    {
        if (!dwCampID)
            return NULL;
        TrainingCampMap::iterator it = m_mapCamp.find(dwCampID);
        if (it == m_mapCamp.end())
            return NULL;
        return it->second;
    }

public:
    TrainingCampMap     m_mapCamp;      // +04
};

C_ASSERT(FIELD_OFFSET(CTrainingCampMgrCommon, m_mapCamp) == 0x04);
C_ASSERT(sizeof(CTrainingCampMgrCommon) == 0x10);

// ---------------------------------------------------------------------------------------------
// DB loads

// TrainingCamp_DBLoadMember (0x45e630): the member record of dwCharID in dwCampID, NULL for 0 or
// a failed query.
CInstanceTrainingCampMember* TrainingCamp_DBLoadMember(DWORD dwCharID, DWORD dwCampID);
// TrainingCamp_DBLoadMemberBuffs (0x45e780): the buff records of a member by slot; TRUE when
// there is at least one.
BOOL TrainingCamp_DBLoadMemberBuffs(DWORD dwCampID, CDBRecordList* pList, DWORD dwCharID);
// TrainingCamp_DBLoadCamp (0x45ebf0)
CInstanceTrainingCamp* TrainingCamp_DBLoadCamp(DWORD dwCampID);
