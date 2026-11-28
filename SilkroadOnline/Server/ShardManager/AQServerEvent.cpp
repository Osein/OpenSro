#include "ShardManager/AQServerEvent.h"

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

AQ_ServerEvent::AQ_ServerEvent()
{
    m_dwServerEventID = 0;
    m_dw54 = 0;
    m_nEventType = 0;
}

void AQ_ServerEvent::ReleaseId()
{
    AQ_Base::ReleaseId();
    m_strQuery.clear();
}

bool AQ_ServerEvent::Execute()
{
    switch (m_nEventType)
    {
    case 1:
        return ExecUpdate();
    case 2:
        return ExecInsertReward();
    case 3:
    case 4:
        return ExecDeleteOrUpdateReward();
    default:
        // BUG(SM-312): the second %d has no argument.
        LogPrintf(0x2000001, "Not supported DoWork AQ_ServerEventType[%d]!!!. ServerEventID[%d]",
                  m_dwServerEventID);
        BSAssertFail();
        return false;
    }
}

bool AQ_ServerEvent::OnComplete()
{
    return true;
}

bool AQ_ServerEvent::OnFail()
{
    return true;
}
