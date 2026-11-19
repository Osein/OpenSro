#pragma once

#include <winsock2.h>
#include <windows.h>

#include <vector>

class CClimateManager;
class CMainProcess;

// World parts of the main process (CMainProcess functions of the original that work on other
// objects; free functions here, the main process calls them).

// CClimateManager_BroadcastWeather_3809 (0x40da30, InitGameWorld) and
// CClimateManager_BroadcastWeatherActive_3809 (0x40db50, CMainProcess::vf43): the climates
// UpdateAll reports (all of them with bForce) to the game servers as 0x3809 {BYTE count,
// climates}. pMgr is the climate manager of the main process (CMainProcess + 0x422F8).
void MainProcess_BroadcastWeather(CClimateManager* pMgr, bool bForce);

// TempleSeth_OpenGameWorld (0x416ea0, CMainProcess::OnMsg_7CE6): opens a layer of
// INS_TEMPLE_SETH and returns its world and layer ID. FALSE when it cannot.
BOOL TempleSeth_OpenGameWorld(WORD* pwWorldID, WORD* pwLayerID);

// SuspectedUser_LoadFromFile (0x415ca0, CShardManager::vf17): the JIDs of SuspectedUserJID.txt
// and SuspectedUser_SendCommand with them.
void SuspectedUser_LoadFromFile(CMainProcess* pProcess, DWORD dwCommand);
// SuspectedUser_SendCommand (0x415df0, also CMainProcess::OnMsg_4014): command 0 registers,
// 1 unregisters the JIDs of *pvecJID (0x3CC3 to the game servers); 2 and above log the
// registered ones (and append them to *pvecJID).
void SuspectedUser_SendCommand(std::vector<DWORD>* pvecJID, CMainProcess* pProcess,
                               DWORD dwCommand);

// UniqueSpawn_BroadcastL3Uniques_34BB (0x414450, CMainProcess::OnMsg_34BB and
// CShardSJ_UniqueSpawn::OnBegin): byFlag 0 spawns the eight level 3 uniques at their five
// places each (0x34BB to every game server), 1 asks the global manager (0x2327).
void UniqueSpawn_BroadcastL3Uniques_34BB(BYTE byFlag);

// A punishment period of the 0x6317 request (16 bytes).
struct sPunishmentPeriod
{
    DWORD   adw[4];
};

#pragma pack(push, 1)
// One slot of the punishment list: set flag and period (17 bytes).
struct sPunishmentSlot
{
    BYTE                bSet;       // +0
    sPunishmentPeriod   period;     // +1
};
#pragma pack(pop)

C_ASSERT(sizeof(sPunishmentSlot) == 17);

// sPunishment_SetSlot (0x41b380, CMainProcess::OnMsg_6317): slot nType - 3 of aSlots.
void sPunishment_SetSlot(int nType, const sPunishmentPeriod* pPeriod, sPunishmentSlot* aSlots);
