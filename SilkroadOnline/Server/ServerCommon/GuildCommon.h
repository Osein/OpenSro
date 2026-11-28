#pragma once

#include <windows.h>

#include <list>
#include <string>
#include <map>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/Msg.h"
#include "ServerCommon/ByteStream.h"
#include "ServerCommon/InstanceRecords.h"
#include "ServerCommon/PoolObject.h"

// Guild data shared by the shard manager and the game server: the guild member, the guild with
// its members, GP history, master candidates and hostile guilds, the alliance and the guild
// manager base with the guild, alliance and guild war maps.
//
// The functions that use the shard DB or the shard manager's vote and user classes (record
// loading and release, GP history, guild war records, master vote candidates) are the shard
// manager's build of these classes: ShardManager\GuildCommonShard.cpp. The rest is in
// GuildCommon.cpp.

class CUser;
class CVote;

// GuildMemberRecord_WriteToMsg (0x43f610): CharID, name, class, level, donation, permission,
// contribution, war kills / deaths, nickname, RefObjID, siege authority.
void GuildMemberRecord_WriteToMsg(CMsg* pMsg, const CInstanceGuildMember* pRecord);
// GuildMemberRecord_Serialize (0x445b90): the same fields into a byte stream.
void GuildMemberRecord_Serialize(const CInstanceGuildMember* pRecord, CByteStream* pStream);
// GuildRecord_Serialize (0x445ca0) / GuildRecord_WriteToMsg (0x445db0): the fields selected by
// bFlag: 1 ID, 2 name, 4 level, 8 gathered SP, 0x10 notice title and text, 0x20 current crest
// revision, 0x40 mercenary abilities.
void GuildRecord_Serialize(CByteStream* pStream, const CInstanceGuild* pRecord, BYTE bFlag);
void GuildRecord_WriteToMsg(CMsg* pMsg, const CInstanceGuild* pRecord, BYTE bFlag);
// GuildGPHistory_WriteToMsg (0x43fa00): ID, guild ID, time (time_t), name, GP, reason.
void GuildGPHistory_WriteToMsg(const CInstanceGPHistory* pRecord, CMsg* pMsg);

// One guild member (0x14 bytes, pooled; RTTI GuildMember).
class CGuildMember
{
public:
    // GuildMember::ctor (0x43fab0)
    CGuildMember()
    {
        m_dwState = (DWORD)-1;
        m_pRecord = NULL;
        m_pUser = NULL;
    }

    // GuildMember::vf00 (0x43fb40): back to the pool ("why do you about to delete already
    // deleted !!!" when it is not taken).
    virtual void Release();                                                 // 0

    // GuildMember_Alloc (0x43fae0): a member from the pool, marked as taken.
    static CGuildMember* Alloc();
    // GuildMember::ReleaseRecord (0x43fb80, GuildCommonShard.cpp): clears the user and the state
    // and returns the record to the guild member table.
    void ReleaseRecord();
    // The reset of Release / GuildMember_FreePtr before the member goes back to the pool.
    void ResetForPool()
    {
        ReleaseRecord();
    }
    // GuildMember_IsOffline (0x43fc10, misnamed): TRUE when the record's member class is 0,
    // i.e. the member is the guild master; FALSE without a record.
    BOOL IsMaster() const
    {
        if (!m_pRecord)
            return FALSE;
        return m_pRecord->MemberClass == 0;
    }
    // GuildMember_GetField1C (0x43fc90): the CharID of the record (0 without one).
    DWORD GetCharID() const
    {
        if (!m_pRecord)
            return 0;
        return m_pRecord->CharID;
    }
    // GuildMember_SetNickname (0x43fca0): record only, asserts without a record.
    void SetNickname(const std::string& strNickname);
    // GuildMember_WriteToMsgWithState (0x43fbe0) / GuildMember_Serialize_43FBC0 (0x43fbc0):
    // the record and the state byte.
    void WriteToMsg(CMsg* pMsg) const;
    void Serialize(CByteStream* pStream) const;

public:
    DWORD                   m_dwState;      // +04  -1 after construction
    BYTE                    m_bInUse;       // +08  taken from the pool
    CInstanceGuildMember*   m_pRecord;      // +0C
    CUser*                  m_pUser;        // +10  set while the member is logged in

    // The guild member pool (its in-use count is dword_89A404).
    static CChunkAllocatorST<CGuildMember> s_Pool;
};

C_ASSERT(FIELD_OFFSET(CGuildMember, m_dwState) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildMember, m_pRecord) == 0x0C);
C_ASSERT(FIELD_OFFSET(CGuildMember, m_pUser) == 0x10);
C_ASSERT(sizeof(CGuildMember) == 0x14);

// GuildMember_FreePtr (0x43fb10): returns a taken member to the pool and clears the pointer.
void GuildMember_FreePtr(CGuildMember*& pMember);

// An entry of the hostile guild map: the guild war and the side of this guild.
struct GUILD_HOSTILE_ENTRY
{
    DWORD               dwSide;     // +0   1 when this guild is the second guild of the war
    CInstanceGuildWar*  pWar;       // +4
};

C_ASSERT(sizeof(GUILD_HOSTILE_ENTRY) == 8);

typedef std::map<DWORD, CGuildMember*> GuildMemberMap;           // CharID -> member
typedef std::map<DWORD, GUILD_HOSTILE_ENTRY> GuildHostileMap;     // guild ID -> war

// The member limit per guild level 1..5 (dword_781A88).
extern const DWORD g_adwGuildMemberLimit[5];

// Guild_GetID_45E4F0 (0x45e4f0): the ID of a guild or training camp record (the record pointer
// at +4 of its owner), 0 without a record. CGuildCommon::GetID and CTrainingCampCommon::GetID
// are this one function.
template <class TRecord>
inline DWORD Guild_GetRecordID(const TRecord* pRecord)
{
    return pRecord ? pRecord->ID : 0;
}

// The guild (0x50 bytes with CGuild, which adds no data).
class CGuildCommon
{
public:
    // CGuildCommon::ctor (0x43fd40): the lock CharID and the level statistics stay
    // uninitialised.
    CGuildCommon();
    // CGuildCommon::dtor (0x43fe90): Clear, then the containers.
    virtual ~CGuildCommon();                                                // 0

    // CGuildCommon::vf01 (0x4401f0, GuildCommonShard.cpp): binds the record, loads the members
    // and the GP history of the last 30 days (after _DeleteGPHistory).
    virtual BOOL Initialize(CInstanceGuild* pRecord);                       // 1
    // CGuildCommon::vf02 (0x43ff50, GuildCommonShard.cpp): releases members, master vote
    // candidates, hostile entries, GP history and the record (nothing without the shard DB).
    virtual void Clear();                                                   // 2
    // CGuildCommon::vf03 (0x440610): a pooled member for the record (asserts a duplicate
    // CharID and keeps the old entry), then CountMembers.
    virtual DWORD AddMember(DWORD dwState, CInstanceGuildMember* pRecord, CUser* pUser);  // 3
    // CGuildCommon::vf04 / vf05 (0x440730 / 0x4406a0): the guild record fields of bFlag, the
    // member count and every member.
    virtual void WriteToMsg(CMsg* pMsg, BYTE bFlag);                        // 4
    virtual void Serialize(CByteStream* pStream, BYTE bFlag);              // 5
    // CGuildCommon::vf06 (0x4407d0): ID, then by bMask: 1 name, 2 level, 4 master name and
    // RefObjID ("Unknown", 0 without a master), 8 member count.
    virtual void WriteInfo(CMsg* pMsg, BYTE bMask);                        // 6
    // CGuildCommon::vf07 (0x440de0): there is a hostile entry for the guild.
    virtual BOOL IsHostileGuild(DWORD dwGuildID);                          // 7

    // Guild_GetID_45E4F0 (0x45e4f0): the ID of the record, 0 without one.
    DWORD GetID() const
    {
        return Guild_GetRecordID(m_pRecord);
    }
    // Guild_GetName (0x4409a0): the name of the record into strName (unchanged without one).
    void GetName(std::string& strName) const
    {
        if (m_pRecord)
            strName.assign(m_pRecord->Name, 0, std::string::npos);
    }
    // Guild_GetLevel_4409C0 (0x4409c0)
    BYTE GetLevel() const
    {
        return m_pRecord->Lvl;
    }
    // Guild_GetAllianceID_4409D0 (0x4409d0)
    DWORD GetAllianceID() const
    {
        return m_pRecord->Alliance;
    }

    // CGuildCommon::FindMember (0x4408a0): NULL for CharID 0 or an unknown member.
    CGuildMember* FindMember(DWORD dwCharID);
    // CGuildCommon::GetMemberSession (0x440a70, misnamed): TRUE when the member is the guild
    // master (FALSE when there is no such member; asserts without a record).
    BOOL IsMemberMaster(DWORD dwCharID);
    // CGuildCommon::MemberHasPermission8 (0x440aa0)
    BOOL MemberHasPermission8(DWORD dwCharID);
    // CGuildCommon::HasMasterCandidate (0x440c80)
    BOOL HasMasterCandidate(BYTE bKey);
    // Guild_PushCandidate_440C60 (0x440c60)
    void PushCandidate(CVote* pVote)
    {
        m_vecCandidate.push_back(pVote);
    }
    // GuildMemberCursor_Find (0x440910) / GuildMemberCursor_Reset (0x440930): the member that
    // holds the guild lock (permission 8).
    void SetLockMember(DWORD dwCharID);
    void ResetLockMember()
    {
        m_pLockMember = NULL;
        m_dwLockCharID = 0;
    }

    // CGuildCommon::FindMemberEntry (0x435ce0): the hostile entry of the guild, NULL when
    // there is none.
    GUILD_HOSTILE_ENTRY* FindHostileEntry(DWORD dwGuildID);

    // CGuildCommon::CountMembers (0x4409e0): the highest and the average member level; the
    // average is the member count's quotient (the map is never empty here). Returns it.
    DWORD CountMembers();
    // CGuildCommon::IsMemberCountFull (0x440ad0): level 0 asserts and counts as level 1.
    BOOL IsMemberCountFull();
    // CGuildCommon::LoadMemberRecord (0x440b10, GuildCommonShard.cpp): the member record of the
    // character from the DB, NULL when it cannot be read.
    static CInstanceGuildMember* LoadMemberRecord(DWORD dwCharID, DWORD dwGuildID);
    // CGuildCommon::RemoveMasterCandidate (0x440cd0): the first candidate vote with that ID.
    void RemoveMasterCandidate(DWORD dwVoteID);
    // CGuildCommon::AddHostileGuild / RemoveHostileGuild (0x440d30 / 0x440e00): the entry of
    // the other guild of the war. Add asserts and returns FALSE when it exists, Remove returns
    // FALSE when there is none.
    BOOL AddHostileGuild(CInstanceGuildWar* pWar);
    BOOL RemoveHostileGuild(CInstanceGuildWar* pWar);
    // CGuildCommon::AddGPHistory (0x440f00): to the front (newest) with bFront 1, else to the
    // back; FALSE (assert) for NULL.
    BOOL AddGPHistory(CInstanceGPHistory* pRecord, BYTE bFront);
    // CGuildCommon::PopGPHistory (0x440f60, GuildCommonShard.cpp): releases the bCount oldest
    // entries.
    void PopGPHistory(BYTE bCount);
    // CGuildCommon::WriteLastGPHistory (0x441030): the newest entry, nothing when empty.
    void WriteLastGPHistory(CMsg* pMsg);

public:
    CInstanceGuild*                 m_pRecord;          // +04
    CInstanceGuildMember*           m_pMasterRecord;    // +08
    CGuildMember*                   m_pLockMember;      // +0C
    std::list<CInstanceGPHistory*>  m_listGPHistory;    // +10
    GuildMemberMap                  m_mapMember;        // +1C
    DWORD                           m_dwLockCharID;     // +28
    std::vector<CVote*>             m_vecCandidate;     // +2C
    GuildHostileMap                 m_mapHostile;       // +3C
    DWORD                           m_dwMaxLevel;       // +48  CountMembers
    DWORD                           m_dwAvgLevel;       // +4C
};

C_ASSERT(FIELD_OFFSET(CGuildCommon, m_pRecord) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_listGPHistory) == 0x10);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_mapMember) == 0x1C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_dwLockCharID) == 0x28);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_vecCandidate) == 0x2C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_mapHostile) == 0x3C);
C_ASSERT(FIELD_OFFSET(CGuildCommon, m_dwMaxLevel) == 0x48);
C_ASSERT(sizeof(CGuildCommon) == 0x50);

// An alliance (union) of up to eight guilds (8 bytes, stored by value in the alliance map).
// The DB functions are the shard manager's (ShardManager\Guild.cpp).
class CAlliance
{
public:
    // CAlliance::ctor (0x433f70)
    CAlliance()
    {
        m_pRecord = NULL;
    }
    // CAlliance::dtor (0x433fa0): nothing but the vtable.
    virtual ~CAlliance() {}                                                 // 0

    // CAlliance_HasGuild (0x4342e0)
    BOOL HasGuild(DWORD dwGuildID) const
    {
        for (BYTE i = 0; i < 8; ++i)
        {
            if (m_pRecord->Ally[i] == (int)dwGuildID)
                return TRUE;
        }
        return FALSE;
    }
    // CAlliance_CountGuilds (0x434300, misnamed): the first empty slot, 0 when all eight are
    // used.
    BYTE FindEmptySlot() const
    {
        for (BYTE i = 0; i < 8; ++i)
        {
            if (!m_pRecord->Ally[i])
                return i;
        }
        return 0;
    }
    // CAlliance::CountGuilds (0x434320): all used slots.
    BYTE CountGuilds() const
    {
        BYTE n = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (m_pRecord->Ally[i])
                ++n;
        }
        return n;
    }
    // CAlliance_GetMasterGuildID (0x434340): slot 0, asserted.
    DWORD GetMasterGuildID() const
    {
        DWORD dwGuildID = m_pRecord->Ally[0];
        if (!dwGuildID)
            BSAssertFail();
        return dwGuildID;
    }
    // CAlliance_GetCrestField3C / 40 (0x434360 / 0x434370)
    int GetLastCrestRev() const
    {
        return m_pRecord->LastCrestRev;
    }
    int GetCurCrestRev() const
    {
        return m_pRecord->CurCrestRev;
    }

    // Shard manager (ShardManager\Guild.cpp).
    void ReleaseRecord();                                   // 0x433fc0
    WORD RemoveGuildSlot(DWORD dwGuildID);                  // 0x434040
    WORD DBRemove();                                        // 0x4341e0
    WORD SetCrestRev(int nCrestRev);                        // 0x434380
    WORD AddGuild(class CGuild* pGuild, BYTE bSlot);        // 0x43bca0

public:
    CInstanceAlliedClan*    m_pRecord;      // +4
};

C_ASSERT(sizeof(CAlliance) == 8);

typedef std::map<DWORD, CGuildCommon*> GuildMap;
typedef std::map<DWORD, CAlliance> AllianceMap;
typedef std::map<DWORD, CInstanceGuildWar*> GuildWarMap;

// The guild manager base (0x28 bytes).
class CGuildMgrCommon
{
public:
    // CGuildMgrCommon::ctor (0x441060)
    CGuildMgrCommon() {}
    // CGuildMgrCommon::dtor (0x4411f0)
    virtual ~CGuildMgrCommon() {}                                           // 0

    // CGuildMgrCommon::vf01 (0x441260, GuildCommonShard.cpp): every guild (ID <> 0), alliance
    // (ID <> 0) and guild war, then LinkGuildWars.
    virtual BOOL Load();                                                    // 1
    // CGuildMgrCommon::vf02 (0x4416e0, GuildCommonShard.cpp): clears and deletes the guilds,
    // releases alliance and war records.
    virtual void Clear();                                                   // 2
    // Reloads everything; TRUE (the shard manager's CGuildManager::vf03 always returns 1).
    virtual BOOL Reset() = 0;                                               // 3
    virtual CGuildCommon* CreateGuild(CInstanceGuild* pRecord) = 0;         // 4
    virtual BOOL AddHostileGuilds(CInstanceGuildWar* pWar) = 0;             // 5
    virtual BOOL RemoveHostileGuilds(CInstanceGuildWar* pWar) = 0;          // 6
    virtual WORD OnCombatResult(CInstanceGuildWar* pWar, CGuildMember* pKiller,
                                CGuildMember* pVictim, int nPoint,
                                const std::string& strKillerName,
                                const std::string& strVictimName) = 0;     // 7
    // CGuildMgrCommon::vf08 (0x441ef0): the alliance of the record's ID (a new one when there
    // is none), bound to the record.
    virtual CAlliance* AddAlliance(CInstanceAlliedClan* pRecord);           // 8
    // CGuildMgrCommon::vf09 (0x442030): NULL for ID 0 or an unknown alliance.
    virtual CAlliance* FindAlliance(DWORD dwAllianceID);                    // 9
    // CGuildMgrCommon::vf10 (0x4420f0, GuildCommonShard.cpp): releases the record and erases.
    virtual void RemoveAlliance(DWORD dwAllianceID);                        // 10
    // CGuildMgrCommon::vf11 (0x442090): the alliance has the guild (FALSE when unknown).
    virtual BOOL AllianceHasGuild(DWORD dwAllianceID, DWORD dwGuildID);    // 11

    // CGuildMgrCommon::FindGuild (0x40bf10): NULL for ID 0 or an unknown guild.
    CGuildCommon* FindGuild(DWORD dwGuildID);

    // CGuildMgrCommon::LinkGuildWars (0x441920): AddHostileGuilds for every war.
    void LinkGuildWars();
    // CGuildMgrCommon::AddGuildWar (0x441990): FALSE when the war ID is known.
    BOOL AddGuildWar(CInstanceGuildWar* pWar);
    // CGuildMgrCommon::FindGuildWar (0x441a10)
    CInstanceGuildWar* FindGuildWar(DWORD dwWarID);
    // CGuildMgrCommon::RemoveGuildWar (0x441a70, GuildCommonShard.cpp): unlinks, erases and
    // releases the record.
    BOOL RemoveGuildWar(CInstanceGuildWar* pWar);
    // CGuildMgrCommon::LoadGuildById / LoadAllianceById (0x441b50 / 0x441d20,
    // GuildCommonShard.cpp): the record of a new guild / alliance from the DB, NULL on failure.
    static CInstanceGuild* LoadGuildById(DWORD dwGuildID);
    static CInstanceAlliedClan* LoadAllianceById(DWORD dwAllianceID);

public:
    GuildMap        m_mapGuild;         // +04
    AllianceMap     m_mapAlliance;      // +10
    GuildWarMap     m_mapGuildWar;      // +1C
};

C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapGuild) == 0x04);
C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapAlliance) == 0x10);
C_ASSERT(FIELD_OFFSET(CGuildMgrCommon, m_mapGuildWar) == 0x1C);
C_ASSERT(sizeof(CGuildMgrCommon) == 0x28);
