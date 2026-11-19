#pragma once

#include <windows.h>

#include <vector>

// What the server events use from parts of SR_ShardManager that are ported elsewhere. Only the
// calls made here are declared; the owners replace these declarations. CMainProcess,
// g_pMainProcess and ClientManager_FindUserByJID come from ShardWorldExtern.h.

class CDBTable;
class CMsg;
class CUser;
class CServerEvent;
class CServerEventReward;
class CServerEventMgr;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CShardDB, g_pShardDB = dword_8A8948)

class CShardDB;
extern CShardDB* g_pShardDB;
// CShardDB + 0x18: the table bindings by table index (0x35 _ServerEvent, 0x36
// _ServerEventReward). The callers check the size themselves (vector::at in the original).
std::vector<CDBTable*>& ShardDB_GetTables(CShardDB* pDB);
// CDBConnPool_GetConnAt (0x4013d0): the table binding of nIndex.
CDBTable* ShardDB_GetTable(CShardDB* pDB, int nIndex);

// The DB job queue of the main process: g_pMainProcess->vf19 (vftable +0x4C) with the job and
// the DB thread member at CMainProcess + 0x42308.
void MainProcess_PostDBJob(void* pJob);

// ---------------------------------------------------------------------------------------------
// Provided by SM_world1 (AQ_ServerEvent and the server event log)

class AQ_ServerEvent;
// UserCertifier_AllocJob (0x47a8b0): an AQ_ServerEvent from its pool, NULL when there is none.
AQ_ServerEvent* AQ_ServerEvent_Alloc();
// AQ_ServerEvent::InitUpdate (0x47a960): the update of the event record.
BOOL AQ_ServerEvent_InitUpdate(AQ_ServerEvent* pJob, CServerEvent* pEvent);
// AQ_ServerEvent::InitReward (0x47a9d0): nKind 2 insert, 3 delete, 4 update of the reward record.
BOOL AQ_ServerEvent_InitReward(AQ_ServerEvent* pJob, CServerEventReward* pReward, int nKind);
// The job's vftable slot 3 (+0x0C), called with the job itself as argument: back to the pool.
void AQ_ServerEvent_Free(AQ_ServerEvent* pJob);
// ServerEventLog_LogDetecting (0x47b600) / ServerEventLog_LogCompletion (0x47b7f0): the start
// and end rows of the server event log. FALSE when no log job could be posted.
BOOL ServerEventLog_LogDetecting(CServerEvent* pEvent);
BOOL ServerEventLog_LogCompletion(CServerEvent* pEvent);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main4 (ProcessingEvent, 0x67e8b0)

// One detecting notification of a game server: dwTarget, byType, dwValue and dwCount as read
// by CServerEventMgr::OnDetectingNotify.
BOOL ServerEvent_ProcessingEvent(CServerEventMgr* pMgr, DWORD dwTarget, BYTE byType,
                                 DWORD dwValue, DWORD dwCount);

// ---------------------------------------------------------------------------------------------
// Provided by the CUser owner

// CUser + 0x1A8: the name of the selected character (std::string).
const char* User_GetCharName(CUser* pUser);
