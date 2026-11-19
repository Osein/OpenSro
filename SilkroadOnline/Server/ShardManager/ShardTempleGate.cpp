#include "ShardManager/ShardTempleGate.h"

#include <time.h>

#include "BSLib/BSDebug.h"
#include "BSNet/Msg.h"
#include "ServerCommon/ReferenceData.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ShardManager/GameWorld.h"
#include "ShardManager/ShardScheduleExtern.h"

using NewSchedule::sScheduleInfo;
using NewSchedule::sSchedulePeriod;

BOOL g_bAnubisAndIsisTempleOpen = FALSE;
CDateTime g_AnubisAndIsisTempleOpenTime;
BOOL g_bHaroerisAndSethTempleOpen = FALSE;
CDateTime g_HaroerisAndSethTempleOpenTime;

static const char s_szDefineAnubisAndIsis[] = "SCHEDULE_DEF_EGYPT_TEMPLE_OF_ANUBIS_AND_ISIS";
static const char s_szDefineHaroerisAndSeth[] = "SCHEDULE_DEF_EGYPT_TEMPLE_HAROERIS_AND_SETH";

const char* const CShardSJ_TempleOfSelkisAndNeithGate::s_pszNoticeLog =
    "Scheduleing : Selkis And Neith Of Temple is Open Before %d Min...";
const char* const CShardSJ_TempleOfSelkisAndNeithGate::s_pszOpenLog =
    "Scheduleing : Selkis And Neith Of Temple is now Open ...";
const char* const CShardSJ_TempleOfAnubisAndIsisGate::s_pszNoticeLog =
    "Scheduleing : Anubis And Isis Of Temple is Open Before %d Min...";
const char* const CShardSJ_TempleOfAnubisAndIsisGate::s_pszOpenLog =
    "Scheduleing : Anubis And Neith Of Temple is now Opened ...";
const char* const CShardSJ_TempleOfHaroerisGate::s_pszNoticeLog =
    "Scheduleing : Haroeris Of Temple is Open Before %d Min...";
const char* const CShardSJ_TempleOfHaroerisGate::s_pszOpenLog =
    "Scheduleing : Haroeris Of Temple is now Open ...";

// ---------------------------------------------------------------------------------------------
// Helpers

static RefGameWorld* FindTempleWorld(const char* pszCodeName)
{
    return CReferenceData::GetInstance()->FindGameWorldByCodeName(std::string(pszCodeName));
}

// The two reference rows of a temple pair; logs and asserts for a missing one.
static BOOL FindTempleWorldPair(const char* pszCode1, const char* pszCode2,
                                const char* pszMissing1, const char* pszMissing2,
                                RefGameWorld** ppRef1, RefGameWorld** ppRef2)
{
    *ppRef1 = FindTempleWorld(pszCode1);
    *ppRef2 = FindTempleWorld(pszCode2);
    if (!*ppRef1)
    {
        LogPrintf(0x2000000, pszMissing1);
        BSAssertFail();
        return FALSE;
    }
    if (!*ppRef2)
    {
        LogPrintf(0x2000000, pszMissing2);
        BSAssertFail();
        return FALSE;
    }
    return TRUE;
}

// Texts of one temple world.
struct TEMPLE_WORLD_TEXT
{
    const char* pszCodeName;
    const char* pszMissing;     // no reference row
    const char* pszNotFound;    // no game world
    const char* pszOpenFailed;  // OpenLayer failed (%d result)
};

static const TEMPLE_WORLD_TEXT s_Selkis =
{
    "INS_TEMPLE_SELK",
    "Selkis Of Temple data is not exists!! check the gust and service for the data",
    "Failed to find Selkis Of Temple GameWorld!!",
    "Open Selkis Of Temple GameWorld Failed!! error [ %d ] ",
};
static const TEMPLE_WORLD_TEXT s_Neith =
{
    "INS_TEMPLE_NEIT",
    "Neith Of Temple data is not exists!! check the gust and service for the data",
    "Failed to find Neith Of Temple GameWorld!!",
    "Open Neith Of Temple GameWorld Failed!! error [ %d ] ",
};
static const TEMPLE_WORLD_TEXT s_Anubis =
{
    "INS_TEMPLE_ANUB",
    "Anubis Of Temple data is not exists!! check the gust and service for the data",
    "Failed to find Anubis Of Temple GameWorld!!",
    "Open Anubis Of Temple GameWorld Failed!! error [ %d ] ",
};
static const TEMPLE_WORLD_TEXT s_Isis =
{
    "INS_TEMPLE_ISIS",
    "Isis Of Temple data is not exists!! check the gust and service for the data",
    "Failed to find Isis Of Temple GameWorld!!",
    "Open Isis Of Temple GameWorld Failed!! error [ %d ] ",
};
static const TEMPLE_WORLD_TEXT s_Haroeris =
{
    "INS_TEMPLE_HARO",
    "Haroeris Of Temple data is not exists!! check the gust and service for the data",
    "Failed to find Haroeris Of Temple GameWorld!!",
    "Open Haroeris Of Temple GameWorld Failed!! error [ %d ] ",
};

// Opens a layer of the temple world into *ppLayer (NULL when OpenLayer fails) and tells the
// game servers (0x3C15). FALSE when the world is missing or the layer did not open; a missing
// world asserts, a failed open asserts when bAssertOpenFail.
static BOOL OpenTempleWorld(const RefGameWorld* pRef, const TEMPLE_WORLD_TEXT& text,
                            BOOL bAssertOpenFail, GameWorldLayer** ppLayer)
{
    sGameWorldCreateParam param;
    WORD wWorldID = (WORD)pRef->ID;
    BYTE byResult = 1;
    param.m_dw0 = 0;
    param.m_b4 = 7;
    param.m_dw8 = 0;
    param.m_b12 = 1;
    param.m_b13 = 0;
    param.m_dwDuration = pRef->WorldDurationTime;
    param.m_dwEmptyRemainTime = pRef->WorldEmptyRemainTime;
    param.m_b24 = 0;
    CGameWorld* pWorld = CGameWorldMgr::GetInstance()->FindWorld(wWorldID);
    if (!pWorld)
    {
        LogPrintf(0x2000000, text.pszNotFound);
        BSAssertFail();
        return FALSE;
    }
    *ppLayer = CGameWorldMgr::GetInstance()->OpenLayer(&byResult, wWorldID, &param);
    if (!*ppLayer)
    {
        LogPrintf(0x2000000, text.pszOpenFailed, byResult);
        if (bAssertOpenFail)
            BSAssertFail();
        return FALSE;
    }
    WORD wOpened = (WORD)pWorld->m_pRef->ID;
    CGameWorldMgr::GetInstance();
    CGameWorldMgr::BroadcastOpened(wOpened, *ppLayer);
    return TRUE;
}

// OpenGameWorlds of the temple pairs: the first world, then the second when it opened.
static void OpenTempleWorldPair(RefGameWorld* pRef2, RefGameWorld* pRef1,
                                const TEMPLE_WORLD_TEXT& text1, const TEMPLE_WORLD_TEXT& text2,
                                GameWorldLayer** ppLayer1, GameWorldLayer** ppLayer2)
{
    if (!pRef1)
    {
        LogPrintf(0x2000000, text1.pszMissing);
        BSAssertFail();
        return;
    }
    if (!pRef2)
    {
        LogPrintf(0x2000000, text2.pszMissing);
        BSAssertFail();
        return;
    }
    if (!OpenTempleWorld(pRef1, text1, TRUE, ppLayer1))
        return;
    OpenTempleWorld(pRef2, text2, TRUE, ppLayer2);
}

// Month, day, hour and minute of t as bytes.
static void WriteMonthToMinute(CMsg* pMsg, CDateTime& t)
{
    BYTE byMinute = (BYTE)t.GetMinute();
    BYTE byHour = (BYTE)t.GetHour();
    BYTE byDay = (BYTE)t.GetDay();
    BYTE byMonth = (BYTE)t.GetMonth();
    pMsg->WriteBytes(&byMonth, 1);
    pMsg->WriteBytes(&byDay, 1);
    pMsg->WriteBytes(&byHour, 1);
    pMsg->WriteBytes(&byMinute, 1);
}

// WORLD_OPEN_TIME of a period's parameter, 30 without it.
static int GetWorldOpenMinutes(const sSchedulePeriod& period)
{
    int nMinutes = 0;
    if (!ScheduleParam_GetInt(period.strName, "WORLD_OPEN_TIME", &nMinutes))
        return 30;
    return nMinutes;
}

// The next period of the define after now for the open notice; NULL logs.
static BOOL GetNextTemplePeriod(const char* pszDefine, sSchedulePeriod* pPeriod)
{
    CDateTime now(_time32(NULL));
    std::list<sSchedulePeriod> periods;
    g_ShardScheduleManager.GetNextPeriods(periods, pszDefine, 1, now);
    if (periods.empty())
    {
        LogPrintf(0x2000000, "Next Schedule is not existed - %s", pszDefine);
        return FALSE;
    }
    *pPeriod = periods.front();
    return TRUE;
}

// The close notice of a temple layer: WORD world ID, WORD layer ID.
static void WriteClosedLayer(CMsg* pMsg, GameWorldLayer* pLayer)
{
    WORD wWorldID = (WORD)pLayer->m_dwWorldID;
    WORD wID = pLayer->m_wLayerID;
    pMsg->WriteBytes(&wWorldID, 2);
    pMsg->WriteBytes(&wID, 2);
}

// The open notice of a temple layer: DWORD reference ID, WORD layer ID.
static void WriteOpenedLayer(CMsg* pMsg, const RefGameWorld* pRef, GameWorldLayer* pLayer)
{
    WORD wID = pLayer->m_wLayerID;
    pMsg->WriteBytes(&pRef->ID, 4);
    pMsg->WriteBytes(&wID, 2);
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_TempleGateT

template <class TJob>
BOOL CShardSJ_TempleGateT<TJob>::OnTick(CDateTime& now)
{
    sScheduleInfo info = this->m_Info;
    info.bBegin = 2;
    std::list<sTempleOpenTime>& list = m_Open.m_list;
    if (list.empty())
        return TRUE;
    std::list<sTempleOpenTime>::iterator it = list.begin();
    if ((DWORD)now.GetTime() > it->dwTime)
    {
        if (!it->bOpen)
        {
            this->BeginNotify(&info);
            BYTE byMinutes = (BYTE)((m_Open.m_dwOpenTime - it->dwTime) / 60);
            WriteByte(this->m_pNotifyMsg, 1);
            WriteByte(this->m_pNotifyMsg, TJob::TICK_GATE);
            WriteByte(this->m_pNotifyMsg, byMinutes);
            this->BroadcastNotify();
            LogPrintf(ShardScheduleLogFlags(), TJob::s_pszNoticeLog,
                      (BYTE)((m_Open.m_dwOpenTime - it->dwTime) / 60));
        }
        if (it->bOpen == 1)
        {
            static_cast<TJob*>(this)->OpenTemples();
            LogPrintf(ShardScheduleLogFlags(), TJob::s_pszOpenLog);
        }
        list.erase(it);
    }
    return TRUE;
}

template <class TJob>
BOOL CShardSJ_TempleGateT<TJob>::BuildOpenSchedule()
{
    if (g_ShardScheduleExecutor.FindJobByName(this->m_Info.strDefine))
        return FALSE;
    if (!ScheduleParam_BuildOpenTimeList(this->m_Info.strName, &m_Open, this->m_Info.dwBegin))
    {
        // Notices 30, 10 and 5 minutes before the opening 30 minutes after the begin.
        DWORD dwBegin = this->m_Info.dwBegin;
        m_Open.m_list.clear();
        m_Open.m_dwOpenTime = 0;
        sTempleOpenTime entry;
        entry.bOpen = 0;
        entry.dwTime = dwBegin;
        m_Open.m_list.push_back(entry);
        entry.dwTime = dwBegin + 1200;
        m_Open.m_list.push_back(entry);
        entry.dwTime = dwBegin + 1500;
        m_Open.m_list.push_back(entry);
        entry.bOpen = 1;
        entry.dwTime = dwBegin + 1800;
        m_Open.m_list.push_back(entry);
        m_Open.m_dwOpenTime = dwBegin + 1800;
        m_Open.m_list.sort();
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_TempleOfSelkisAndNeithGate

CShardSJ_TempleOfSelkisAndNeithGate::CShardSJ_TempleOfSelkisAndNeithGate()
{
    m_pLayerSelkis = NULL;
    m_pLayerNeith = NULL;
    NewSchedule::IScheduleJob::SetDefineName("SCHEDULE_DEF_EGYPT_TEMPLE_OF_SELKIS_AND_NEITH");
    m_bWorkingStateWhenServiceStart = 1;
}

BOOL CShardSJ_TempleOfSelkisAndNeithGate::SetScheduleInfo(const sScheduleInfo& info)
{
    NewSchedule::IScheduleJob::SetScheduleInfo(info);
    m_pLayerSelkis = NULL;
    m_pLayerNeith = NULL;
    return TRUE;
}

BOOL CShardSJ_TempleOfSelkisAndNeithGate::OnBegin()
{
    if (!BuildOpenSchedule())
        return FALSE;
    BeginNotify(NULL);
    BroadcastNotify();
    LogPrintf(ShardScheduleLogFlags(),
              "Scheduleing : Selkis And Neith Of Temple Schedule is Start !");
    LogSchedule(NULL);
    return TRUE;
}

void CShardSJ_TempleOfSelkisAndNeithGate::OnEnd(CDateTime& /*now*/)
{
    RefGameWorld* pSelkis;
    RefGameWorld* pNeith;
    if (!FindTempleWorldPair(s_Selkis.pszCodeName, s_Neith.pszCodeName, s_Selkis.pszMissing,
                             s_Neith.pszMissing, &pSelkis, &pNeith))
        return;
    if (!m_pLayerSelkis || !m_pLayerNeith)
        return;
    BeginNotify(NULL);
    WriteByte(m_pNotifyMsg, 3);
    WriteClosedLayer(m_pNotifyMsg, m_pLayerSelkis);
    WriteClosedLayer(m_pNotifyMsg, m_pLayerNeith);
    WriteByte(m_pNotifyMsg, 2);
    WriteByte(m_pNotifyMsg, 3);
    BroadcastNotify();
    LogPrintf(ShardScheduleLogFlags(),
              "Scheduleing : Selkis And Neith Of Temple is now Closed ...");
    m_pLayerSelkis = NULL;
    m_pLayerNeith = NULL;
    LogSchedule(NULL);
}

void CShardSJ_TempleOfSelkisAndNeithGate::OpenTemples()
{
    RefGameWorld* pSelkis;
    RefGameWorld* pNeith;
    if (!FindTempleWorldPair(s_Selkis.pszCodeName, s_Neith.pszCodeName, s_Selkis.pszMissing,
                             s_Neith.pszMissing, &pSelkis, &pNeith))
        return;
    OpenGameWorlds(pNeith, pSelkis);
    if (!m_pLayerSelkis || !m_pLayerNeith)
        return;
    sScheduleInfo info = m_Info;
    info.bBegin = 2;
    BeginNotify(&info);
    WriteByte(m_pNotifyMsg, 2);
    WriteOpenedLayer(m_pNotifyMsg, pSelkis, m_pLayerSelkis);
    WriteOpenedLayer(m_pNotifyMsg, pNeith, m_pLayerNeith);
    WriteByte(m_pNotifyMsg, 3);
    BroadcastNotify();
}

void CShardSJ_TempleOfSelkisAndNeithGate::OpenGameWorlds(RefGameWorld* pNeith,
                                                         RefGameWorld* pSelkis)
{
    OpenTempleWorldPair(pNeith, pSelkis, s_Selkis, s_Neith, &m_pLayerSelkis, &m_pLayerNeith);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_TempleOfAnubisAndIsisGate

CShardSJ_TempleOfAnubisAndIsisGate::CShardSJ_TempleOfAnubisAndIsisGate()
{
    m_pLayerAnubis = NULL;
    m_pLayerIsis = NULL;
    NewSchedule::IScheduleJob::SetDefineName(s_szDefineAnubisAndIsis);
    m_bWorkingStateWhenServiceStart = 1;
}

BOOL CShardSJ_TempleOfAnubisAndIsisGate::OnBegin()
{
    if (!BuildOpenSchedule())
        return FALSE;
    g_bAnubisAndIsisTempleOpen = TRUE;
    g_AnubisAndIsisTempleOpenTime = CDateTime(m_Open.m_dwOpenTime);
    BeginNotify(NULL);
    BroadcastNotify();
    LogPrintf(ShardScheduleLogFlags(),
              "Scheduleing :  Anubis And Isis Of Temple Schedule is Start !");
    LogSchedule(NULL);
    return TRUE;
}

void CShardSJ_TempleOfAnubisAndIsisGate::OnEnd(CDateTime& /*now*/)
{
    RefGameWorld* pAnubis;
    RefGameWorld* pIsis;
    if (!FindTempleWorldPair(s_Anubis.pszCodeName, s_Isis.pszCodeName, s_Anubis.pszMissing,
                             s_Isis.pszMissing, &pAnubis, &pIsis))
        return;
    if (!m_pLayerAnubis || !m_pLayerIsis)
        return;
    BeginNotify(NULL);
    WriteByte(m_pNotifyMsg, 3);
    WriteClosedLayer(m_pNotifyMsg, m_pLayerAnubis);
    WriteClosedLayer(m_pNotifyMsg, m_pLayerIsis);
    WriteByte(m_pNotifyMsg, 0);
    WriteByte(m_pNotifyMsg, 0);
    BroadcastNotify();
    m_pLayerAnubis = NULL;
    m_pLayerIsis = NULL;
    g_bAnubisAndIsisTempleOpen = FALSE;
    LogPrintf(ShardScheduleLogFlags(),
              "Scheduleing : Anubis And Isis Of Temple is now Closed ...");
    LogSchedule(NULL);
}

void CShardSJ_TempleOfAnubisAndIsisGate::OpenTemples()
{
    RefGameWorld* pAnubis;
    RefGameWorld* pIsis;
    if (!FindTempleWorldPair(s_Anubis.pszCodeName, s_Isis.pszCodeName, s_Anubis.pszMissing,
                             s_Isis.pszMissing, &pAnubis, &pIsis))
        return;
    OpenGameWorlds(pIsis, pAnubis);
    if (!m_pLayerAnubis || !m_pLayerIsis)
        return;
    BS_ASSERT(g_pAssociationReputationMgr);
    if (!AssociationReputationMgr_DecideTempleWinner(g_pAssociationReputationMgr, 0))
        return;
    sSchedulePeriod next;
    if (!GetNextTemplePeriod(s_szDefineAnubisAndIsis, &next))
        return;
    int nMinutes = GetWorldOpenMinutes(next);
    CDateTime open(m_Open.m_dwOpenTime);
    CDateTime nextOpen(next.tBegin + 60 * nMinutes);
    sScheduleInfo info = m_Info;
    info.bBegin = 2;
    BeginNotify(&info);
    BS_ASSERT(g_pAssociationReputationMgr);
    BYTE byWinner = (BYTE)AssociationReputationMgr_GetWinner28(g_pAssociationReputationMgr);
    WriteByte(m_pNotifyMsg, 2);
    WriteOpenedLayer(m_pNotifyMsg, pAnubis, m_pLayerAnubis);
    WriteOpenedLayer(m_pNotifyMsg, pIsis, m_pLayerIsis);
    WriteByte(m_pNotifyMsg, byWinner);
    WriteMonthToMinute(m_pNotifyMsg, open);
    WriteMonthToMinute(m_pNotifyMsg, nextOpen);
    BroadcastNotify();
}

void CShardSJ_TempleOfAnubisAndIsisGate::OpenGameWorlds(RefGameWorld* pIsis,
                                                        RefGameWorld* pAnubis)
{
    OpenTempleWorldPair(pIsis, pAnubis, s_Anubis, s_Isis, &m_pLayerAnubis, &m_pLayerIsis);
}

// ---------------------------------------------------------------------------------------------
// CShardSJ_TempleOfHaroerisGate

CShardSJ_TempleOfHaroerisGate::CShardSJ_TempleOfHaroerisGate()
{
    m_pLayer = NULL;
    NewSchedule::IScheduleJob::SetDefineName(s_szDefineHaroerisAndSeth);
    m_bWorkingStateWhenServiceStart = 1;
}

BOOL CShardSJ_TempleOfHaroerisGate::OnBegin()
{
    if (!BuildOpenSchedule())
        return FALSE;
    g_bHaroerisAndSethTempleOpen = TRUE;
    g_HaroerisAndSethTempleOpenTime = CDateTime(m_Open.m_dwOpenTime);
    BeginNotify(NULL);
    BroadcastNotify();
    LogPrintf(ShardScheduleLogFlags(), "Scheduleing : Haroeris Of Temple Schedule is Start !");
    LogSchedule(NULL);
    return TRUE;
}

void CShardSJ_TempleOfHaroerisGate::OnEnd(CDateTime& /*now*/)
{
    if (!FindTempleWorld(s_Haroeris.pszCodeName))
    {
        LogPrintf(0x2000000, s_Haroeris.pszMissing);
        BSAssertFail();
        return;
    }
    if (!m_pLayer)
        return;
    BeginNotify(NULL);
    WriteByte(m_pNotifyMsg, 3);
    WriteClosedLayer(m_pNotifyMsg, m_pLayer);
    WriteByte(m_pNotifyMsg, 1);
    WriteByte(m_pNotifyMsg, 0);
    BroadcastNotify();
    m_pLayer = NULL;
    g_bHaroerisAndSethTempleOpen = FALSE;
    LogPrintf(ShardScheduleLogFlags(), "Scheduleing : Haroeris Of Temple is now Closed ...");
    LogSchedule(NULL);
}

void CShardSJ_TempleOfHaroerisGate::OpenTemples()
{
    RefGameWorld* pHaroeris = FindTempleWorld(s_Haroeris.pszCodeName);
    if (!pHaroeris)
    {
        LogPrintf(0x2000000, s_Haroeris.pszMissing);
        BSAssertFail();
        return;
    }
    OpenGameWorld(pHaroeris);
    if (!m_pLayer)
        return;
    BS_ASSERT(g_pAssociationReputationMgr);
    if (!AssociationReputationMgr_DecideTempleWinner(g_pAssociationReputationMgr, 1))
        return;
    sSchedulePeriod next;
    if (!GetNextTemplePeriod(s_szDefineHaroerisAndSeth, &next))
        return;
    int nMinutes = GetWorldOpenMinutes(next);
    CDateTime open(m_Open.m_dwOpenTime);
    CDateTime nextOpen(next.tBegin + 60 * nMinutes);
    sScheduleInfo info = m_Info;
    info.bBegin = 2;
    BeginNotify(&info);
    BS_ASSERT(g_pAssociationReputationMgr);
    BYTE byWinner = (BYTE)AssociationReputationMgr_GetWinner2C(g_pAssociationReputationMgr);
    WriteByte(m_pNotifyMsg, 2);
    WriteOpenedLayer(m_pNotifyMsg, pHaroeris, m_pLayer);
    WriteByte(m_pNotifyMsg, byWinner);
    WriteMonthToMinute(m_pNotifyMsg, open);
    WriteMonthToMinute(m_pNotifyMsg, nextOpen);
    BroadcastNotify();
}

void CShardSJ_TempleOfHaroerisGate::OpenGameWorld(RefGameWorld* pHaroeris)
{
    if (!pHaroeris)
    {
        LogPrintf(0x2000000, s_Haroeris.pszMissing);
        BSAssertFail();
        return;
    }
    // A failed open is only logged here.
    OpenTempleWorld(pHaroeris, s_Haroeris, FALSE, &m_pLayer);
}

// ---------------------------------------------------------------------------------------------
// Opening times for CMainProcess::OnMsg_7CDC

static BOOL QueryTempleOpenTime(const char* pszDefine, BOOL bOpen, CDateTime& openTime,
                                CDateTime* pPrev, CDateTime* pNext)
{
    CDateTime now(_time32(NULL));
    std::list<sSchedulePeriod> prevPeriods;
    g_ShardScheduleManager.GetPrevPeriods(prevPeriods, pszDefine, 1, now);
    if (prevPeriods.empty())
        return FALSE;
    CDateTime prevOpen(prevPeriods.front().tBegin + 60 * GetWorldOpenMinutes(prevPeriods.front()));
    if (bOpen == 1)
        *pPrev = openTime;
    std::list<sSchedulePeriod> nextPeriods;
    g_ShardScheduleManager.GetNextPeriods(nextPeriods, pszDefine, 1, now);
    if (nextPeriods.empty())
        return FALSE;
    CDateTime nextOpen(nextPeriods.front().tBegin + 60 * GetWorldOpenMinutes(nextPeriods.front()));
    *pPrev = prevOpen;
    *pNext = nextOpen;
    if (bOpen == 1)
    {
        if (now < openTime)
        {
            *pPrev = prevOpen;
            *pNext = openTime;
        }
        else
        {
            *pPrev = openTime;
            *pNext = nextOpen;
        }
    }
    return TRUE;
}

BOOL TempleOfAnubisAndIsis_QueryNextOpenTime(CDateTime* pPrev, CDateTime* pNext)
{
    return QueryTempleOpenTime(s_szDefineAnubisAndIsis, g_bAnubisAndIsisTempleOpen,
                               g_AnubisAndIsisTempleOpenTime, pPrev, pNext);
}

BOOL TempleOfHaroerisAndSeth_QueryNextOpenTime(CDateTime* pPrev, CDateTime* pNext)
{
    return QueryTempleOpenTime(s_szDefineHaroerisAndSeth, g_bHaroerisAndSethTempleOpen,
                               g_HaroerisAndSethTempleOpenTime, pPrev, pNext);
}
