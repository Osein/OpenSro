#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

#include "BSLib/DB/Database.h"
#include "ServerFramework/ServerBody.h"

class CMsg;

// What the shard schedule jobs use from other parts of SR_ShardManager. Only the calls made
// here are declared.

// Provided by SM_main (CMainProcess, g_pMainProcess, the siege schedule state):
// ShardWorldExtern.h.

// Provided by the certification group (0x8588B4, g_pServerBodyOfMyself in the logs): the body of
// this server (wID, nState).
SERVER_BODY* Shard_GetServerBodyOfMyself();

// Provided by the shard DB group (CShardDB, 0x8A8948).
class CShardDB;
extern CShardDB* g_pShardDB;
// *(CShardDB + 8): the session pool of the shard database.
CDBSessionMgr* ShardDB_GetSession(CShardDB* pDB);

// Provided by SM_main (DB_SiegeFortressStatusUpdateForWeb): writes the fortress state of the
// siege fortress manager (SiegeFortress.h) for the web page.
class CSiegeFortressMgr;
void SiegeFortressMgr_StatusUpdateForWeb(CSiegeFortressMgr* pMgr);

// Provided by SM_world1 (CAssociationReputationMgr, 0x8A891C; GetSingleton asserts it).
class CAssociationReputationMgr;
extern CAssociationReputationMgr* g_pAssociationReputationMgr;
// DecideTempleWinner (0x434d60): nTemple 0 Anubis and Isis, 1 Haroeris and Seth.
BOOL AssociationReputationMgr_DecideTempleWinner(CAssociationReputationMgr* pMgr, int nTemple);
// The decided winners (members +28 / +2C; GetField_0x28 / GetField_0x2C in the analysis).
int AssociationReputationMgr_GetWinner28(CAssociationReputationMgr* pMgr);
int AssociationReputationMgr_GetWinner2C(CAssociationReputationMgr* pMgr);

// Provided by SM_main4 (String_Split, 0x6c9580): the trimmed tokens of str between the
// characters of strDelims.
void String_Split(const std::string& str, std::string strDelims,
                  std::vector<std::string>* pTokens);
