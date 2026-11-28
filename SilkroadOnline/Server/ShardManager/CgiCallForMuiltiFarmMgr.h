#pragma once

#include <windows.h>
#include <string>

// CCgiCallForMuiltiFarmMgr (vftable 0x78ad98, 0x20 bytes, the static object at 0x89AFD4,
// g_pCgiCallForMuiltiFarmMgr 0x8A8938): HTTP calls to the billing web server of a multi-farm
// setup. The owner sets m_strBaseURL (the directory of the .asp pages, with a trailing '/').
class CCgiCallForMuiltiFarmMgr
{
public:
    // 0x48c580: constructs the static object (asserts when it exists) and returns it.
    static CCgiCallForMuiltiFarmMgr* CreateInstance();

    virtual ~CCgiCallForMuiltiFarmMgr();                                // 0 (0x48c630, 0x48c610)

    // 0x48c830: GET <base>billing_serverstate.asp; TRUE when the page answers "1".
    BOOL CheckBillingServerState();

    // 0x48c6e0: reads the page at pszURL into pBuffer (at most 255 bytes, 0-terminated);
    // pdwElapsed gets the time in ms. FALSE when the page cannot be opened, is empty or does
    // not fit.
    static BOOL HttpGet(const char* pszURL, char* pBuffer, DWORD* pdwElapsed);

private:
    CCgiCallForMuiltiFarmMgr() {}

public:
    std::string     m_strBaseURL;   // +04
};

C_ASSERT(sizeof(CCgiCallForMuiltiFarmMgr) == 0x20);

extern CCgiCallForMuiltiFarmMgr* g_pCgiCallForMuiltiFarmMgr;
