#pragma once

#include <list>

#include "ShardManager/ShardScheduleJob.h"

class GameWorldLayer;
struct RefGameWorld;

// The Egyptian temple gate jobs. OnBegin builds the open list from the schedule parameter
// (WORLD_OPEN_TIME / NOTIFY_BEFORE_OPEN, or 30 / 10 / 5 minute notices without one); OnTick
// works off the list one entry per tick (notices, then the opening of the temple worlds);
// OnEnd closes the temples.
//
// 0x34FE fields after the head (state 2 while running, 0 at the end):
//     notice: BYTE 1, BYTE gate (2 Selkis and Neith, 0 Anubis and Isis, 1 Haroeris), BYTE minutes
//     open:   BYTE 2, per world DWORD ref ID + WORD layer ID; Selkis and Neith then BYTE 3,
//             the others BYTE winner, then month / day / hour / minute of this opening and of
//             the next one
//     close:  BYTE 3, per world WORD world ID + WORD layer ID, then BYTE 2 3 (Selkis and
//             Neith), 0 0 (Anubis and Isis) or 1 0 (Haroeris)

// The common part of the three gates. TJob supplies TICK_GATE, s_pszNoticeLog, s_pszOpenLog
// and OpenTemples.
template <class TJob>
class CShardSJ_TempleGateT : public CShardScheduleJobT<TJob, IShard_ScheduleJob>
{
public:
    // The front entry once its time has passed: a notice with the minutes left or the
    // opening, then the entry goes (0x6600d0 / 0x660d50 / 0x661ff0).
    virtual BOOL OnTick(CDateTime& now);                                        // 6

protected:
    // Head of OnBegin: FALSE while another job of the define runs; the open list.
    BOOL BuildOpenSchedule();

public:
    sTempleOpenSchedule m_Open;     // +78
};

// SCHEDULE_DEF_EGYPT_TEMPLE_OF_SELKIS_AND_NEITH, flag 1 (vftable 0x796144).
class CShardSJ_TempleOfSelkisAndNeithGate
    : public CShardSJ_TempleGateT<CShardSJ_TempleOfSelkisAndNeithGate>
{
public:
    enum { TICK_GATE = 2 };
    static const char* const s_pszNoticeLog;
    static const char* const s_pszOpenLog;

    CShardSJ_TempleOfSelkisAndNeithGate();                                      // 0x664dc0

    // The info and no open layers.
    virtual BOOL SetScheduleInfo(const NewSchedule::sScheduleInfo& info);       // 1  0x65fe00
    virtual BOOL OnBegin();                                                     // 5  0x65fe30
    virtual void OnEnd(CDateTime& now);                                         // 7  0x6602c0

    // INS_TEMPLE_SELK / INS_TEMPLE_NEIT layers and the open notice.
    void OpenTemples();                                                         // 0x6605a0
    void OpenGameWorlds(RefGameWorld* pNeith, RefGameWorld* pSelkis);           // 0x660820

public:
    GameWorldLayer*    m_pLayerSelkis; // +88
    GameWorldLayer*    m_pLayerNeith;  // +8C
};

C_ASSERT(FIELD_OFFSET(CShardSJ_TempleOfSelkisAndNeithGate, m_pLayerSelkis) == 0x88);
C_ASSERT(sizeof(CShardSJ_TempleOfSelkisAndNeithGate) == 0x90);

// SCHEDULE_DEF_EGYPT_TEMPLE_OF_ANUBIS_AND_ISIS, flag 1 (vftable 0x796174).
class CShardSJ_TempleOfAnubisAndIsisGate
    : public CShardSJ_TempleGateT<CShardSJ_TempleOfAnubisAndIsisGate>
{
public:
    enum { TICK_GATE = 0 };
    static const char* const s_pszNoticeLog;
    static const char* const s_pszOpenLog;

    CShardSJ_TempleOfAnubisAndIsisGate();                                       // 0x664fa0

    virtual BOOL OnBegin();                                                     // 5  0x660a90
    virtual void OnEnd(CDateTime& now);                                         // 7  0x660f40

    // INS_TEMPLE_ANUB / INS_TEMPLE_ISIS layers, the winner and the next opening.
    void OpenTemples();                                                         // 0x661230
    void OpenGameWorlds(RefGameWorld* pIsis, RefGameWorld* pAnubis);            // 0x661750

public:
    GameWorldLayer*    m_pLayerAnubis; // +88
    GameWorldLayer*    m_pLayerIsis;   // +8C
};

C_ASSERT(sizeof(CShardSJ_TempleOfAnubisAndIsisGate) == 0x90);

// SCHEDULE_DEF_EGYPT_TEMPLE_HAROERIS_AND_SETH, flag 1 (vftable 0x7961a4).
class CShardSJ_TempleOfHaroerisGate : public CShardSJ_TempleGateT<CShardSJ_TempleOfHaroerisGate>
{
public:
    enum { TICK_GATE = 1 };
    static const char* const s_pszNoticeLog;
    static const char* const s_pszOpenLog;

    CShardSJ_TempleOfHaroerisGate();                                            // 0x6650c0

    virtual BOOL OnBegin();                                                     // 5  0x661d30
    virtual void OnEnd(CDateTime& now);                                         // 7  0x6621e0

    // INS_TEMPLE_HARO layer, the winner and the next opening.
    void OpenTemples();                                                         // 0x662430
    void OpenGameWorld(RefGameWorld* pHaroeris);                                // 0x662880

public:
    GameWorldLayer*    m_pLayer;       // +88
};

C_ASSERT(sizeof(CShardSJ_TempleOfHaroerisGate) == 0x8C);

// The temples that are open and their opening times (0x8588a0 / 0x8a7ad4, 0x8588a4 / 0x8a7afc).
extern BOOL g_bAnubisAndIsisTempleOpen;
extern CDateTime g_AnubisAndIsisTempleOpenTime;
extern BOOL g_bHaroerisAndSethTempleOpen;
extern CDateTime g_HaroerisAndSethTempleOpenTime;

// The opening before now and the next one (WORLD_OPEN_TIME minutes after the periods' begin,
// 30 without the parameter); while the temple is open its opening time takes the place of one
// of them. FALSE without both periods. Called by CMainProcess::OnMsg_7CDC.
BOOL TempleOfAnubisAndIsis_QueryNextOpenTime(CDateTime* pPrev, CDateTime* pNext);  // 0x6619d0
BOOL TempleOfHaroerisAndSeth_QueryNextOpenTime(CDateTime* pPrev, CDateTime* pNext); // 0x6629f0
