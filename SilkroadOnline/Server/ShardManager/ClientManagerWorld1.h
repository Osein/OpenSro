#pragma once

#include <winsock2.h>
#include <windows.h>

#include <hash_map>
#include <set>
#include <string>

#include "BSLib/BSDebug.h"
#include "BSLib/ChunkAllocator.h"

class CMsg;
struct SERVER_BODY;
struct SERVER_CORD;
namespace ServerFramework { class CMassiveMsg; }

// The users of the shard: the CClientManager and CUser functions of SM_world1 (the life cycle of
// the users, the server shutdown handling, the play time). The classes belong to SM_main
// (ShardManager\ClientManager.h and User.h of the main branch); the declarations below carry the
// members used here with the layout of the original and are replaced by those headers when the
// branches meet. Differences found against the main branch's headers:
// - the CUser pool is the allocator at +0x60 (OnPrepareUser allocates from it, ReleaseUser gives
//   back to it); the one at +0x04 holds the characters;
// - the hash map at +0x10C is keyed by CharID (RegisterCharacter, ReleaseUser);
// - the set at +0x134 holds the CUser pointers of every user (ReleaseUser, the ForEachUser
//   functions);
// - sUserLoginInfo +0x176 is the play time in seconds (CUser + 0x1EE).

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (ShardManager\User.h)

// A character of the account (owner SM_main3; only its size matters here).
class CCharacter
{
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
// vftable).
struct GameWorldContext
{
    void*       pVtbl;              // +00
    WORD        wWorldID;           // +04
    WORD        wLayerID;           // +06
    DWORD       adw08[2];           // +08
    WORD        wRegionID;          // +10 region X / Z bytes
    WORD        w12;                // +12
    float       fX;                 // +14
    float       fY;                 // +18
    float       fZ;                 // +1C
    DWORD       dwCreateTime;       // +20 packed create time of the layer
    DWORD       dw24;               // +24
};

C_ASSERT(sizeof(GameWorldContext) == 0x28);

// The character position GameWorldContext_SetFromCharPos reads (the _Char record).
struct GameWorldCharRecordPos
{
    BYTE        ab00[0x20];         // +00
    WORD        wWorldID;           // +20
    WORD        w22;                // +22
    WORD        wLayerID;           // +24
    WORD        w26;                // +26
    WORD        awCreateTime[6];    // +28
    DWORD       dw34;               // +34
    WORD        wRegionID;          // +38
    WORD        w3A;                // +3A
    int         nPosX;              // +3C
    int         nPosY;              // +40
    int         nPosZ;              // +44
};

C_ASSERT(FIELD_OFFSET(GameWorldCharRecordPos, wRegionID) == 0x38);
C_ASSERT(FIELD_OFFSET(GameWorldCharRecordPos, nPosZ) == 0x44);

class CUser
{
public:
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

    WORD GetState() const       { return LOWORD(m_dwState); }

public:
    void*               m_pVtbl;            // +000
    void*               m_pCreateData;      // +004
    BYTE                m_PartyInfo[0x60];  // +008
    DWORD               m_dwPartyID;        // +068
    DWORD               m_dwGuildID;        // +06C
    DWORD               m_dwTrainingCampID; // +070
    DWORD               m_dw074;            // +074
    sUserLoginInfo      m_LoginInfo;        // +078
    GameWorldContext    m_GameWorld;        // +1F0
    BYTE                m_ab218[0x18];      // +218
    DWORD               m_dwStateTime;      // +230 ms in the state (TickUserTimeouts)
    DWORD               m_dwState;          // +234 LOWORD state, HIWORD step
    BYTE                ab238[0x0C];        // +238
    DWORD               m_dwCharCount;      // +244
    DWORD               m_dw248;            // +248
    DWORD               m_dw24C;            // +24C
    BYTE                ab250[0x20];        // +250
};

C_ASSERT(FIELD_OFFSET(CUser, m_LoginInfo) == 0x78);
C_ASSERT(FIELD_OFFSET(CUser, m_GameWorld) == 0x1F0);
C_ASSERT(FIELD_OFFSET(CUser, m_dwStateTime) == 0x230);
C_ASSERT(FIELD_OFFSET(CUser, m_dwCharCount) == 0x244);
C_ASSERT(sizeof(CUser) == 0x270);

// User states (LOWORD) and steps (HIWORD) of CUser::m_dwState.
enum
{
    USER_STATE_IN_GAME_WORLD1   = 2,
    USER_STEP_SWITCH_WORLD1     = MAKELONG(1, 4),   // 0x6207 sent to the agent
    USER_STEP_LOGOUT_WORLD1     = MAKELONG(3, 3),   // 0x7804 sent to the game server
    USER_STEP_SWITCHED_WORLD1   = MAKELONG(1, 2),   // 0x7803 sent
};

// GameWorldContext::Reset (0x4294d0): world and layer 1, the rest 0.
void GameWorldContext_Reset(GameWorldContext* pContext);
// GameWorldContext_SetFromCharPos (0x4292e0)
void GameWorldContext_SetFromCharPos(GameWorldContext* pContext,
                                     const GameWorldCharRecordPos* pChar);
// sUserLoginInfo_BindServerBody (0x429490): the game server of the character; the old session
// moves to dw150. FALSE for body 0 or a body without session.
BOOL sUserLoginInfo_BindServerBody(sUserLoginInfo* pInfo, WORD wBodyID);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (ShardManager\ClientManager.h)

class CClientManager
{
public:
    // CClientManager::dtor (0x40bca0)
    virtual ~CClientManager();

    // SM_world1
    CUser* FindUserByJID(DWORD dwJID);                                  // 0x404380
    CUser* FindUserByCharName(const std::string& strName);              // 0x4043f0
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

// 0x85884C
extern CClientManager* g_pClientManager;

// ---------------------------------------------------------------------------------------------
// The main process functions on the users (CMainProcess members of the original; SM_main calls
// them)

// CMainProcess::DisconnectUser (0x40e140): byFlags 1 logs the user out of its game server
// (0x7804), 4 removes it, 2 has the agent close the client (0x2206).
void MainProcess_DisconnectUser(DWORD dwJID, BYTE byFlags);
// CMainProcess::SendSwitchSession_7803 (0x40e9b0)
void MainProcess_SendSwitchSession(CUser* pUser);
// CMainProcess::OnMsg_A207 (0x40ea50): the agent's answer to the session switch.
void MainProcess_OnMsg_A207(CMsg* pMsg);
