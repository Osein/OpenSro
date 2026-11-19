#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ShardManager/ShardExtern.h"

class CMsg;

// The users of the shard: the CClientManager and CUser functions of SM_world1 (the life cycle of
// the users, the server shutdown handling, the play time). The classes and their layout are in
// ShardExtern.h.

// User states (LOWORD) and steps (HIWORD) of CUser::m_dwState.
enum
{
    USER_STATE_IN_GAME_WORLD1   = 2,
    USER_STEP_SWITCH_WORLD1     = MAKELONG(1, 4),   // 0x6207 sent to the agent
    USER_STEP_LOGOUT_WORLD1     = MAKELONG(3, 3),   // 0x7804 sent to the game server
    USER_STEP_SWITCHED_WORLD1   = MAKELONG(1, 2),   // 0x7803 sent
};

// GameWorldContext::Reset (0x4294d0): world and layer 1, the rest 0.
void GameWorldContext_Reset(GameWorldContext* pContext);
// GameWorldContext_SetFromCharPos (0x4292e0)
void GameWorldContext_SetFromCharPos(GameWorldContext* pContext,
                                     const GameWorldCharPos* pChar);
// sUserLoginInfo_BindServerBody (0x429490): the game server of the character; the old session
// moves to dw150. FALSE for body 0 or a body without session.
BOOL sUserLoginInfo_BindServerBody(sUserLoginInfo* pInfo, WORD wBodyID);

// ---------------------------------------------------------------------------------------------
// The main process functions on the users (CMainProcess members of the original; SM_main calls
// them)

// CMainProcess::DisconnectUser (0x40e140): byFlags 1 logs the user out of its game server
// (0x7804), 4 removes it, 2 has the agent close the client (0x2206).
void MainProcess_DisconnectUser(DWORD dwJID, BYTE byFlags);
// CMainProcess::SendSwitchSession_7803 (0x40e9b0)
void MainProcess_SendSwitchSession(CUser* pUser);
// CMainProcess::OnMsg_A207 (0x40ea50): the agent's answer to the session switch.
void MainProcess_OnMsg_A207(CMsg* pMsg);
