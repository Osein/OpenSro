#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ShardManager/ShardExtern.h"

// What the game world code needs from parts of SR_ShardManager that are ported elsewhere. Only
// the calls and fields it uses are declared here; the owners replace these declarations.
// CMainProcess, CUser, CClientManager and PackedDateTime_GetLocal are the shared ones of
// ShardExtern.h.

class CMsg;
class CUser;
class CGameWorld;

// ---------------------------------------------------------------------------------------------
// Provided by the CUser / CClientManager owner (SM_world1 / SM_main)

// &CUser + 0x1F0 (GameWorldContext of ShardExtern.h).
GameWorldContext* User_GetGameWorldPos(CUser* pUser);
// ShardMgr8A894C_CheckByte141 (0x42ab90): the GM level check of the user's byte +141 on the GM
// level manager (dword_8A894C, asserts when there is none).
BOOL User_HasGMLevelForIP(CUser* pUser);


// ---------------------------------------------------------------------------------------------
// Used by the flag world, the battle arena and the siege fortress (SM_world4)

// Provided by SM_main: the siege schedule state of the main process (CMainProcess + 0x42310),
// used by the siege jobs and CSiegeFortressMgr::InitSiegeScheduleTimes.
struct SHARD_SIEGE_SCHEDULE
{
    BYTE        ab00[0x24];
    BYTE        bRequestPeriod;     // +24 inside the siege request period
    BYTE        bSiegeRunning;      // +25 between siege begin and end
    BYTE        bTaxPeriod;         // +26 inside the tax period
    BYTE        b27;
    SYSTEMTIME  stPrevSiegeBegin;   // +28
    SYSTEMTIME  stSiegeBegin;       // +38
    SYSTEMTIME  stSiegeEnd;         // +48
    SYSTEMTIME  stPeriodBegin;      // +58 the request or tax period that began last (at start:
                                    //     the next SCHEDULE_DEF_SIEGE_REQUEST_PERIOD period)
    SYSTEMTIME  stPeriodEnd;        // +68
};

C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, bSiegeRunning) == 0x25);
C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stPrevSiegeBegin) == 0x28);
C_ASSERT(FIELD_OFFSET(SHARD_SIEGE_SCHEDULE, stPeriodEnd) == 0x68);

SHARD_SIEGE_SCHEDULE* MainProcess_GetSiegeSchedule();

// Provided by SM_main: CMainProcess + 0x423B0: one BYTE per battle arena match type (occupy, score,
// flag), 0 while the match type is switched off.
BYTE MainProcess_IsArenaMatchOpen(BYTE byMatch);
