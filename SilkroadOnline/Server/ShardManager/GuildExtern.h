#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

#include "BSLib/DB/Database.h"
#include "ServerCommon/InstanceRecords.h"
#include "ShardManager/SocialExtern.h"

// The parts of other SR_ShardManager groups that the guild, alliance and crest uploader code
// (SM_social1) calls, on top of the shared ones of SocialExtern.h. Only declarations; the owning
// groups supply the real classes (with these members at the offsets given) and the definitions.

// ---------------------------------------------------------------------------------------------
// The guild tables of the shard DB (CShardDB::GetTable).
enum
{
    SHARD_DB_TABLE_GUILD            = 0x0B,
    SHARD_DB_TABLE_GUILDMEMBER      = 0x0C,
    SHARD_DB_TABLE_ALLIEDCLANS      = 0x0D,
    SHARD_DB_TABLE_GUILDWAR         = 0x0E,
    SHARD_DB_TABLE_GPHISTORY        = 0x0F,
};

// ---------------------------------------------------------------------------------------------
// Provided by SM_main: the shard's own server data (0x8588B4); the shard ID is the WORD at +4.
struct SHARD_SELF_INFO
{
    DWORD   dw00;
    WORD    wShardID;       // +4
};

extern SHARD_SELF_INFO* g_pShardSelfInfo;           // 0x8588B4

// ---------------------------------------------------------------------------------------------
// Provided by SM_main [SM_social2]: CharName_IsValid (0x40f200), the name filter of the main
// process (g_pMainProcess is asserted before the call).
BOOL CharName_IsValid(const std::string& strName);

// ---------------------------------------------------------------------------------------------
// Provided by the transaction group [SM_social2]: the guild job of the hostile guild request
// (trs_guild_job, pool 0x4204b0); COverlappedJobMgr::StartJob runs it.
class trs_guild_job : public transaction
{
public:
    // trs_guild_job_PoolAlloc_4204B0 (0x4204b0)
    static trs_guild_job* Alloc();
};
