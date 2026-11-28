#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/DB/Database.h"
#include "BSLib/thread/Task.h"
#include "ServerFramework/ServerProcess.h"

// What the admin query jobs, the shard maintainer and the one-by-one processor use from other
// parts of SR_ShardManager. Only the calls made here are declared.

// Provided by the main process group (CMainProcess, 0x8A88F8). The main process is a
// CServerProcessBase; the jobs answer through its SendRelayReplyMassive (slot 15) and
// post messages to its task (GetOwner, slot 5).
extern ServerFramework::CServerProcessBase* g_pShardMainProcess;
// *(g_pShardMainProcess + 0x400CC): the task the admin query jobs report to.
CTask* MainProcess_GetJobOwnerTask();

// Provided by the shard DB group (CShardDB, 0x8A8948).
class CShardDB;
extern CShardDB* g_pShardDB;
// *(CShardDB + 8): the session pool of the shard database.
CDBSessionMgr* ShardDB_GetSession(CShardDB* pDB);
// *(CShardDB + 0x0C): the session pool of the log database.
CDBSessionMgr* ShardDB_GetLogSession(CShardDB* pDB);
// CDBConnPool_GetConnAt: the registered table nIndex (asserts and returns NULL when the table
// vector is shorter).
CDBTable* ShardDB_GetTable(CShardDB* pDB, DWORD nIndex);
// CShardDB::CheckSqlInjection: 1 when str contains a forbidden token (written to strFound).
int ShardDB_CheckSqlInjection(CShardDB* pDB, const std::string& str, std::string& strFound);

// Provided by the character DB helpers (0x484ee0, 0x412310).
// DB_Select_Char: the name of the character.
std::string DB_Select_Char(int nCharID);
// DB_AddLogChar: one row of the character log (_AddLogChar).
void DB_AddLogChar(int nCharID, int nJID, int nParam, BYTE byEventID, int nData);

// Provided by the character manager group (0x85884C): non-zero once the character data is
// loaded (+200).
BOOL CharMgr_IsReady();
// Provided by the certification group (GetGlobal_8588D0 / CertModule_FindByName): the server
// state rows of the shard, by module name.
struct SHARD_SERVER_STATE
{
    DWORD   dwReserved[4];
    DWORD   dwState;        // +10  4 / 5: stopped
    DWORD   dwModuleID;     // +14
};
typedef std::list<SHARD_SERVER_STATE*> ShardServerStateList;
ShardServerStateList* Cert_GetServerStateList();
// The row of this server (pExceptionObject in the analysis).
SHARD_SERVER_STATE* Cert_GetOwnServerState();
DWORD Cert_FindModuleIDByName(const char* pszModule);

// Provided by the shard integration part of the main process (ShardIntegrate_OnQueryResult,
// 0xB450 answer): result of a rename job (bSucceeded 1 from OnComplete, 0 from OnFail).
class AQ_CharDataEditor2;
void ShardIntegrate_OnQueryResult(AQ_CharDataEditor2* pJob, BOOL bSucceeded);

// Provided by the certification group (the own server record, 0x8588B4): its first WORD (the
// shard ID) and the body ID at +0x121 of the record at +0x1C (the GlobalManager the silk
// queries are relayed to).
WORD Cert_GetOwnShardID();
WORD Cert_GetGlobalManagerBodyID();

// Provided by the shard DB group (CDBObjectList_DetachAndClear, 0x40a7e0): every record of
// *pList goes back to pTable (CDBTable::FreeRecord), then the list is cleared.
void DB_FreeRecordList(CDBRecordList* pList, CDBTable* pTable);

// Provided by the client manager group (CClientManager 0x85884C, FindUserByCharName 0x4043f0):
// the user playing the character, NULL when it is not in game.
void* ClientMgr_FindUserByCharName(const std::string& strCharName);
// Provided by the main process group (CharName_IsValid 0x40f200 on the main process): the
// character name check of the shard (characters and length).
BOOL MainProcess_IsValidCharName(const std::string& strCharName);

// Provided by the guild group (CGuildMgrCommon::FindGuild 0x40bf10 on the guild manager, then
// the name of the guild): leaves strName unchanged when there is no such guild.
void GuildManager_FindGuildName(int nGuildID, std::string& strName);

// Provided by SM_world3 (ShardSchedule.h: CShard_ScheduleManager, the static object
// g_ShardScheduleManager 0x8a7b24, and sScheduleRow::Copy 0x663560). Only the calls of the
// schedule jobs are declared.
namespace NewSchedule
{
struct SCHEDULE_DATA;
}
// CShard_ScheduleManager::DumpSchedules (0x663aa0).
void ShardScheduleManager_DumpSchedules();
// g_ShardScheduleManager.AddSchedule / RemoveSchedule (CScheduleManager 0x6c0090 / 0x6c0290).
void ShardScheduleManager_AddSchedule(const NewSchedule::SCHEDULE_DATA* pData);
void ShardScheduleManager_RemoveSchedule(const NewSchedule::SCHEDULE_DATA* pData);
// sScheduleRow::Copy (0x663560).
NewSchedule::SCHEDULE_DATA* CopyScheduleRow(const NewSchedule::SCHEDULE_DATA* pSrc,
                                            NewSchedule::SCHEDULE_DATA* pDst);

// Provided by the guild group: CGuildMgrCommon::FindGuild (0x40bf10) on the guild manager
// (0x8A8924, asserted), NULL when there is no such guild; the name of a guild (0x4409a0).
void* GuildManager_FindGuild(int nGuildID);
void Guild_GetName(void* pGuild, std::string& strName);

// Provided by the main process group: *(g_pShardMainProcess + 0x42308), the task of the DB jobs
// (AQ_ServerEventLog requests).
CTask* MainProcess_GetDBJobTask();

// Provided by SM_world6 (ServerEvent.h): the record member (+4) of a server event / reward.
class CServerEvent;
class CServerEventReward;
class CInstanceServerEvent;
class CInstanceServerEventReward;
CInstanceServerEvent* ServerEvent_GetRecord(CServerEvent* pEvent);
CInstanceServerEventReward* ServerEventReward_GetRecord(CServerEventReward* pReward);
