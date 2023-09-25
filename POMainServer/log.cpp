#include <windows.h>
#include< strsafe.h>
#include <iostream>
#include "log.h"

INT64 g_logCount = 0;
int g_logLevel = 0;
CRITICAL_SECTION g_log_CS;

void logInit()
{
    InitializeCriticalSection(&g_log_CS);
}

void systemLog(LPCWSTR String, int LogLevel, LPCWSTR StringFormat, ...)
{
    EnterCriticalSection(&g_log_CS);
    //���ɰ�
    if (g_logLevel > LogLevel)
    {
        LeaveCriticalSection(&g_log_CS);
        return;
    }

    FILE* fp;
    fopen_s(&fp, "LOG.txt", "at");
    if (fp == NULL)
    {
        LeaveCriticalSection(&g_log_CS);
        return;
    }
    fwprintf(fp, L"[%s] ", String);
    fprintf(fp, "[%s %s] ", __DATE__, __TIME__);
    fwprintf(fp, L"[LogCount : %lld] ", g_logCount++);

    WCHAR tmpStr[dfMAX_STRING];
    va_list argList;
    va_start(argList, StringFormat);
    StringCchVPrintf(tmpStr, dfMAX_STRING, StringFormat, argList);
    fwprintf(fp, L"%s", tmpStr);
    fwprintf(fp, L"\n");
    va_end(argList);
    fclose(fp);
    //������
    LeaveCriticalSection(&g_log_CS);
}