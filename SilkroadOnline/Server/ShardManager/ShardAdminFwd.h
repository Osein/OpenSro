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
