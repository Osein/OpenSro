#include "ShardManager/CharDataEditor.h"

#include <stdarg.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"
#include "ServerFramework/ServerProcess.h"
#include "ShardManager/AQBase.h"
#include "ShardManager/AdminQueryJobs.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

namespace
{

// The statement formatting of the editors: a failed StringCchPrintfA asserts and throws an int
// that the request reader catches as an invalid message.
void CheckFormatted(HRESULT hr)
{
    if (hr != S_OK)
    {
        BSAssertFail();
        throw 0;
    }
}

void FormatOrThrow(char* psz, size_t cch, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    HRESULT hr = StringCchVPrintfA(psz, cch, pszFormat, args);
    va_end(args);
    CheckFormatted(hr);
}

// How CCharDataSelector reads the argument of a query type.
enum
{
    CDS_NAME,               // character name (checked)
    CDS_NAME_NOT_IN_GAME,   // character name (checked, the character must be offline)
    CDS_JID,                // DWORD account ID
    CDS_FLAG_GUILD,         // BYTE 1: guild of a character name, else a guild name (checked)
    CDS_FLAG_ITEM,          // BYTE 1: character / guild name (checked), else QWORD serial
    CDS_NAME_DWORD,         // character name and an unused DWORD (checked)
};

struct CDS_QUERY
{
    BYTE        byType;
    BYTE        byRead;
    const char* pszQuery;       // name (or JID) variant
    const char* pszDesc;        // NULL: the description stays the default text
    const char* pszQuery2;      // flag != 1 variant
    const char* pszDesc2;
};

const CDS_QUERY s_aSelectorQuery[] =
{
    { 0, CDS_NAME_NOT_IN_GAME,
      "select LatestRegion, PosX, PosY, PosZ from _Char with(NOLOCK) where CharName16 = '%s' ",
      "Querying Character [%s] Position", NULL, NULL },
    { 1, CDS_JID,
      "exec _QueryCharName %d",
      "Querying Character Name with JID[%d]", NULL, NULL },
    { 3, CDS_NAME,
      "select CurLevel, MaxLevel, Strength, Intellect, RemainStatPoint, \t\t\t\t\t\t"
      "RemainSkillPoint, RemainGold, ExpOffset\tfrom _char with (nolock) where  "
      "CharName16 = '%s' ",
      "Querying Char Attr with CharName[%s]", NULL, NULL },
    { 4, CDS_FLAG_GUILD,
      "select [Name], lvl, gatheredsp from _guild with (nolock) where _guild.id = "
      "\t\t\t\t\t\t(select guildid from _char with (nolock) where charname16 = '%s' )",
      "Querying GuildInfo with Char Name[%s]",
      "select [Name], Lvl, GatheredSP, LastCrestRev, CurCrestRev from _Guild with (nolock) "
      "where [Name] = '%s' ",
      "Querying GuildInfo with GuildName[%s]" },
    { 6, CDS_NAME,
      "select DailyPK, TotalPK, PKPenaltyPoint from _Char with (nolock) where CharName16 = "
      "'%s' ",
      "Querying Char PK Data with CharName[%s]", NULL, NULL },
    { 7, CDS_NAME,
      "select JobType, [Level], Exp, Contribution, Reward from _CharTrijob with (nolock) "
      "where CharId = (select CharID from _Char with (nolock) where CharName16 = '%s') ",
      "Querying Char Trijob Data with CharName[%s]", NULL, NULL },
    { 9, CDS_FLAG_ITEM,
      "exec _GetInventoryItem '%s' ",
      "Querying Char Inventory with CharName[%s]",
      "exec _GetInventoryItemBySerial %I64d",
      "Querying Char Inventory with Serial[%I64d]" },
    { 10, CDS_FLAG_ITEM,
      "exec _GetChestItem '%s' ",
      "Querying Char Chest with CharName[%s]",
      "exec _GetChestItemBySerial %I64d",
      "Querying Char Chest with Serial[%I64d]" },
    { 11, CDS_FLAG_ITEM,
      "exec _GetGuildChestItem '%s' ",
      "Querying Guild Chest with GuildName[%s]",
      "exec _GetGuildChestItemBySerial %I64d",
      "Querying Guild Chest with Serial[%I64d]" },
    { 12, CDS_NAME,
      "select ID,Category,JobID,TimeToKeep,Data1,Data2,Data3,Data4,Data5,Data6,Data7,Data8 "
      "from _timedjob where CharId = (select CharID from _Char with (nolock) where "
      "CharName16 = '%s')",
      NULL, NULL, NULL },
    { 13, CDS_NAME,
      "select CharName, Nickname from _guildmember with (nolock) where GuildID = (select "
      "[ID] from _guild with (nolock) where [Name] = '%s')",
      "Querying Guild Member Nickname Data with GuildName[%s]", NULL, NULL },
    { 14, CDS_NAME,
      "select Nickname16 from _Char with (nolock) where CharName16 = '%s' ",
      "Querying Char Trijob Nickname with CharName[%s]", NULL, NULL },
    { 15, CDS_NAME,
      "declare @CampID int; exec @CampID = _GetCharTrainingCamp '%s'; select [ID], Rank, "
      "GraduateCount, EvaluationPoint from _TrainingCamp with (nolock) where @CampID > 0 "
      "and [ID] = @CampID",
      "Querying Training Camp Data with MemberName[%s]", NULL, NULL },
    { 17, CDS_NAME,
      "declare @CampID int;exec @CampID = _GetCharTrainingCamp '%s'; select CampID, "
      "CharName, MemberClass, HonorPoint from _TrainingCampMember with (nolock) where "
      "@CampID > 0 and CampID = @CampID",
      "Querying Training Camp Member Data with MemberName[%s]", NULL, NULL },
    { 19, CDS_FLAG_ITEM,
      "exec _GetAvatarInventoryItem '%s' ",
      "Querying Char AvatarInventory with CharName[%s]",
      "exec _GetAvatarInventoryItemBySerial %I64d",
      "Querying Char AvatarInventory with Serial[%I64d]" },
    { 20, CDS_NAME_DWORD,
      "select CharID, ThemeID, SlotIndex, RegDate from _CharCollectionBook where CharId = "
      "(select CharID from _Char with (nolock) where CharName16 = '%s')",
      "Querying Char CollectionBook with CharName[%s]", NULL, NULL },
};

// Row sizes of CCharDataSelector::Execute per query type (0: no row buffer).
const int s_anSelectorRowSize[21] =
{
    14, 68, 0, 28, 77, 0, 7, 14, 0, 323, 323, 323, 45, 81, 17, 13, 0, 73, 0, 323, 28,
};

} // namespace

// -------------------------------------------------------------------------------------------
// CBaseCharDataEditor

CBaseCharDataEditor::CBaseCharDataEditor()
{
    m_byResult = 0xFF;
    m_pReply = NULL;
}

CBaseCharDataEditor::~CBaseCharDataEditor()
{
}

bool CharDataEditor_CheckNotInGame(CBaseCharDataEditor* pEditor, const std::string& strName)
{
    if (!ClientMgr_FindUserByCharName(strName))
        return true;
    LogPrintf(0x1000000, "User In Game!! - CharEdit");
    pEditor->m_byResult = 14;
    return false;
}

// -------------------------------------------------------------------------------------------
// CCharDataSelector

CCharDataSelector::CCharDataSelector()
{
    Reset();
}

CCharDataSelector::~CCharDataSelector()
{
    Reset();
}

void CCharDataSelector::Reset()
{
    m_byQueryType = 0xFF;
    m_byResult = 0xFF;
    m_strQuery.clear();
    if (m_pReply)
    {
        m_pReply->Free();
        m_pReply = NULL;
    }
}

bool CCharDataSelector::ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator)
{
    char szDesc[0xFF] = "No Log. Some Error Occurred";
    m_dwRequestID = dwRequestID;
    m_strOperator.assign(*pstrOperator, 0, std::string::npos);

    try
    {
        if (m_pReply)
        {
            BSAssertFail();
            m_pReply->Free();
        }
        m_pReply = CMassiveMsg::Alloc(ServerFramework::MASSIVE_MODE_WRITE);
        *pMsg >> m_byQueryType;

        if (m_byQueryType == 2)
            return false;
        const CDS_QUERY* pQuery = NULL;
        for (int i = 0; i < _countof(s_aSelectorQuery); ++i)
        {
            if (s_aSelectorQuery[i].byType == m_byQueryType)
            {
                pQuery = &s_aSelectorQuery[i];
                break;
            }
        }
        if (!pQuery)
            throw 0;

        char szQuery[0xC00];
        std::string strName;
        BYTE byFlag = 0;
        DWORD dwValue = 0;
        unsigned __int64 qwSerial = 0;
        bool bName = true;
        const char* pszQuery = pQuery->pszQuery;
        const char* pszDesc = pQuery->pszDesc;
        switch (pQuery->byRead)
        {
        case CDS_JID:
            *pMsg >> dwValue;
            bName = false;
            break;
        case CDS_FLAG_GUILD:
            *pMsg >> byFlag;
            *pMsg >> strName;
            if (byFlag != 1)
            {
                pszQuery = pQuery->pszQuery2;
                pszDesc = pQuery->pszDesc2;
            }
            break;
        case CDS_FLAG_ITEM:
            *pMsg >> byFlag;
            if (byFlag == 1)
            {
                *pMsg >> strName;
            }
            else
            {
                // CMsg_ReadQword_Either (0x46f430)
                *pMsg >> qwSerial;
                pszQuery = pQuery->pszQuery2;
                pszDesc = pQuery->pszDesc2;
                bName = false;
            }
            break;
        case CDS_NAME_DWORD:
            *pMsg >> strName;
            *pMsg >> dwValue;
            break;
        default:
            *pMsg >> strName;
            break;
        }

        if (bName)
        {
            if (!MainProcess_IsValidCharName(strName))
                return false;
            if (pQuery->byRead == CDS_NAME_NOT_IN_GAME &&
                !CharDataEditor_CheckNotInGame(this, strName))
                return false;
            FormatOrThrow(szQuery, 0xC00, pszQuery, strName.c_str());
            if (pszDesc)
                FormatOrThrow(szDesc, 0xFF, pszDesc, strName.c_str());
        }
        else if (pQuery->byRead == CDS_JID)
        {
            FormatOrThrow(szQuery, 0xC00, pszQuery, dwValue);
            FormatOrThrow(szDesc, 0xFF, pszDesc, dwValue);
        }
        else
        {
            FormatOrThrow(szQuery, 0xC00, pszQuery, qwSerial);
            FormatOrThrow(szDesc, 0xFF, pszDesc, qwSerial);
        }

        m_strQuery.assign(szQuery, strlen(szQuery));
        m_strMessage.assign(szDesc, strlen(szDesc));
        return true;
    }
    catch (...)
    {
        // CCharDataSelector::vf01_CatchInvalidMsg (0x487d1b)
        LogPrintf(0x1000000, "CharDataEdit Request Message is invalid!!");
        m_byResult = 5;
        return false;
    }
}

bool CCharDataSelector::Execute()
{
    ServerFramework::CClassTrigger<CCharDataSelector> trigger(this);
    trigger.m_pfnTrigger2 = &CCharDataSelector::OnQueryRow;

    BYTE abRow[323];
    AQ_DBQuery query;
    query.Setup(&trigger, 4);
    if (m_byQueryType <= 20 && s_anSelectorRowSize[m_byQueryType])
        query.SetRow(abRow, s_anSelectorRowSize[m_byQueryType]);

    // BUG(SM-307): the statement text is passed as the format string.
    if (query.ExecuteFormat(m_strQuery.c_str()) < 0)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }
    if (query.m_bNoRow == 1 && m_byQueryType != 20)
    {
        AQ_WriteString(m_pReply, std::string("No Result"));
        m_byResult = AQ_RESULT_NOT_FOUND;
        return false;
    }
    BYTE byEnd = 0;
    m_pReply->Write(&byEnd, 1);
    return true;
}

bool CCharDataSelector::BuildReply()
{
    return true;
}

int CCharDataSelector::OnQueryRow(DWORD dwQuery, DWORD)
{
    AQ_DBQuery* pQuery = (AQ_DBQuery*)(DWORD_PTR)dwQuery;
    if (pQuery->m_bRowFetched)
    {
        BYTE byRow = 1;
        m_pReply->Write(&byRow, 1);
        m_pReply->Write(pQuery->m_pBuffer, (int)(DWORD_PTR)pQuery->m_pUserData);
        return 1;
    }
    if (pQuery->m_bNoRow)
        pQuery->m_bNoRow = 0;
    pQuery->ResetRowBuffer();
    return 1;
}

// -------------------------------------------------------------------------------------------
// CCharDataEditor

CCharDataEditor::CCharDataEditor()
{
    Reset();
}

CCharDataEditor::~CCharDataEditor()
{
    Reset();
}

void CCharDataEditor::Reset()
{
    m_byResult = 0xFF;
    m_byQueryType = 0xFF;
    m_byItemOp = 0xFF;
    m_strQuery.clear();
    if (m_pReply)
    {
        m_pReply->Free();
        m_pReply = NULL;
    }
    m_dwJID = 0;
    m_dw8C = 0;
    m_strOldName.clear();
    m_strNewName.clear();
    m_strLogQuery.clear();
    m_strEditDesc.clear();
    m_qwItemID = 0;
    m_qwSerial = 0;
    m_dwRefItemID = 0;
    m_dw84 = 0;
    m_bySlot = 0xFF;
}

namespace
{

// The three texts of an edit request: the statement, the console description and the
// arguments of the edit for the SMC log (m_strEditDesc, the description of BuildSMCLog).
// A format failure throws (invalid message).
struct CDE_TEXT
{
    char szQuery[0xC00];    // BUG(SM-340): not initialised
    char szDesc[0x100];     // "No Log. Some Error was Occurred", formatted with 0xFF characters
    char szLog[0x80];       // empty

    void Query(const char* pszFormat, ...)
    {
        va_list args;
        va_start(args, pszFormat);
        HRESULT hr = StringCchVPrintfA(szQuery, 0xC00, pszFormat, args);
        va_end(args);
        CheckFormatted(hr);
    }
    void Desc(const char* pszFormat, ...)
    {
        va_list args;
        va_start(args, pszFormat);
        HRESULT hr = StringCchVPrintfA(szDesc, 0xFF, pszFormat, args);
        va_end(args);
        CheckFormatted(hr);
    }
    void Log(const char* pszFormat, ...)
    {
        va_list args;
        va_start(args, pszFormat);
        HRESULT hr = StringCchVPrintfA(szLog, 0x80, pszFormat, args);
        va_end(args);
        CheckFormatted(hr);
    }
};

// The item row of an item edit (0x143 bytes, CMsg_ReadStruct323_Either 0x48c460), the layout of
// the _GetInventoryItem rows of the data selector.
#pragma pack(push, 1)
struct CDE_ITEM
{
    char                szOwner[0x80];      // +00  character or guild name
    BYTE                bySlot;             // +80
    unsigned __int64    qwItemID;           // +81
    DWORD               dwRefItemID;        // +89
    BYTE                byOptLevel;         // +8D
    unsigned __int64    qwVariance;         // +8E
    DWORD               dwData;             // +96
    char                szCreaterName[0x40];// +9A
    BYTE                byMagParamNum;      // +DA
    unsigned __int64    aqwMagParam[12];    // +DB
    unsigned __int64    qwSerial;           // +13B
};
#pragma pack(pop)
C_ASSERT(sizeof(CDE_ITEM) == 0x143);

// _SMC_EDIT_ITEM (bEdit: owner, slot, item ID, RefItemID, ...) or _SMC_ADD_ITEM (owner,
// RefItemID, ...) arguments of an item row.
void FormatItemQuery(CDE_TEXT& text, const char* pszFormat, const CDE_ITEM& item, bool bEdit)
{
    const unsigned __int64* p = item.aqwMagParam;
    if (bEdit)
    {
        text.Query(pszFormat, item.szOwner, item.bySlot, item.qwItemID, item.dwRefItemID,
                   item.byOptLevel, item.qwVariance, item.dwData, item.szCreaterName,
                   item.byMagParamNum, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
                   p[9], p[10], p[11]);
    }
    else
    {
        text.Query(pszFormat, item.szOwner, item.dwRefItemID, item.byOptLevel,
                   item.qwVariance, item.dwData, item.szCreaterName, item.byMagParamNum, p[0],
                   p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
    }
}

bool IsValidName(const std::string& strName)
{
    return MainProcess_IsValidCharName(strName) != FALSE;
}

// Reads the character name and checks it (name rule, the character must be offline).
bool ReadOfflineChar(CCharDataEditor* pEditor, CMsg* pMsg, std::string& strName)
{
    *pMsg >> strName;
    return IsValidName(strName) && CharDataEditor_CheckNotInGame(pEditor, strName);
}

// Every reader returns false without a result code when a check fails; the checks that answer
// one (result 14 in game, 15 game servers running) set it themselves.
typedef bool (*CDE_READER)(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text);

// 0: position {name, WORD region, float x, y, z, WORD angle}, read after the checks.
bool ReadCharPosition(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    *pMsg >> strName;
    if (!IsValidName(strName) || !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    WORD wRegion, wAngle;
    float fX, fY, fZ;
    *pMsg >> wRegion >> fX >> fY >> fZ >> wAngle;
    // The statement gets the region signed, the texts unsigned.
    text.Query("exec _ModifyCharPos '%s', %d, %f, %f, %f, %d", strName.c_str(),
               (int)(short)wRegion, (double)fX, (double)fY, (double)fZ, (int)wAngle);
    text.Desc("Modifying Char[%s] Position To [%d, %f, %f, %f, %d]", strName.c_str(),
              (int)wRegion, (double)fX, (double)fY, (double)fZ, (int)wAngle);
    text.Log("[%s], %d, %f, %f, %f, %d", strName.c_str(), (int)wRegion, (double)fX, (double)fY,
             (double)fZ, (int)wAngle);
    return true;
}

// 1: character rename {JID, DWORD, CharID, old name, new name}; BuildReply tells the
// GlobalManager.
bool ReadCharRename(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    DWORD dwCharID;
    *pMsg >> pEditor->m_dwJID >> pEditor->m_dw8C >> dwCharID;
    *pMsg >> pEditor->m_strOldName;
    *pMsg >> pEditor->m_strNewName;
    if (!IsValidName(pEditor->m_strOldName) || !IsValidName(pEditor->m_strNewName) ||
        !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    const char* pszOld = pEditor->m_strOldName.c_str();
    const char* pszNew = pEditor->m_strNewName.c_str();
    text.Query("exec _RenameCharNameByID %d, '%s' ", dwCharID, pszNew);
    text.Desc("Modifying Character Name [Frm:%s To: %s]", pszOld, pszNew);
    text.Log("[%s] -> [%s]", pszOld, pszNew);
    return true;
}

// 2: character deletion {name}.
bool ReadCharDelete(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    *pMsg >> strName;
    if (!IsValidName(strName) || !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("exec _ForceDeleteCharacter '%s' ", strName.c_str());
    text.Desc("Deleting Character [%s]", strName.c_str());
    text.Log("[%s]", strName.c_str());
    return true;
}

// 3: character stats {name, BYTE cur level, BYTE max level, WORD strength, WORD intellect,
// WORD stat points, DWORD skill points, QWORD gold, QWORD exp offset}.
bool ReadCharStat(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    BYTE byCurLevel, byMaxLevel;
    WORD wStrength, wIntellect, wStatPoint;
    DWORD dwSkillPoint;
    unsigned __int64 qwGold, qwExpOffset;
    *pMsg >> strName;
    *pMsg >> byCurLevel >> byMaxLevel >> wStrength >> wIntellect >> wStatPoint >> dwSkillPoint
          >> qwGold >> qwExpOffset;
    if (!IsValidName(strName) || !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    text.Query("exec _EditCharStat '%s', %d, %d, %d, %d, %d, %d, %I64d, %I64d", strName.c_str(),
               byCurLevel, byMaxLevel, wStrength, wIntellect, wStatPoint, dwSkillPoint, qwGold,
               qwExpOffset);
    text.Desc("Modifying Character[%s] Stat", strName.c_str());
    text.Log("[%s], %d, %d, %d, %d, %d, %d, %I64d, %I64d", strName.c_str(), byCurLevel,
             byMaxLevel, wStrength, wIntellect, wStatPoint, dwSkillPoint, qwGold, qwExpOffset);
    return true;
}

// 4: guild {name, new name, BYTE level, DWORD gathered SP, DWORD last / current crest
// revision}: the same name edits the stats, another one renames the guild.
bool ReadGuild(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strGuild, strNewName;
    BYTE byLevel;
    DWORD dwGatheredSP, dwLastCrestRev, dwCurCrestRev;
    *pMsg >> strGuild;
    *pMsg >> strNewName;
    *pMsg >> byLevel >> dwGatheredSP >> dwLastCrestRev >> dwCurCrestRev;
    if (!IsValidName(strGuild) || !IsValidName(strNewName) ||
        !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    if (strNewName.compare(strGuild) == 0)
    {
        text.Query("exec _EditGuildStat '%s', %d, %d, %d, %d", strGuild.c_str(), byLevel,
                   dwGatheredSP, dwLastCrestRev, dwCurCrestRev);
        text.Desc("Modifying Guild[%s] Stat", strGuild.c_str());
        text.Log("[%s], %d, %d, %d, %d", strGuild.c_str(), byLevel, dwGatheredSP,
                 dwLastCrestRev, dwCurCrestRev);
    }
    else
    {
        text.Query("exec _RenameGuildName '%s', '%s' ", strGuild.c_str(), strNewName.c_str());
        text.Desc("Modifying Guild Name [Frm:%s To: %s]", strGuild.c_str(), strNewName.c_str());
        text.Log("[%s] -> [%s]", strGuild.c_str(), strNewName.c_str());
    }
    return true;
}

// 6: PK data {name, BYTE daily PK, WORD total PK, DWORD penalty points}.
bool ReadCharPK(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    BYTE byDailyPK;
    WORD wTotalPK;
    DWORD dwPenaltyPoint;
    *pMsg >> strName;
    *pMsg >> byDailyPK >> wTotalPK >> dwPenaltyPoint;
    if (!IsValidName(strName) || !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    text.Query("exec _EditCharPK '%s', %d, %d, %d", strName.c_str(), byDailyPK, wTotalPK,
               dwPenaltyPoint);
    text.Desc("Modifying Character[%s] PK Data", strName.c_str());
    text.Log("[%s], %d, %d, %d", strName.c_str(), byDailyPK, wTotalPK, dwPenaltyPoint);
    return true;
}

// 7: trade job {name, BYTE job type, BYTE level, DWORD exp, DWORD contribution, DWORD reward}.
bool ReadTrijob(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    BYTE byJobType, byLevel;
    DWORD dwExp, dwContribution, dwReward;
    *pMsg >> strName;
    *pMsg >> byJobType >> byLevel >> dwExp >> dwContribution >> dwReward;
    if (!IsValidName(strName) || !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    text.Query("exec _SMC_EDIT_TRIJOB '%s', %d, %d, %d, %d, %d", strName.c_str(), byJobType,
               byLevel, dwExp, dwContribution, dwReward);
    text.Desc("Modifying Character[%s] Trijob Data", strName.c_str());
    text.Log("[%s], %d, %d, %d, %d, %d", strName.c_str(), byJobType, byLevel, dwExp,
             dwContribution, dwReward);
    return true;
}

// The item edits (9 inventory, 10 chest, 11 guild chest, 19 avatar inventory): {BYTE operation
// (m_byItemOp), owner name}, then per operation 1 {BYTE slot [, DWORD RefItemID, QWORD
// serial]}, 2 / 3 {item row}, 4 {QWORD gold}. Other operations fail without a result.
struct CDE_ITEM_EDIT
{
    BYTE        byType;
    bool        bGuild;         // the owner check is "game servers stopped", not "offline"
    bool        bDeleteReadsID; // the deletion also reads the RefItemID and the serial
    bool        bCheckRowOwner; // add / edit check the owner name of the item row
    const char* pszDelete;
    const char* pszDeleteDesc;
    const char* pszEdit;
    const char* pszEditDesc;
    const char* pszAdd;
    const char* pszAddDesc;
    const char* pszGold;        // NULL: no gold operation
    const char* pszGoldDesc;
};

#define CDE_EDIT_ITEM_ARGS "'%s', %d, %I64d, %d, %d, %I64d, %d, '%s', %d, %I64d, %I64d, %I64d, " \
    "%I64d, %I64d, %I64d, %I64d, %I64d, %I64d, %I64d, %I64d, %I64d"
#define CDE_ADD_ITEM_ARGS "'%s', %d, %d, %I64d, %d, '%s', %d, %I64d, %I64d, %I64d, %I64d, " \
    "%I64d, %I64d, %I64d, %I64d, %I64d, %I64d, %I64d, %I64d"

const CDE_ITEM_EDIT s_aItemEdit[] =
{
    { 9, false, true, false,
      "exec _SMC_DEL_ITEM 0, '%s', %d",
      "Deleting Character[%s] Inventory Item[Slot: %d]",
      "exec _SMC_EDIT_ITEM 0, " CDE_EDIT_ITEM_ARGS,
      "Modifying Character[%s] Inventory Item[Slot: %d]",
      "exec _SMC_ADD_ITEM 0, " CDE_ADD_ITEM_ARGS,
      "Adding Character[%s] Inventory Item[RefID: %d]",
      NULL, NULL },
    { 10, false, false, true,
      "exec _SMC_DEL_ITEM 1, '%s', %d",
      "Deleting Character[%s] Chest Item[Slot: %d]",
      "exec _SMC_EDIT_ITEM 1, " CDE_EDIT_ITEM_ARGS,
      "Modifying Character[%s] Chest Item[Slot: %d]",
      "exec _SMC_ADD_ITEM 1, " CDE_ADD_ITEM_ARGS,
      "Adding Character[%s] Chest Item[RefItemID: %d]",
      "exec _SMC_EDIT_USERGOLD '%s', %I64d",
      "Modifying Character[%s] Chest Gold[NewGold: %I64d]" },
    // The guild chest deletion uses the text of the character chest.
    { 11, true, false, true,
      "exec _SMC_DEL_ITEM 2, '%s', %d",
      "Deleting Character[%s] Chest Item[Slot: %d]",
      "exec _SMC_EDIT_ITEM 2, " CDE_EDIT_ITEM_ARGS,
      "Modifying Guild[%s] Chest Item[Slot: %d]",
      "exec _SMC_ADD_ITEM 2, " CDE_ADD_ITEM_ARGS,
      "Adding Guild[%s] Chest Item[RefItemID: %d]",
      "exec _SMC_EDIT_GUILDGOLD '%s', %I64d",
      "Modifying Guild[%s] Chest Gold[NewGold: %I64d]" },
    // The avatar inventory uses the texts of the inventory.
    { 19, false, true, false,
      "exec _SMC_DEL_ITEM 3, '%s', %d",
      "Deleting Character[%s] Inventory Item[Slot: %d]",
      "exec _SMC_EDIT_ITEM 3, " CDE_EDIT_ITEM_ARGS,
      "Modifying Character[%s] Inventory Item[Slot: %d]",
      "exec _SMC_ADD_ITEM 3, " CDE_ADD_ITEM_ARGS,
      "Adding Character[%s] Inventory Item[RefID: %d]",
      NULL, NULL },
};

#undef CDE_EDIT_ITEM_ARGS
#undef CDE_ADD_ITEM_ARGS

bool ReadItemEdit(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    const CDE_ITEM_EDIT* pEdit = NULL;
    for (int i = 0; i < _countof(s_aItemEdit); ++i)
    {
        if (s_aItemEdit[i].byType == pEditor->m_byQueryType)
            pEdit = &s_aItemEdit[i];
    }

    std::string strName;
    *pMsg >> pEditor->m_byItemOp;
    *pMsg >> strName;
    if (!IsValidName(strName))
        return false;
    if (pEdit->bGuild ? !CharDataEditor_CheckGameServersStopped(pEditor)
                      : !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;

    BYTE byOp = pEditor->m_byItemOp;
    if (byOp == 1)
    {
        BYTE bySlot;
        *pMsg >> bySlot;
        if (pEdit->bDeleteReadsID)
        {
            DWORD dwRefItemID;
            unsigned __int64 qwSerial;
            *pMsg >> dwRefItemID >> qwSerial;
            pEditor->m_qwSerial = qwSerial;
            pEditor->m_dwRefItemID = dwRefItemID;
        }
        text.Query(pEdit->pszDelete, strName.c_str(), bySlot);
        text.Desc(pEdit->pszDeleteDesc, strName.c_str(), bySlot);
        text.Log("[%s]", strName.c_str());
        pEditor->m_bySlot = bySlot;
        return true;
    }
    if (byOp == 3 || byOp == 2)
    {
        // BUG(SM-341): the owner and creator names of the row go into the statement
        // unchecked (the owner only for the chests is checked); the in-game check covers the
        // message name only.
        CDE_ITEM item;
        *pMsg >> item;
        if (pEdit->bCheckRowOwner && !IsValidName(std::string(item.szOwner)))
            return false;
        if (byOp == 3)
        {
            FormatItemQuery(text, pEdit->pszEdit, item, true);
            text.Desc(pEdit->pszEditDesc, item.szOwner, item.bySlot);
            text.Log("[%s]", item.szOwner);
            pEditor->m_dwRefItemID = item.dwRefItemID;
            pEditor->m_qwItemID = item.qwItemID;
            pEditor->m_qwSerial = item.qwSerial;
        }
        else
        {
            FormatItemQuery(text, pEdit->pszAdd, item, false);
            text.Desc(pEdit->pszAddDesc, item.szOwner, item.dwRefItemID);
            text.Log("[%s]", item.szOwner);
            pEditor->m_dwRefItemID = item.dwRefItemID;
        }
        return true;
    }
    if (byOp == 4 && pEdit->pszGold)
    {
        unsigned __int64 qwGold;
        *pMsg >> qwGold;
        text.Query(pEdit->pszGold, strName.c_str(), qwGold);
        text.Desc(pEdit->pszGoldDesc, strName.c_str(), qwGold);
        text.Log("[%s], %I64d", strName.c_str(), qwGold);
        return true;
    }
    return false;
}

// 12: timed job {BYTE operation (m_byItemOp), name}, operation 1 deletes {DWORD ID, BYTE
// category, DWORD job ID}. No in-game check.
bool ReadTimedJob(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName;
    *pMsg >> pEditor->m_byItemOp;
    *pMsg >> strName;
    if (!IsValidName(strName))
        return false;
    if (pEditor->m_byItemOp == 1)
    {
        DWORD dwID = 0, dwJobID = 0;
        BYTE byCategory = 0;
        *pMsg >> dwID >> byCategory >> dwJobID;
        text.Query("exec _SMC_DEL_TimedJob '%s', %d", strName.c_str(), dwID);
        text.Desc("Delete Char TimedJob CharName[%s] Category[%d] JobID[%d]", strName.c_str(),
                  byCategory, dwJobID);
        text.Log("[%s]", strName.c_str());
    }
    // BUG(SM-340): another operation succeeds with the statement buffer never written.
    return true;
}

// 13: guild member nickname {guild, character, nickname}.
bool ReadGuildNick(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strGuild, strChar, strNick;
    *pMsg >> strGuild;
    *pMsg >> strChar;
    *pMsg >> strNick;
    if (!IsValidName(strGuild) || !IsValidName(strChar) || !IsValidName(strNick) ||
        !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("EXEC _SMC_EDIT_GUILDNICK '%s', '%s', '%s'", strGuild.c_str(), strChar.c_str(),
               strNick.c_str());
    text.Desc("Update GuildMember Nickname(guild: %s, char: %s, nick: %s)", strGuild.c_str(),
              strChar.c_str(), strNick.c_str());
    text.Log("[%s]", strChar.c_str());
    return true;
}

// 14: trade job nickname {name, nickname}; the nickname is checked with the name rule.
bool ReadTrijobNick(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    std::string strName, strNick;
    *pMsg >> strName;
    *pMsg >> strNick;
    if (!IsValidName(strName) || !IsValidName(strNick) ||
        !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    text.Query("exec _SMC_EDIT_TRIJOBNICK '%s', '%s'", strName.c_str(), strNick.c_str());
    text.Desc("Modifying Character[%s] Trijob Nickname", strName.c_str());
    text.Log("[%s], %s", strName.c_str(), strNick.c_str());
    return true;
}

// 15: training camp {DWORD camp ID, BYTE rank, DWORD graduate count, DWORD evaluation point}.
bool ReadTrainingCamp(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    DWORD dwCampID, dwGraduateCount, dwEvaluationPoint;
    BYTE byRank;
    *pMsg >> dwCampID >> byRank >> dwGraduateCount >> dwEvaluationPoint;
    if (!CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("exec _SMC_EDIT_TRAINING_CAMP %d, %d, %d, %d", dwCampID, byRank, dwGraduateCount,
               dwEvaluationPoint);
    text.Desc("Update TrainingCamp Data(CampID: %d)", dwCampID);
    text.Log("[%d]", dwCampID);
    return true;
}

// 16: training camp deletion {DWORD camp ID}.
bool ReadTrainingCampDelete(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    DWORD dwCampID;
    *pMsg >> dwCampID;
    if (!CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("exec _SMC_DEL_TRAINING_CAMP %d", dwCampID);
    text.Desc("Delete TrainingCamp(CampID: %d)", dwCampID);
    text.Log("[%d]", dwCampID);
    return true;
}

// 17: training camp member {DWORD camp ID, name, DWORD honor point}.
bool ReadTrainingCampMember(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    DWORD dwCampID, dwHonorPoint;
    std::string strName;
    *pMsg >> dwCampID;
    *pMsg >> strName;
    *pMsg >> dwHonorPoint;
    if (!IsValidName(strName) || !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("exec _SMC_EDIT_TRAINING_CAMP_MEMBER %d, '%s', %u", dwCampID, strName.c_str(),
               dwHonorPoint);
    text.Desc("Update TrainingCamp Member Data(CampID: %d, Member: %s)", dwCampID,
              strName.c_str());
    text.Log("[%s]", strName.c_str());
    return true;
}

// 18: training camp member deletion {DWORD camp ID, name}.
bool ReadTrainingCampMemberDelete(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    DWORD dwCampID;
    std::string strName;
    *pMsg >> dwCampID;
    *pMsg >> strName;
    if (!IsValidName(strName) || !CharDataEditor_CheckGameServersStopped(pEditor))
        return false;
    text.Query("exec _SMC_DEL_TRAINING_CAMP_MEMBER %d, '%s'", dwCampID, strName.c_str());
    text.Desc("Delete TrainingCamp Member Data(CampID: %d, Member: %s)", dwCampID,
              strName.c_str());
    text.Log("[%s]", strName.c_str());
    return true;
}

// 20: collection book {BYTE action (1 register), name, DWORD theme ID, DWORD slot index}.
bool ReadCollectionBook(CCharDataEditor* pEditor, CMsg* pMsg, CDE_TEXT& text)
{
    BYTE byAction = 0;
    std::string strName;
    DWORD dwThemeID = 0, dwSlotIndex = 0;
    *pMsg >> byAction;
    *pMsg >> strName;
    *pMsg >> dwThemeID >> dwSlotIndex;
    if (!IsValidName(strName) || !CharDataEditor_CheckNotInGame(pEditor, strName))
        return false;
    text.Query("exec _SMC_UPDATE_COLLECTION_BOOK '%s', %u, %u, %u", strName.c_str(), byAction,
               dwThemeID, dwSlotIndex);
    text.Desc("Modifying Character[%s] CollectionBook [Action : %s] [ThemeID : %u] "
              "[SlotIndex : %u]", strName.c_str(), byAction == 1 ? "Register" : "Unregister",
              dwThemeID, dwSlotIndex);
    text.Log("[%s], %u, %u, %u", strName.c_str(), byAction, dwThemeID, dwSlotIndex);
    return true;
}

// The edit types of CCharDataEditor (jump table 0x48b8d8); NULL: invalid message.
const CDE_READER s_apfnEditReader[21] =
{
    ReadCharPosition,               // 0
    ReadCharRename,                 // 1
    ReadCharDelete,                 // 2
    ReadCharStat,                   // 3
    ReadGuild,                      // 4
    NULL,                           // 5
    ReadCharPK,                     // 6
    ReadTrijob,                     // 7
    NULL,                           // 8
    ReadItemEdit,                   // 9  inventory
    ReadItemEdit,                   // 10 chest
    ReadItemEdit,                   // 11 guild chest
    ReadTimedJob,                   // 12
    ReadGuildNick,                  // 13
    ReadTrijobNick,                 // 14
    ReadTrainingCamp,               // 15
    ReadTrainingCampDelete,         // 16
    ReadTrainingCampMember,         // 17
    ReadTrainingCampMemberDelete,   // 18
    ReadItemEdit,                   // 19 avatar inventory
    ReadCollectionBook,             // 20
};

} // namespace

bool CCharDataEditor::ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator)
{
    m_dwRequestID = dwRequestID;
    m_strOperator.assign(*pstrOperator, 0, std::string::npos);

    CDE_TEXT text;
    memset(text.szLog, 0, sizeof(text.szLog));
    memcpy(text.szDesc, "No Log. Some Error was Occurred", 0x20);
    memset(text.szDesc + 0x20, 0, 0xDF);

    try
    {
        if (m_pReply)
        {
            BSAssertFail();
            m_pReply->Free();
        }
        m_pReply = CMassiveMsg::Alloc(ServerFramework::MASSIVE_MODE_WRITE);
        *pMsg >> m_byQueryType;

        if (m_byQueryType >= _countof(s_apfnEditReader) || !s_apfnEditReader[m_byQueryType])
            throw 0;
        if (!s_apfnEditReader[m_byQueryType](this, pMsg, text))
            return false;

        m_strQuery.assign(text.szQuery, strlen(text.szQuery));
        m_strMessage.assign(text.szDesc, strlen(text.szDesc));
        m_strEditDesc.assign(text.szLog, strlen(text.szLog));
        return true;
    }
    catch (...)
    {
        // CCharDataEditor::vf01 catch block (0x48b889)
        LogPrintf(0x1000000, "CharDataEdit Request Message is invalid!!");
        m_byResult = 5;
        return false;
    }
}

bool CCharDataEditor::Execute()
{
    ServerFramework::CClassTrigger<CCharDataEditor> trigger(this);
    trigger.m_pfnTrigger2 = &CCharDataEditor::OnQueryRow;

    int nResult = 1;
    BYTE abItemRow[20];
    AQ_DBQuery query;
    query.Setup(&trigger, 5);
    if (m_byItemOp == 2)
        query.SetRow(abItemRow, 20);
    else
        query.SetRow(&nResult, 4);

    // BUG(SM-307): the statement text is passed as the format string.
    if (query.ExecuteFormat(m_strQuery.c_str()) < 0 || query.m_bNoRow == 1)
    {
        m_byResult = AQ_RESULT_NO_ROW;
        return false;
    }

    BYTE byEnd = 0;
    m_pReply->Write(&byEnd, 1);

    char szLog[0x80] = "No Log. Some Error was Occurred";
    BuildSMCLog(m_strEditDesc.c_str(), this, szLog);
    m_strLogQuery.assign(szLog, strlen(szLog));
    if (m_strLogQuery.size())
    {
        if (!g_pShardDB)
            BSAssertFail();
        CDatabase::ExecDirectWithReturnValue(ShardDB_GetLogSession(g_pShardDB), 2,
                                             m_strLogQuery.c_str(), 1, NULL);
    }
    return true;
}

int CCharDataEditor::OnQueryRow(DWORD dwQuery, DWORD)
{
    AQ_DBQuery* pQuery = (AQ_DBQuery*)(DWORD_PTR)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        if (pQuery->m_bNoRow)
            pQuery->m_bNoRow = 0;
        pQuery->ResetRowBuffer();
        return 1;
    }

    if (m_byItemOp == 2)
    {
        // {int error or slot, QWORD item ID, QWORD serial}
        char szText[0xFF];
        memset(szText, 0, sizeof(szText));
        const BYTE* pRow = pQuery->m_pBuffer;
        int nError = *(const int*)pRow;
        HRESULT hr;
        if (nError >= 0)
        {
            m_bySlot = (BYTE)nError;
            m_qwItemID = *(const unsigned __int64*)(pRow + 4);
            m_qwSerial = *(const unsigned __int64*)(pRow + 0xC);
            hr = StringCchPrintfA(szText, 0xFF, "(ID : %I64d, SN : %I64d, SlotTo : %d)",
                                  m_qwItemID, m_qwSerial, m_bySlot);
        }
        else
        {
            hr = StringCchPrintfA(szText, 0xFF, "(Add Item Failed (ErrCode:%d)!!)", nError);
        }
        if (hr != S_OK)
        {
            BSAssertFail();
            return 0;
        }
        m_strMessage.append(szText, strlen(szText));
    }

    BYTE byRow = 1;
    m_pReply->Write(&byRow, 1);
    m_pReply->Write(pQuery->m_pBuffer, (int)(DWORD_PTR)pQuery->m_pUserData);
    return 1;
}

bool CCharDataEditor::BuildReply()
{
    BYTE byType = m_byQueryType;
    if (byType == 1)
    {
        // The web DB keeps the character names: 0x6210 {shard, JID, new name, old name} to the
        // GlobalManager.
        CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
        pMsg->SetOpcode(0x6210);
        WORD wShardID = Cert_GetOwnShardID();
        pMsg->WriteBytes(&wShardID, 2);
        pMsg->WriteBytes(&m_dwJID, 4);
        *pMsg << m_strNewName;
        *pMsg << m_strOldName;
        if (!g_pShardMainProcess)
            BSAssertFail();
        if (!ServerFramework::RelayMsgToBody_6008(g_pShardMainProcess, pMsg,
                                                  Cert_GetGlobalManagerBodyID(), 0, 0, 0))
        {
            LogPrintf(0x2000001, "Failed To Change Character Name of WebDB[JID: %d, Old: %s, "
                      "New: %s]", m_dwJID, m_strOldName.c_str(), m_strNewName.c_str());
        }
    }
    else if (byType != 4 && byType != 2 && byType != 13)
    {
        if (byType == 15 || byType == 16 || byType == 17 || byType == 18)
        {
            if (TrainingCampManager_Reload() == 1)
                LogPrintf(0, "TrainingCamp Information Reloaded!!!");
        }
        return true;
    }

    if (GuildManager_Reload() == 1)
        LogPrintf(0, "Guild Information Reloaded!!!");
    return true;
}
