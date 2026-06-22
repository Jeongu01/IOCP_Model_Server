#pragma comment(lib, "DbgHelp.Lib")
#include "CrashDump.h"
#include <crtdbg.h>
#include <stdio.h>
#include <dbghelp.h>

namespace jeongu 
{
	long CrashDump::_DumpCount = 0;
	CrashDump::CrashDump()
	{
		_invalid_parameter_handler oldHandler, newHandler;
		newHandler = myInvalidParameterHandler;

		oldHandler = _set_invalid_parameter_handler(newHandler);	// crt 함수에 null 포인터 등을 넣었을 때
		_CrtSetReportMode(_CRT_WARN, 0);							// CRT 오류 메시지 표시 중단. 바로 덤프 남도록
		_CrtSetReportMode(_CRT_ASSERT, 0);							// CRT 오류 메시지 표시 중단. 바로 덤프 남도록
		_CrtSetReportMode(_CRT_ERROR, 0);							// CRT 오류 메시지 표시 중단. 바로 덤프 남도록

		_CrtSetReportHook(_custom_Report_hook);

		//----------------------------------------------------------------------------------------
		// pure virtual function called 에러 핸들러를 사용자 정의 함수로 우회시킨다.
		//----------------------------------------------------------------------------------------
		_set_purecall_handler(myPurecallHandler);

		SetHandlerDump();
	}

	void CrashDump::Crash(void)
	{
		__debugbreak();
	}

	LONG WINAPI CrashDump::MyExceptionFilter(PEXCEPTION_POINTERS pExceptionPointer)
	{
		int iWorkingMemory = 0;
		SYSTEMTIME stNowTime;

		long DumpCount = InterlockedIncrement(&_DumpCount);

		//----------------------------------------------------------------------------------------
		// 현재 날짜와 시간을 알아온다.
		//----------------------------------------------------------------------------------------
		WCHAR filename[MAX_PATH];

		GetLocalTime(&stNowTime);
		wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d.dmp",
			stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond, DumpCount);

		wprintf(L"\n\n\n!!! Crash Error !!!  %d.%d.%d / %d:%d:%d\n",
			stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute, stNowTime.wSecond);
		wprintf(L"Now Save dump file...\n");

		HANDLE hDumpFile = ::CreateFile(filename,
			GENERIC_WRITE,
			FILE_SHARE_WRITE,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (hDumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;

			MinidumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
			MinidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
			MinidumpExceptionInformation.ClientPointers = TRUE;

			MiniDumpWriteDump(
				GetCurrentProcess(),
				GetCurrentProcessId(),
				hDumpFile,
				MiniDumpWithFullMemory,
				&MinidumpExceptionInformation,
				NULL,
				NULL
			);

			CloseHandle(hDumpFile);

			wprintf(L"CrashDump Save Finish!");
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}

	void CrashDump::SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);
	}

	void CrashDump::myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
	{
		Crash();
	}

	int CrashDump::_custom_Report_hook(int ireposttype, char* message, int* returnvalue)
	{
		Crash();
		return true;
	}

	void CrashDump::myPurecallHandler(void)
	{
		Crash();
	}
}