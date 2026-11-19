#pragma once

#include <winsock2.h>
#include <windows.h>

#include <string>

class CServerEvent;

// The log rows of the server events (_LogServerEvent) and the schedules (_LogSchedule). Each
// call fills an admin query job (AQ_ServerEventLog / AQ_ScheduleInsertLog) and posts it to the
// DB task of the main process.

// _LogServerEvent LogType
enum
{
    SERVER_EVENT_LOG_DETECTING  = 1,
    SERVER_EVENT_LOG_COMPLETION = 2,
    SERVER_EVENT_LOG_REWARD     = 3,
};

// ServerEventLog_LogDetecting (0x47b600): "Detecting[ref]_<KillMonster|EventCompletion> ..."
// when an event starts. FALSE when no job was posted.
BOOL ServerEventLog_LogDetecting(CServerEvent* pEvent);
// ServerEventLog_LogCompletion (0x47b7f0): the values of the event when it ends.
BOOL ServerEventLog_LogCompletion(CServerEvent* pEvent);
// ServerEventLog_LogReward (0x47b980, called by ProcessingEvent): a reward of the event, to the
// character of dwJID or, with dwJID 0, to everybody.
BOOL ServerEventLog_LogReward(DWORD dwJID, CServerEvent* pEvent);

// Schedule_InsertLogAsync (0x473880): a _LogSchedule row with the current time.
void Schedule_InsertLogAsync(std::string strModule, int nServerID, std::string strDefine,
                             DWORD dwScheduleID, std::string strState);
