#include "ShardManager/CgiCallForMuiltiFarmMgr.h"

#include <new>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>
#include <wininet.h>

#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerDataExtern.h"

CCgiCallForMuiltiFarmMgr* g_pCgiCallForMuiltiFarmMgr = NULL;

namespace
{
// The static object (0x89AFD4), constructed by CreateInstance.
union
{
    double  dAlign;
    BYTE    ab[sizeof(CCgiCallForMuiltiFarmMgr)];
} s_Instance;
}

CCgiCallForMuiltiFarmMgr* CCgiCallForMuiltiFarmMgr::CreateInstance()
{
    if (g_pCgiCallForMuiltiFarmMgr)
        BSAssertFail();
    g_pCgiCallForMuiltiFarmMgr = new (s_Instance.ab) CCgiCallForMuiltiFarmMgr;
    return g_pCgiCallForMuiltiFarmMgr;
}

CCgiCallForMuiltiFarmMgr::~CCgiCallForMuiltiFarmMgr()
{
    m_strBaseURL.clear();
    g_pCgiCallForMuiltiFarmMgr = NULL;
}

BOOL CCgiCallForMuiltiFarmMgr::HttpGet(const char* pszURL, char* pBuffer, DWORD* pdwElapsed)
{
    if (lstrlenA(pszURL) < 1 || !pBuffer)
        return FALSE;
    if (pdwElapsed)
        *pdwElapsed = 0;

    DWORD dwStart = GetTickCount();
    HINTERNET hInternet = InternetOpenA("SR_GameServer", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL,
                                        0);
    if (!hInternet)
        return FALSE;
    DWORD dwFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_EXISTING_CONNECT;   // 0xA0000000
    if (strstr(pszURL, "https"))
        dwFlags |= INTERNET_FLAG_SECURE;
    HINTERNET hURL = InternetOpenUrlA(hInternet, pszURL, NULL, 0, dwFlags, 0);
    if (!hURL)
    {
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    DWORD dwAvailable = 0;
    DWORD dwRead = 0;
    DWORD dwTotal = 0;
    do
    {
        InternetQueryDataAvailable(hURL, &dwAvailable, 0, 0);
        if (dwTotal + dwAvailable >= 0x100)
        {
            InternetCloseHandle(hURL);
            InternetCloseHandle(hInternet);
            return FALSE;
        }
        InternetReadFile(hURL, pBuffer + dwTotal, dwAvailable, &dwRead);
        dwTotal += dwRead;
    }
    while (dwRead);
    InternetCloseHandle(hURL);
    InternetCloseHandle(hInternet);
    pBuffer[dwTotal] = 0;
    if (!dwTotal)
        return FALSE;
    if (pdwElapsed)
        *pdwElapsed = GetTickCount() - dwStart;
    return TRUE;
}

BOOL CCgiCallForMuiltiFarmMgr::CheckBillingServerState()
{
    if (!m_strBaseURL.size())
    {
        LogPrintf(0x2000000, "Failed to getting silk data: Empty URL!!!");
        return FALSE;
    }
    char szURL[0xFF];
    StringCchPrintfA(szURL, 0xFF, "%sbilling_serverstate.asp", m_strBaseURL.c_str());
    DWORD dwElapsed = 0;
    char szAnswer[0x100];
    if (!HttpGet(szURL, szAnswer, &dwElapsed))
    {
        LogPrintf(0x2000000, "Failed to initialize billing server: can't open URL!");
        return FALSE;
    }
    if (!lstrlenA(szAnswer))
    {
        LogPrintf(0x2000000, "Failed to initialize billing server: return value is empty!");
        return FALSE;
    }
    long nState = atol(szAnswer);
    if (nState != 1)
    {
        LogPrintf(0x2000000, "Failed to initialize billing server: invalid return value (%d)",
                  nState);
        return FALSE;
    }
    return TRUE;
}
