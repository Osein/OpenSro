#pragma once

#include <winsock2.h>
#include <windows.h>

#include <set>
#include <string>
#include <vector>

#include "BSLib/BSDebug.h"
#include "BSLib/DB/Database.h"
#include "BSLib/Util/TimeSpan.h"
#include "ServerFramework/OverlapJob.h"
#include "ShardManager/ServerEventExtern.h"

// What the association reputation, the log jobs and the other SM_world1 parts use from parts of
// SR_ShardManager that are ported elsewhere. Only the calls made here are declared; the owners
// replace these declarations. CShardDB, g_pMainProcess and CMainProcess come from ShardExtern.h,
// MainProcess_PostDBJob from ServerEventExtern.h. The statement builders of the log and
// reputation jobs are with the admin query jobs (AdminQueryJobs.h, AQScheduleJobs.h,
// AQServerEvent.h).

class CDBTable;
class CMainProcess;
class CMsg;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess)

// CMainProcess + 0x42314 (sShardInfo::bTeenage, VIOLANCE_RESTRICT = TEENAGE).
BYTE MainProcess_IsTeenage();
// CMainProcess + 0x423D8: the JIDs registered as suspected users (std::set<DWORD>).
std::set<DWORD>& MainProcess_GetSuspectedUsers(CMainProcess* pProcess);
// ServerFramework::RelayMsgToBody_6008(g_pMainProcess, pMsg, global manager body, 0, 0, 0): the
// body ID is g_pSelfBody->pDivision->wGlobalManagerID (g_pSelfBody 0x8588B4). The result of the
// relay.
BOOL MainProcess_RelayToGlobalManager(CMsg* pMsg);
// CMainProcess::SendToServerBody (0x412290).
BOOL MainProcess_SendToServerBody(CMsg* pMsg, WORD wBodyID, DWORD dwJID);
// CMainProcess_GetServerBodyField13: the session of the server body wBodyID, 0 without one.
DWORD MainProcess_GetBodySession(WORD wBodyID);
// MeterRateConfig_GetStatic (0x41b9b0): the two meter rate thresholds in hours
// (HourForMeterRateLevel).
const BYTE* MeterRateConfig_GetStatic();
// DB_AddLogChar (0x412310)
void DB_AddLogChar(int nCharID, int nJID, int nParam, BYTE byEventID, int nData);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main3 (CUser)

class CUser;
// CUser_SetPlayTimeFlagAndSend (CMainProcess::OnMsg_231F per user).
void CUser_SetPlayTimeFlagAndSend(WORD wFlag, CUser* pUser);
