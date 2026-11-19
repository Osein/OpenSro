#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

#include "BSLib/DB/Database.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerProcess.h"

class CMsg;
struct RefGameWorld;

// What the shard schedule jobs use from other parts of SR_ShardManager. Only the calls made
// here are declared.

// Provided by the main process group (CMainProcess, 0x8A88F8).
extern ServerFramework::CServerProcessBase* g_pShardMainProcess;
// CMainProcess::BroadcastToAllGameServers: sends the message to every game server.
int MainProcess_BroadcastToAllGameServers(CMsg* pMsg);

// Siege schedule state of the main process (CMainProcess + 0x42310).
struct SHARD_SIEGE_SCHEDULE
{
    BYTE        ab00[0x25];
    BYTE        bSiegeRunning;      // +25 between siege begin and end
    BYTE        bTaxPeriod;         // +26 inside the tax period
    BYTE        b27;
    SYSTEMTIME  stPrevSiegeBegin;   // +28
    SYSTEMTIME  stSiegeBegin;       // +38
    SYSTEMTIME  stSiegeEnd;         // +48
    SYSTEMTIME  stTaxBegin;         // +58
    SYSTEMTIME  stTaxEnd;           // +68
};

C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, bSiegeRunning) == 0x25);
C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stPrevSiegeBegin) == 0x28);
C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stTaxEnd) == 0x68);

SHARD_SIEGE_SCHEDULE* MainProcess_GetSiegeSchedule();

// Provided by the certification group (0x8588B4, g_pServerBodyOfMyself in the logs): the body of
// this server (wID, nState).
SERVER_BODY* Shard_GetServerBodyOfMyself();

// Provided by the shard DB group (CShardDB, 0x8A8948).
class CShardDB;
extern CShardDB* g_pShardDB;
// *(CShardDB + 8): the session pool of the shard database.
CDBSessionMgr* ShardDB_GetSession(CShardDB* pDB);

// Provided by SM_world1 (Schedule_InsertLogAsync, 0x473880): queues an INSERT into _LogSchedule.
void Schedule_InsertLogAsync(std::string strModule, int nServerID, std::string strDefine,
                             DWORD dwScheduleID, std::string strState);

// Provided by SM_world4 (CSiegeFortressMgr, 0x8A8954).
class CSiegeFortressMgr;
extern CSiegeFortressMgr* g_pSiegeFortressMgr;
// CSiegeFortressMgr::InitSiegeScheduleTimes (0x668920).
void SiegeFortressMgr_InitSiegeScheduleTimes(CSiegeFortressMgr* pMgr);
// DB_SiegeFortressStatusUpdateForWeb: writes the fortress state for the web page.
void SiegeFortressMgr_StatusUpdateForWeb(CSiegeFortressMgr* pMgr);

// Provided by SM_world1 (CAssociationReputationMgr, 0x8A891C; GetSingleton asserts it).
class CAssociationReputationMgr;
extern CAssociationReputationMgr* g_pAssociationReputationMgr;
// DecideTempleWinner (0x434d60): nTemple 0 Anubis and Isis, 1 Haroeris and Seth.
BOOL AssociationReputationMgr_DecideTempleWinner(CAssociationReputationMgr* pMgr, int nTemple);
// The decided winners (members +28 / +2C; GetField_0x28 / GetField_0x2C in the analysis).
int AssociationReputationMgr_GetWinner28(CAssociationReputationMgr* pMgr);
int AssociationReputationMgr_GetWinner2C(CAssociationReputationMgr* pMgr);

// Provided by SM_world1 / SM_world2 (game worlds, CGameWorldMgr 0x8A8950).
class CGameWorld;
class CGameWorldLayer;
class CGameWorldMgr;
extern CGameWorldMgr* g_pGameWorldMgr;

// sGameWorldCreateParam (ctor 0x41ada0, dtor 0x4170a0).
struct sGameWorldCreateParam
{
    sGameWorldCreateParam();
    ~sGameWorldCreateParam();

    DWORD               dw00;           // +00
    BYTE                by04;           // +04
    DWORD               dw08;           // +08
    BYTE                by0C;           // +0C
    BYTE                by0D;           // +0D
    DWORD               dwDuration;     // +10 RefGameWorld::WorldDurationTime
    DWORD               dwEmptyRemain;  // +14 RefGameWorld::WorldEmptyRemainTime
    BYTE                by18;           // +18
    std::vector<DWORD>  vec1C;          // +1C
};

// CGameWorldMgr::FindWorld (0x65c050) and OpenLayer (0x65c200; *pResult 1 = opened).
CGameWorld* GameWorldMgr_FindWorld(CGameWorldMgr* pMgr, WORD wWorldID);
CGameWorldLayer* GameWorldMgr_OpenLayer(CGameWorldMgr* pMgr, BYTE* pResult, WORD wWorldID,
                                        sGameWorldCreateParam* pParam);
// **(WORD**)(world + 4): the ID of the world's reference row.
WORD GameWorld_GetRefID(CGameWorld* pWorld);
// Layer members +4 (layer ID) and +6C.
WORD GameWorldLayer_GetID(CGameWorldLayer* pLayer);
WORD GameWorldLayer_GetW6C(CGameWorldLayer* pLayer);
// GameWorld_BroadcastOpened_3C15 (0x65c900).
int GameWorld_BroadcastOpened(WORD wWorldID, CGameWorldLayer* pLayer);

// Provided by SM_world4 (schedule parameters, 0x666680 / 0x6669c0).
struct sTempleOpenSchedule;
// WORLD_OPEN_TIME / NOTIFY_BEFORE_OPEN of strParam into the open list (dwBegin + minutes).
BOOL ScheduleParam_BuildOpenTimeList(const std::string& strParam, sTempleOpenSchedule* pOpen,
                                     DWORD dwBegin);
// The value of strKey in strParam (atol) into *pnValue.
BOOL ScheduleParam_GetInt(const std::string& strParam, std::string strKey, int* pnValue);

// Provided by SM_main4 (String_Split, 0x6c9580): the trimmed tokens of str between the
// characters of strDelims.
void String_Split(const std::string& str, std::string strDelims,
                  std::vector<std::string>* pTokens);
