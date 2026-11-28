#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/DB/Database.h"
#include "BSLib/thread/Task.h"
#include "ShardManager/ShardExtern.h"

// What the admin query jobs, the shard maintainer and the one-by-one processor use from other
// parts of SR_ShardManager on top of the shared declarations of ShardExtern.h (g_pMainProcess,
// CShardDB, the client manager, CharName_IsValid, DB_AddLogChar). Only the calls made here are
// declared.

// Provided by the main process group: *(g_pMainProcess + 0x400CC), the task the admin query
// jobs report to.
CTask* MainProcess_GetJobOwnerTask();

// Provided by SM_db (g_pShardDB->GetLogTable): table nIndex of the log table keeper at
// CShardDB + 0x34 (CDBManager_GetSessionByIndex 0x46fd00, called by AQ_LogSelector and inlined
// in AQ_ScheduleLog, AQ_ServerEventLog and AQ_SiegeLogSelector); asserts and returns NULL when
// the index is out of range.
CDBTable* ShardDB_GetLogTable(int nIndex);

// Provided by the character DB helpers (DB_Select_Char 0x484ee0): the name of the character.
std::string DB_Select_Char(int nCharID);

// Provided by the character manager group (0x85884C): non-zero once the character data is
// loaded (+200).
BOOL CharMgr_IsReady();

// Provided by the shard integration part of the main process (ShardIntegrate_OnQueryResult,
// 0xB450 answer): result of a rename job (bSucceeded 1 from OnComplete, 0 from OnFail).
class AQ_CharDataEditor2;
void ShardIntegrate_OnQueryResult(AQ_CharDataEditor2* pJob, BOOL bSucceeded);

// Provided by the main process group: *(g_pMainProcess + 0x42308), the task of the DB jobs
// (AQ_ServerEventLog requests).
CTask* MainProcess_GetDBJobTask();
