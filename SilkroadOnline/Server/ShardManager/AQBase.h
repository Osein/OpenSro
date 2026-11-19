#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "BSNet/Msg.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/OverlapJob.h"

// Admin query jobs (AQ_*): requests of the service manager console (SMC) that the main process
// reads from a relayed message, runs on the overlap task and answers with a massive message
// (opcode 0x80xx) through the main process.
//
//     job = AQ_X::Alloc();
//     if (!job->ReadRequest(pMsg, dwRelayID)) job->OnInitFailed(opcode);   // answers the error
//     else main process: SendOverlapJob(job)                               // Execute, OnComplete
//
// Every AQ class derives from CAQJobT<AQ_X>, which supplies the parts that only differ by the
// class: the object pool, ReturnToPool (the per-class vf03) and Prepare.

enum
{
    AQ_RESULT_OK                = 1,
    AQ_RESULT_FAILED            = 2,    // default; no shard DB
    AQ_RESULT_NOT_FOUND         = 4,
    AQ_RESULT_INJECTION         = 5,
    AQ_RESULT_NO_ROW            = 7,
    AQ_RESULT_NO_SESSION        = 8,
    AQ_RESULT_NO_RELAY          = 9,
    AQ_RESULT_UNKNOWN_TARGET    = 11,
    AQ_RESULT_UNKNOWN_ITEM_TARGET = 12,
};

// AQ_Base (vftable 0x780754, 0x50 bytes).
class AQ_Base : public ServerFramework::COverlapJob_Local
{
public:
    AQ_Base();                                                          // 0x401000
    virtual ~AQ_Base();                                                 // 0 (0x401080, 0x401110)

    // 0x4865d0: releases the ID and resets the request (result 2, no session, no response).
    virtual void ReleaseId();                                           // 2
    // Slot 5 is pure in AQ_Base; see CAQJobT::Prepare.
    // 0x4866e0: sends the response to the console through the main process.
    virtual bool OnComplete();                                          // 7
    // 0x486730: appends {BYTE 2, BYTE result} and sends the response.
    virtual bool OnFail();                                              // 8
    // 0x486600: owner task, session, relay ID, request ID and operator text of the request.
    virtual bool ReadRequest(CMsg* pMsg, DWORD dwRelayID);              // 9
    // 0x4867a0: answers "Failed to Init" with opcode wOpcode.
    virtual bool OnInitFailed(WORD wOpcode);                            // 10

    // 0x4866b0: a new response with opcode wOpcode (asserts that there is none yet).
    void BeginResponse(WORD wOpcode);

public:
    BYTE                            m_byResult;     // +20  AQ_RESULT_*
    DWORD                           m_dwSessionID;  // +24
    DWORD                           m_dwRelayID;    // +28
    DWORD                           m_dwRequestID;  // +2C
    std::string                     m_strOperator;  // +30
    ServerFramework::CMassiveMsg*   m_pResponse;    // +4C
};

C_ASSERT(FIELD_OFFSET(AQ_Base, m_byResult) == 0x20);
C_ASSERT(FIELD_OFFSET(AQ_Base, m_dwRequestID) == 0x2C);
C_ASSERT(FIELD_OFFSET(AQ_Base, m_strOperator) == 0x30);
C_ASSERT(FIELD_OFFSET(AQ_Base, m_pResponse) == 0x4C);
C_ASSERT(sizeof(AQ_Base) == 0x50);

// Writes a string as {WORD length, chars} (the inlined CMassiveMsg string writer, also
// CASChat_Helper_40B650 for a std::string).
inline void AQ_WriteString(ServerFramework::CMassiveMsg* pMsg, const std::string& str)
{
    pMsg->WriteString(str.c_str());
}

// The per-class part of an admin query job. TBase is AQ_Base, or COverlapJob_Local for the
// jobs without a console response (AQ_CharDataEditor2).
template <class T, class TBase = AQ_Base>
class CAQJobT : public TBase
{
public:
    // vf03 of every AQ class: jobs with an ID release it (virtual ReleaseId) and go back to
    // the class pool.
    virtual void ReturnToPool()                                         // 3
    {
        if (this->m_dwID)
        {
            this->ReleaseId();
            s_Pool.Free(static_cast<T*>(this));
        }
    }

    // 0x69bb10 (shared by the AQ classes): nothing to prepare.
    virtual bool Prepare()                                              // 5
    {
        return true;
    }

    // Pool object with an overlap job ID.
    static T* Alloc()
    {
        T* p = s_Pool.Alloc();
        p->AcquireId();
        return p;
    }

    // CChunkAllocatorST_AQ_<class> (500 jobs per block).
    static CChunkAllocatorST<T> s_Pool;
};

template <class T, class TBase>
CChunkAllocatorST<T> CAQJobT<T, TBase>::s_Pool;
