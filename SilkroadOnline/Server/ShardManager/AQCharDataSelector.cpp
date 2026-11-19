#include "ShardManager/AQCharDataSelector.h"

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/AdminQueryJobs.h"
#include "ShardManager/ShardAdminFwd.h"

AQ_CharDataSelector::AQ_CharDataSelector()
{
}

void AQ_CharDataSelector::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_dwTargetID = 0;
    m_byQueryTarget = 0xFF;
    m_dw98 = 0;
}

bool AQ_CharDataSelector::ReadRequest(CMsg* pMsg, DWORD dwRelayID)
{
    if (!AQ_Base::ReadRequest(pMsg, dwRelayID))
        return false;

    *pMsg >> m_byQueryTarget;
    *pMsg >> m_strName;
    switch (m_byQueryTarget)
    {
    case 7:
    case 10:
    case 11:
        *pMsg >> m_dwTargetID;
        break;
    case 16:
    case 18:
    case 20:
        *pMsg >> m_qwItemSerial;
        break;
    case 21:
        *pMsg >> m_dw98;
        break;
    case 23:
        *pMsg >> m_byA1;
        *pMsg >> m_strC0;
        *pMsg >> m_strDC;
        *pMsg >> m_dw9C;
        *pMsg >> m_byA0;
        break;
    default:
        break;
    }

    std::string strFound;
    if (!g_pShardDB)
        BSAssertFail();
    if (ShardDB_CheckSqlInjection(g_pShardDB, m_strName, strFound) == 1)
    {
        m_byResult = AQ_RESULT_INJECTION;
        return false;
    }

    BuildDescription();
    if (m_strName.find(" ", 0, 1) != std::string::npos)
        return false;
    return true;
}

bool AQ_CharDataSelector::Execute()
{
    BeginResponse(AQ_OPCODE_CHAR_DATA_SELECT);
    AQ_WriteString(m_pResponse, m_strDesc);
    if (!g_pShardDB)
    {
        m_byResult = AQ_RESULT_FAILED;
        return false;
    }

    switch (m_byQueryTarget)
    {
    case 1:
        return QueryCharInfo();
    case 2:
    case 3:
    case 4:
    case 7:
    case 11:
    case 14:
    case 16:
        return QueryItems();
    case 5:
        return QueryQuests();
    case 6:
        return QuerySkills();
    case 8:
        return QueryGuild();
    case 9:
    case 10:
        return QueryPetDetail();
    case 12:
        return QueryTrainingCamp();
    case 18:
        return QueryItemDetail();
    case 20:
        return QueryItemExtra();
    case 21:
        return QueryCollectionBook();
    case 22:
        return QueryOpenMarketByChar();
    case 23:
        return QueryOpenMarketByDate();
    default:
        m_byResult = AQ_RESULT_UNKNOWN_TARGET;
        LogPrintf(0x1000000, "Unknown QueryTarget is Request!!! [%d]", m_byQueryTarget);
        return false;
    }
}

void AQ_CharDataSelector::BuildDescription()
{
    m_strDesc.assign("Querying Data of Character", 0x1A);
    switch (m_byQueryTarget)
    {
    case 1:     m_strDesc.append(" (Information)"); break;
    case 2:     m_strDesc.append(" (Equiped Items)"); break;
    case 3:     m_strDesc.append(" (Inventory Items)"); break;
    case 4:     m_strDesc.append(" (COS Inventory Items)"); break;
    case 5:     m_strDesc.append(" (Quest Information)"); break;
    case 6:     m_strDesc.append(" (Skill Information)"); break;
    case 7:     m_strDesc.append(" (Chest Items)"); break;
    case 8:     m_strDesc.append(" (Guild Information)"); break;
    case 11:    m_strDesc.append(" (Guild Chest Items)"); break;
    case 12:    m_strDesc.append(" (TrainingCamp Information)"); break;
    case 14:    m_strDesc.append(" (Avatar Inventory Items)"); break;
    case 21:    m_strDesc.append(" (Collection Book)"); break;
    default:    m_strDesc.append(" (INVALID!!!)", 13); break;
    }

    // Util_StringConcatPrefix (0x46f450) and Util_StringAppendCloseBracket (0x46f520)
    const char* pszPrefix = m_byQueryTarget == 11 ? " : [ GuildName " : " : [ CharName ";
    std::string strName = std::string(pszPrefix) + m_strName;
    std::string strPart = strName + " ]";
    m_strDesc.append(strPart, 0, std::string::npos);
}
