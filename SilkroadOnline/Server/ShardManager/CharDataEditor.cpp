#include "ShardManager/CharDataEditor.h"

#include <stdarg.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"
#include "ServerFramework/ServerProcess.h"
#include "ShardManager/AQBase.h"
#include "ShardManager/ShardAdminFwd.h"

using ServerFramework::CMassiveMsg;

namespace
{

// The statement formatting of the editors: a failed StringCchPrintfA asserts and throws an int
// that the request reader catches as an invalid message.
void FormatOrThrow(char* psz, size_t cch, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    HRESULT hr = StringCchVPrintfA(psz, cch, pszFormat, args);
    va_end(args);
    if (hr != S_OK)
    {
        BSAssertFail();
        throw 0;
    }
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
    m_dwCharID = 0;
    m_dw84 = 0;
    m_bySlot = 0xFF;
}

bool CCharDataEditor::ReadRequest(CMsg* pMsg, DWORD dwRequestID, std::string* pstrOperator)
{
    // Pending: the request reader (0x4883e0, 0x34a9 bytes, about 20 edit types) has no
    // pseudocode and is not ported yet. It sets the request ID and the operator first.
    m_dwRequestID = dwRequestID;
    m_strOperator.assign(*pstrOperator, 0, std::string::npos);
    (void)pMsg;
    m_byResult = 5;
    return false;
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
