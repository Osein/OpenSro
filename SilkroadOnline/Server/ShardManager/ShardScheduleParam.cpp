#include "ShardManager/ShardScheduleJob.h"

#include <stdlib.h>

BOOL ScheduleParam_BuildOpenTimeList(const std::string& strParam, sTempleOpenSchedule* pOpen,
                                     DWORD dwBegin)
{
    ScheduleParamList params;
    if (ScheduleParamParser_Parse(strParam, &params) != 1)
        return FALSE;

    BOOL bFound = FALSE;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params.at(i).first == "WORLD_OPEN_TIME")
        {
            sTempleOpenTime entry;
            entry.dwTime = dwBegin + 60 * atol(params.at(i).second.c_str());
            entry.bOpen = 1;
            pOpen->m_list.push_back(entry);
            pOpen->m_dwOpenTime = entry.dwTime;
            bFound = TRUE;
        }
    }
    if (!bFound)
        return FALSE;

    for (size_t i = 0; i < params.size(); ++i)
    {
        if (params.at(i).first == "NOTIFY_BEFORE_OPEN")
        {
            sTempleOpenTime entry;
            entry.dwTime = pOpen->m_dwOpenTime - 60 * atol(params.at(i).second.c_str());
            entry.bOpen = 0;
            pOpen->m_list.push_back(entry);
        }
    }
    pOpen->m_list.sort();
    return TRUE;
}

BOOL ScheduleParam_GetInt(const std::string& strParam, std::string strKey, int* pnValue)
{
    ScheduleParamList params;
    if (ScheduleParamParser_Parse(strParam, &params) == 1)
    {
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[i].first == strKey)
            {
                *pnValue = atol(params.at(i).second.c_str());
                return TRUE;
            }
        }
    }
    return FALSE;
}
