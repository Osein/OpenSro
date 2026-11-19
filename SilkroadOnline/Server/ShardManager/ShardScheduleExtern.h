#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

#include "BSLib/DB/Database.h"
#include "ServerFramework/ServerBody.h"
#include "ShardManager/ShardExtern.h"

class CMsg;

// What the shard schedule jobs use from other parts of SR_ShardManager. Only the calls made
// here are declared.

// Provided by SM_main (CMainProcess, g_pMainProcess, the siege schedule state):
// ShardWorldExtern.h.

// Provided by the certification group (0x8588B4, g_pServerBodyOfMyself in the logs): the body of
// this server (wID, nState).
SERVER_BODY* Shard_GetServerBodyOfMyself();

// Provided by SM_main (DB_SiegeFortressStatusUpdateForWeb): writes the fortress state of the
// siege fortress manager (SiegeFortress.h) for the web page.
class CSiegeFortressMgr;
void SiegeFortressMgr_StatusUpdateForWeb(CSiegeFortressMgr* pMgr);

// Provided by SM_main4 (String_Split, 0x6c9580): the trimmed tokens of str between the
// characters of strDelims.
void String_Split(const std::string& str, std::string strDelims,
                  std::vector<std::string>* pTokens);
