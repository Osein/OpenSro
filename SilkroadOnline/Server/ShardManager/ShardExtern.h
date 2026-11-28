#pragma once

#include <winsock2.h>
#include <windows.h>

#include <hash_map>
#include <set>
#include <string>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/DB/Database.h"
#include "ServerCommon/PartyInfo.h"
#include "ServerFramework/ServerProcessMain.h"

// The stub declarations every SR_ShardManager group shares: the classes of SM_main and SM_world1
// (CMainProcess, CUser, CClientManager) and the packed local time. Only the members the ported
// groups call are declared; the owning groups define them (the CUser and CClientManager
// functions of SM_world1 are in ClientManagerWorld1.cpp). Group specific declarations stay in
// SocialExtern.h, GuildExtern.h, ShardWorldExtern.h and the other *Extern.h files.

class CMsg;
class CUser;
class CGameWorld;
class CInstancePC;
struct SERVER_BODY;
struct SERVER_CORD;
namespace ServerFramework { class CMassiveMsg; }

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess)

class CMainProcess : public ServerFramework::CServerProcessMain
{
public:
    // CMainProcess::CountReadyGameServers (0x4142c0): the SR_GameServer bodies of this shard
    // with a bound session.
    static int CountReadyGameServers();
    // CMainProcess::BroadcastToGameServers (0x412120): to every SR_GameServer of this shard but
    // the session nExcept (0: to all). The three do not use the object.
    static int BroadcastToGameServers(CMsg* pMsg, int nExcept);
    // CMainProcess::BroadcastToAllGameServers (0x4121e0)
    static int BroadcastToAllGameServers(CMsg* pMsg);
    // The CREST_FTP address of the configuration: the std::string at +0x42318 (the original
    // reads the member directly).
    const std::string& GetCrestFtpAddress() const;
};

// g_pMainProcess (0x8a88f8)
extern CMainProcess* g_pMainProcess;

// CharName_IsValid (0x40f200): the character name rule of the main process (the callers assert
// g_pMainProcess before the call).
BOOL CharName_IsValid(const std::string& strName);

// DB_AddLogChar (0x412310): one row of the character log (_AddLogChar).
void DB_AddLogChar(int nCharID, int nJID, int nParam, BYTE byEventID, int nData);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CShardDB)

class CShardDB
{
public:
    // +0x08
    CDBSessionMgr* GetSession();
    // +0x0C: the session pool of the log database.
    CDBSessionMgr* GetLogSession();
    // +0x18: the instance tables (index = slot; _Friend 0x14, _Memo 0x15, _ServerEvent 0x35,
    // _ServerEventReward 0x36). The callers check the size themselves (vector::at).
    std::vector<CDBTable*>& GetInstanceTables();
    // CDBConnPool_GetConnAt (0x4013d0, also inlined): table nIndex of the table vector at
    // +0x18; asserts and returns NULL when the vector is shorter.
    CDBTable* GetTable(int nIndex);
    // CShardDB::CheckSqlInjection (0x527390): 1 when str contains a forbidden token (written to
    // strFound).
    int CheckSqlInjection(const std::string& str, std::string& strFound);
};

// g_pShardDB (dword_8A8948); GetSingleton_CShardDB (0x401c10) asserts it.
extern CShardDB* g_pShardDB;
CShardDB* GetSingleton_CShardDB();

// CDBObjectList_DetachAndClear (0x40a7e0): frees every record of the list to pTable and clears it.
void CDBObjectList_DetachAndClear(CDBRecordList* pList, CDBTable* pTable);

// ---------------------------------------------------------------------------------------------
// Provided by SM_world1 / SM_main (CUser, CClientManager; ShardManager\User.h and
// ClientManager.h of the main branch). The layout is the one SM_world1 read from the code of
// 0x404450, 0x404ee0, 0x404bf0, 0x4059e0 and 0x40bca0; differences against the main branch:
// - the CUser pool is the allocator at +0x60 (OnPrepareUser allocates from it, ReleaseUser gives
//   back to it); the one at +0x04 holds the characters;
// - the hash map at +0x10C is keyed by CharID (RegisterCharacter, ReleaseUser);
// - the set at +0x134 holds the CUser pointers of every user (ReleaseUser, the ForEachUser
//   functions);
// - sUserLoginInfo +0x176 is the play time in seconds (CUser + 0x1EE).

// A character of the account (owner SM_main3).
class CCharacter
{
public:
    // The record at +4.
    CInstancePC* GetPCRecord() const;

public:
    void*       m_pVtbl;        // +0
    void*       m_pRecord;      // +4
};

struct sUserLoginInfo
{
    DWORD       dwJID;              // +000 CUser + 0x78
    DWORD       dwCharID;           // +004 CUser + 0x7C
    WORD        wAgentBodyID;       // +008 CUser + 0x80
    WORD        w00A;               // +00A
    DWORD       dwAgentSessionID;   // +00C CUser + 0x84
    DWORD       dwClientSessionID;  // +010 CUser + 0x88
    BYTE        b014;               // +014
    BYTE        b015;               // +015 CUser + 0x8D, the GM level of the account
    BYTE        b016;               // +016
    BYTE        b017;               // +017
    DWORD       dw018;              // +018 CUser + 0x90
    BYTE        ab01C[0x110];       // +01C
    std::string strCharName;        // +12C CUser + 0x1A4
    WORD        wGameServerBodyID;  // +148 CUser + 0x1C0
    WORD        w14A;               // +14A
    DWORD       dwGameServerSession;// +14C CUser + 0x1C4
    DWORD       dw150;              // +150 CUser + 0x1C8 the session of the previous game server
    std::string strAccountID;       // +154 CUser + 0x1CC
    DWORD       dw170;              // +170
    WORD        w174;               // +174
    WORD        w176;               // +176 CUser + 0x1EE play time (seconds)
};

C_ASSERT(FIELD_OFFSET(sUserLoginInfo, strCharName) == 0x12C);
C_ASSERT(FIELD_OFFSET(sUserLoginInfo, dwGameServerSession) == 0x14C);
C_ASSERT(sizeof(sUserLoginInfo) == 376);

// The game world position of a user (GameWorldContext, CUser + 0x1F0, 0x28 bytes with its
// vftable): set by CGameWorld::EnterLayer, read by CGameWorldMgr::OnUserLogout.
struct GameWorldContext
{
    void*       pVtbl;              // +00
    WORD        wWorldID;           // +04 CUser + 0x1F4
    WORD        wLayerID;           // +06 CUser + 0x1F6
    DWORD       adw08[2];           // +08
    WORD        wRegionID;          // +10 region X / Z bytes
    WORD        w12;                // +12
    float       fX;                 // +14
    float       fY;                 // +18
    float       fZ;                 // +1C
    DWORD       dwCreateTime;       // +20 packed create time of the layer
    CGameWorld* pGameWorld;         // +24 CUser + 0x214
};

C_ASSERT(FIELD_OFFSET(GameWorldContext, wRegionID) == 0x10);
C_ASSERT(FIELD_OFFSET(GameWorldContext, pGameWorld) == 0x24);
C_ASSERT(sizeof(GameWorldContext) == 0x28);

// The character position of the _Char record: CUser::SelectCharacter hands it to
// CGameWorldMgr::IsCharInLayer, GameWorldContext_SetFromCharPos reads it.
struct GameWorldCharPos
{
    BYTE        ab00[0x20];         // +00
    WORD        wWorldID;           // +20
    WORD        w22;                // +22
    WORD        wLayerID;           // +24
    WORD        w26;                // +26
    WORD        awCreateTime[6];    // +28 packed with RefObj_PackTypeID
    DWORD       dw34;               // +34
    WORD        wRegionID;          // +38
    WORD        w3A;                // +3A
    int         nPosX;              // +3C
    int         nPosY;              // +40
    int         nPosZ;              // +44
};

C_ASSERT(FIELD_OFFSET(GameWorldCharPos, awCreateTime) == 0x28);
C_ASSERT(FIELD_OFFSET(GameWorldCharPos, wRegionID) == 0x38);
C_ASSERT(FIELD_OFFSET(GameWorldCharPos, nPosZ) == 0x44);

class CUser
{
public:
    sPartyMemberInfo& GetPartyMemberInfo();     // +0x08 (its m_dwPartyID is the user's party)
    DWORD GetGuildID() const;                   // +0x6C (cleared when the member leaves)
    void SetGuildID(DWORD dwGuildID);
    DWORD GetTrainingCampID() const;            // +0x70
    void SetTrainingCampID(DWORD dwCampID);
    DWORD GetCampRegID() const;                 // +0x74 training camp recruit entry
    void SetCampRegID(DWORD dwRegID);
    DWORD GetJID() const;                       // +0x78
    DWORD GetCharID() const;                    // +0x7C
    const std::string& GetCharNameStr() const;  // +0x1A4
    DWORD GetSessionID() const;                 // +0x1C4
    DWORD GetDw1C8() const;                     // +0x1C8
    DWORD GetRefObjID() const;                  // +0x248
    DWORD GetMatchEntryID() const;              // +0x24C
    void SetMatchEntryID(DWORD dwID);
    BYTE GetByte250() const;                    // +0x250
    // +0x254: the name a user without party registers on the party matching board with.
    const std::string& GetMatchName() const;
    void SetMatchName(const std::string& strName);

    // Character_GetNameCStr_404360
    const char* GetCharName() const
    {
        return GetCharNameStr().c_str();
    }
    // +0x234 (2: in game)
    WORD GetState() const       { return LOWORD(m_dwState); }

    // Provided by SM_main3: CUser::Reset (0x4296f0), CUser::SelectCharacter,
    // CUser::DeleteCharTemporal (0x42a6a0).
    void Reset();
    BOOL SelectCharacter(const char* pszCharName, CMsg* pExtra, BYTE byMode, WORD* pwResult);
    BOOL DeleteCharTemporal(const char* pszCharName);

    // SM_world1
    // CUser::GetStateTimeoutMs (0x4298b0): ms the user may stay in its state, -1 for no limit.
    int GetStateTimeoutMs() const;
    // CUser::AddPlayTime (0x42ac20): in game only; 0x3122 when the meter rate level changes.
    void AddPlayTime(BYTE bySeconds);
    // CUser::SendPlayTime_3122 (0x42acd0)
    BYTE SendPlayTime();
    // CUser::NotifyStartPlayTime_2320 (0x42ad50): FRAMEWORKMSG_START_PLAYTIME_INGAME_NOTIFY
    // to the global manager; the user is disconnected when it cannot be sent.
    void NotifyStartPlayTime();

public:
    void*               m_pVtbl;            // +000
    void*               m_pCreateData;      // +004
    sPartyMemberInfo    m_PartyMember;      // +008 its m_dwPartyID at +068
    DWORD               m_dwGuildID;        // +06C
    DWORD               m_dwTrainingCampID; // +070
    DWORD               m_dwCampRegID;      // +074
    sUserLoginInfo      m_LoginInfo;        // +078
    GameWorldContext    m_GameWorld;        // +1F0
    BYTE                m_ab218[0x18];      // +218
    DWORD               m_dwStateTime;      // +230 ms in the state (TickUserTimeouts)
    DWORD               m_dwState;          // +234 LOWORD state, HIWORD step
    BYTE                ab238[0x0C];        // +238
    DWORD               m_dwCharCount;      // +244
    DWORD               m_dwRefObjID;       // +248
    DWORD               m_dwMatchEntryID;   // +24C
    BYTE                m_b250;             // +250
    std::string         m_strMatchName;     // +254
};

C_ASSERT(FIELD_OFFSET(CUser, m_dwGuildID) == 0x6C);
C_ASSERT(FIELD_OFFSET(CUser, m_LoginInfo) == 0x78);
C_ASSERT(FIELD_OFFSET(CUser, m_GameWorld) == 0x1F0);
C_ASSERT(FIELD_OFFSET(CUser, m_dwStateTime) == 0x230);
C_ASSERT(FIELD_OFFSET(CUser, m_dwCharCount) == 0x244);
C_ASSERT(FIELD_OFFSET(CUser, m_strMatchName) == 0x254);
C_ASSERT(sizeof(CUser) == 0x270);

class CClientManager
{
public:
    // CClientManager::dtor (0x40bca0)
    virtual ~CClientManager();

    // 0x404380 / 0x4043f0
    CUser* FindUserByJID(DWORD dwJID);
    CUser* FindUserByCharName(const std::string& strName);
    // The char ID -> user hash_map at +0x10C (filled by CClientManager::RegisterCharacter),
    // inlined in the original. NULL when not found.
    CUser* FindUserByCharID(DWORD dwCharID);

    // SM_world1
    // CClientManager::GetUserField244ByJID (0x404b00): the character count, -1 without user.
    int GetCharCountByJID(DWORD dwJID);
    // CClientManager::UserCall_42A6A0 (0x404ba0)
    BOOL DeleteCharTemporal(DWORD dwJID, const char* pszCharName);
    // CClientManager::RegisterCharacter (0x404bf0): selects the character (*pwResult 0x401 and
    // the result of CUser::SelectCharacter); mode 1 registers its name and CharID.
    BOOL RegisterCharacter(CUser* pUser, const char* pszCharName, BYTE byMode, CMsg* pExtra,
                           WORD* pwResult);
    // CClientManager::RemoveUserByJID (0x404e60)
    BOOL RemoveUserByJID(DWORD dwJID);
    // CClientManager::ReleaseUser (0x404ee0): out of the name, CharID and user maps and back to
    // the pool (the JID map entry stays with the caller).
    void ReleaseUser(CUser* pUser);
    // CClientManager::OnServerDisconnected (0x405110, CMainProcess::vf42)
    void OnServerDisconnected(SERVER_CORD* pCord, SERVER_BODY* pPeer);
    // 0x405150 / 0x405670
    void OnGameServerDown(SERVER_CORD* pCord);
    void OnAgentServerDown(SERVER_CORD* pCord);
    // CClientManager::TickUserTimeouts (0x405850, every 5 seconds)
    void TickUserTimeouts();
    // CClientManager::SerializeUserList (0x4058f0, CMainProcess::vf43): the shard ID, the user
    // count and per user dw018, JID, account ID, agent body, +0x170 (6 bytes) and the play
    // time. FALSE (nothing written) without users.
    bool SerializeUserList(ServerFramework::CMassiveMsg* pMsg);
    // CClientManager::ForEachUser_Set231F (0x4059e0) / ForEachUser_42AC20 (0x405a50)
    void SetPlayTimeFlag(WORD wFlag);
    void AddPlayTime(BYTE bySeconds);

public:
    CChunkAllocatorST<CCharacter>               m_CharPool;         // +004
    CChunkAllocatorST<CUser>                    m_UserPool;         // +060
    stdext::hash_map<DWORD, CUser*>             m_hmapUserByJID;    // +0BC
    stdext::hash_map<std::string, CUser*>       m_hmapUserByName;   // +0E4
    stdext::hash_map<DWORD, CUser*>             m_hmapUserByCharID; // +10C
    std::set<CUser*>                            m_setUser;          // +134
};

C_ASSERT(FIELD_OFFSET(CClientManager, m_UserPool) == 0x60);
C_ASSERT(FIELD_OFFSET(CClientManager, m_hmapUserByJID) == 0xBC);
C_ASSERT(FIELD_OFFSET(CClientManager, m_hmapUserByCharID) == 0x10C);
C_ASSERT(FIELD_OFFSET(CClientManager, m_setUser) == 0x134);
C_ASSERT(sizeof(CClientManager) == 0x140);

// GetSingleton_CClientManager (0x417c00, g_pClientManager 0x85884c, asserted)
CClientManager* GetSingleton_CClientManager();
extern CClientManager* g_pClientManager;            // 0x85884C

// ---------------------------------------------------------------------------------------------
// Provided by the guild war group (PackedDateTime_GetLocal 0x43f8c0 / PackedDateTime_Compare
// 0x43f830): the local time packed into a DWORD and the comparison of two of them.

void PackedDateTime_GetLocal(DWORD* pdwOut);
int PackedDateTime_Compare(const DWORD* pdwLeft, const DWORD* pdwRight);
