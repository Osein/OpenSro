#pragma once

#include <windows.h>

#include <vector>

#include "ShardManager/ShardAdminExtern.h"
#include "ShardManager/ShardExtern.h"
#include "ShardManager/ShardLogJobs.h"

// What the server events use from parts of SR_ShardManager that are ported elsewhere. Only the
// calls made here are declared; the owners replace these declarations. CMainProcess,
// g_pMainProcess and the client manager come from ShardExtern.h, the AQ_ServerEvent job from
// ShardAdminExtern.h, the server event log rows from ShardLogJobs.h.

class CDBTable;
class CMsg;
class CServerEvent;
class CServerEventReward;
class CServerEventMgr;

// ---------------------------------------------------------------------------------------------
// Provided by SM_main (CMainProcess; CShardDB is in ShardExtern.h)

// The DB job queue of the main process: g_pMainProcess->vf19 (vftable +0x4C) with the job and
// the DB thread member at CMainProcess + 0x42308.
void MainProcess_PostDBJob(void* pJob);

// ---------------------------------------------------------------------------------------------
// Provided by SM_main4 (ProcessingEvent, 0x67e8b0)

// One detecting notification of a game server: dwTarget, byType, dwValue and dwCount as read
// by CServerEventMgr::OnDetectingNotify.
BOOL ServerEvent_ProcessingEvent(CServerEventMgr* pMgr, DWORD dwTarget, BYTE byType,
                                 DWORD dwValue, DWORD dwCount);
