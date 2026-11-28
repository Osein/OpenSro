#include "ShardManager/CrestUploader.h"

#include <stdio.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/thread/Task.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/SimpleFTP.h"
#include "ShardManager/GuildExtern.h"

using namespace ServerFramework;

CCrestUploader* g_pCrestUploader = NULL;

BS_IMPLEMENT_DYNCREATE(CCrestUploader, &CServerProcessBase::classCServerProcessBase)

CCrestUploader::CCrestUploader()
{
    if (g_pCrestUploader)
        BSAssertFail();
    g_pCrestUploader = this;
}

CCrestUploader::~CCrestUploader()
{
    g_pCrestUploader = NULL;
}

DWORD CCrestUploader::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;
    do
    {
        CTask* pTask = (CTask*)m_pOwner;
        CMsg* pMsg = NULL;
        LONG nCount = 0;
        if (pTask->m_pQueue || (BSAssertFail(), pTask->m_pQueue))
            nCount = pTask->m_pQueue->GetSize();
        if ((DWORD)nCount == 0)
        {
            Sleep(1);
            continue;
        }
        for (DWORD i = 0; i < (DWORD)nCount; ++i)
        {
            if (((CTask*)m_pOwner)->m_pQueue->Pop(&pMsg, 1, FALSE) != 1)
                goto closed;
            if (pMsg)
            {
                if (!nIndex)
                    BSAssertFail();
                pMsg->m_pOwner = (void*)(INT_PTR)nIndex;
                Dispatch(pMsg, 0, 0, NULL);
                CheckMsgFullyConsumed(pMsg);
                g_pNetEngine->ReleaseMsg(pMsg);
            }
        }
    } while (!*pbStop);
closed:
    LogPrintf(0x2000000, "CrestUploader Thread Closed!");
    return 0;
}

void CCrestUploader::InitMsgHandlers()
{
    CServerProcessBase::InitMsgHandlers();
    RegisterHandler(CREST_MSG_UPLOAD, &CCrestUploader::OnMsg_380E);
}

BOOL CMainProcess_IsCrestFtpEnabled()
{
    if (!g_pMainProcess)
        BSAssertFail();
    return g_pMainProcess->GetCrestFtpAddress().size() != 0;
}

// CCrestUploader_UploadFtp (0x40ada0): replaces the file on the CREST_FTP server.
static bool CrestUploader_UploadFtp(const char* pszFileName, const void* pData)
{
    CSimpleFTP ftp;
    if (!g_pMainProcess)
        BSAssertFail();
    const std::string& strAddress = g_pMainProcess->GetCrestFtpAddress();
    if (!strAddress.size())
        return false;
    if (!ftp.Connect(strAddress.c_str()))
    {
        LogPrintf(0x1000000, "SimpleFTP: Cannot create ftp session");
        return false;
    }
    LogPrintf(0x1000000, "Crest FTP : %s", strAddress.c_str());
    if (!ftp.ChangeToCrestDir())
    {
        LogPrintf(0x1000000, "SimpleFTP: Cannot change ftp folder for crest");
        return false;
    }
    ftp.DeleteRemoteFile(pszFileName);
    if (!ftp.UploadBuffer(pszFileName, pData))
    {
        LogPrintf(0x1000000, "SimpleFTP: Cannot upload crest data file");
        return false;
    }
    return true;
}

// CCrestUploader_SaveLocalCopy (0x40af20): .\Crest\<name> with the 256 crest bytes.
static bool CrestUploader_SaveLocalCopy(const char* pszFileName, const void* pData)
{
    if (!CreateDirectoryA(".\\Crest", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        LogPrintf(0x1000000, "Failed to create local crest folder!!");
        return false;
    }
    char szPath[MAX_PATH];
    sprintf_s(szPath, MAX_PATH, ".\\Crest\\%s", pszFileName);
    FILE* pFile;
    if (fopen_s(&pFile, szPath, "w+b"))
    {
        LogPrintf(0x1000000, "Failed to open new crest file!!");
        return false;
    }
    if (fwrite(pData, CREST_SIZE, 1, pFile) < 1)
    {
        LogPrintf(0x1000000, "Failed to write new crest file!!");
        fclose(pFile);
        return false;
    }
    fclose(pFile);
    return true;
}

void CCrestUploader::OnMsg_380E(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    if (!g_pMainProcess)
        BSAssertFail();
    CTask* pMainTask = (CTask*)g_pMainProcess->GetOwner();
    if (!pMainTask)
        BSAssertFail();
    WORD wType = pMsg->ReadForwardOnly<WORD>();
    DWORD dw1 = pMsg->ReadForwardOnly<DWORD>();
    DWORD dw2 = pMsg->ReadForwardOnly<DWORD>();
    DWORD dw3 = pMsg->ReadForwardOnly<DWORD>();
    DWORD dw4 = pMsg->ReadForwardOnly<DWORD>();
    CREST_DATA crest;
    *pMsg >> crest;
    // The shard ID of the body of this server (g_pSelfBody 0x8588B4).
    if (!g_pSelfBody)
        BSAssertFail();
    char szFileName[MAX_PATH];
    memset(szFileName, 0, sizeof(szFileName));
    BYTE bResult;
    if (StringCchPrintfA(szFileName, MAX_PATH, "%c%u_%u_%u.crb",
                         wType != CREST_TYPE_ALLIANCE ? 'G' : 'A',
                         g_pSelfBody->wShardID, dw3, dw4))
    {
        BSAssertFail();
        bResult = 2;
    }
    else if (!CrestUploader_SaveLocalCopy(szFileName, &crest))
    {
        LogPrintf(0x1000000, "failed to save local copy of crest!");
        bResult = 2;
    }
    else
    {
        if (!CrestUploader_UploadFtp(szFileName, &crest))
        {
            LogPrintf(0x1000000, "Failed To Upload Crest File [%s] But Revision Updated!",
                      szFileName);
        }
        bResult = 1;
    }
    CMsg* pAnswer = g_pNetEngine->AllocMsg(FALSE);
    pAnswer->SetOpcode(CREST_MSG_UPLOAD);
    pAnswer->WriteBytes(&wType, 2);
    pAnswer->WriteBytes(&dw1, 4);
    pAnswer->WriteBytes(&dw2, 4);
    pAnswer->WriteBytes(&dw3, 4);
    pAnswer->WriteBytes(&dw4, 4);
    pAnswer->WriteBytes(&bResult, 1);
    pMainTask->m_pQueue->Push(pAnswer);
}

BOOL CCrestUploader_PostUploadRequest(DWORD dw1, DWORD dw2, BYTE bGuild, DWORD dw3, DWORD dw4,
                                      const CREST_DATA* pCrest)
{
    WORD wType = (WORD)((bGuild != 1) + CREST_TYPE_GUILD);
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(CREST_MSG_UPLOAD);
    pMsg->WriteBytes(&wType, 2);
    pMsg->WriteBytes(&dw1, 4);
    pMsg->WriteBytes(&dw2, 4);
    pMsg->WriteBytes(&dw3, 4);
    pMsg->WriteBytes(&dw4, 4);
    pMsg->WriteBytes(pCrest, CREST_SIZE);
    if (!g_pCrestUploader)
        BSAssertFail();
    CTask* pTask = (CTask*)g_pCrestUploader->GetOwner();
    return pTask->m_pQueue->Push(pMsg);
}
