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
// MainProcess_PostDBJob from ServerEventExtern.h.

class CDBTable;
class CMainProcess;
class CMsg;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main3 (the statement builders of the AQ jobs)

// DB_UpdateAssociationReputation (0x465590): Init on the main task and
// "exec _UpdateAssociationReputation %d, %d, %d, %d" with the two pairs. FALSE when the text
// does not fit.
BOOL DB_UpdateAssociationReputation(ServerFramework::COverlapJob* pJob, const int* pnAnubis,
                                    const int* pnHaroeris);
// DB_Insert_LogServerEvent (0x47b2b0): "INSERT _LogServerEvent VALUES( GetDate(), %d, %d,
// '%s' )" with dwEventID, byType and strDesc.
BOOL DB_Insert_LogServerEvent(const std::string& strDesc, BYTE byType,
                              ServerFramework::COverlapJob* pJob, DWORD dwEventID);
// DB_Insert_LogSchedule (0x4735b0): "INSERT INTO _LogSchedule VALUES( ... )" with the values
// and the local time of dt.
BOOL DB_Insert_LogSchedule(ServerFramework::COverlapJob* pJob, std::string strModule,
                           int nServerID, std::string strDefine, DWORD dwScheduleID,
                           std::string strState, CDateTime dt);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess)

// CMainProcess + 0x42314 (sShardInfo::bTeenage, VIOLANCE_RESTRICT = TEENAGE).
BYTE MainProcess_IsTeenage();
// CMainProcess + 0x423D8: the JIDs registered as suspected users (std::set<DWORD>).
std::set<DWORD>& MainProcess_GetSuspectedUsers(CMainProcess* pProcess);
// ServerFramework::RelayMsgToBody_6008(g_pMainProcess, pMsg, global manager body, 0, 0, 0): the
// body ID is the WORD at +0x121 of the record at +0x1C of the shard's own server data
// (0x8588B4). The result of the relay.
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
