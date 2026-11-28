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

// Provided by the character DB helpers (DB_Select_Char 0x484ee0): the name of the character.
std::string DB_Select_Char(int nCharID);

// Provided by the character manager group (0x85884C): non-zero once the character data is
// loaded (+200).
BOOL CharMgr_IsReady();

// Provided by the shard integration part of the main process (ShardIntegrate_OnQueryResult,
// 0xB450 answer): result of a rename job (bSucceeded 1 from OnComplete, 0 from OnFail).
class AQ_CharDataEditor2;
void ShardIntegrate_OnQueryResult(AQ_CharDataEditor2* pJob, BOOL bSucceeded);

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

// Provided by the main process group: *(g_pMainProcess + 0x42308), the task of the DB jobs
// (AQ_ServerEventLog requests).
CTask* MainProcess_GetDBJobTask();

// Provided by SM_world6 (ServerEvent.h): the record member (+4) of a server event / reward.
class CServerEvent;
class CServerEventReward;
class CInstanceServerEvent;
class CInstanceServerEventReward;
CInstanceServerEvent* ServerEvent_GetRecord(CServerEvent* pEvent);
CInstanceServerEventReward* ServerEventReward_GetRecord(CServerEventReward* pReward);
