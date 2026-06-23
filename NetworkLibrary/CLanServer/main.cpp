#include "EchoServer.h"
#include "CrashDump.h"
#include <stdio.h>
#include <conio.h>

int main()
{
	jeongu::CrashDump crashHandler;

	EchoServer server;

	// 서버 설정 값
	const char* BIND_IP = "0.0.0.0";
	SHORT PORT = 6000;
	DWORD WORKER_THREADS = 20;
	DWORD ACTIVE_THREADS = 10;
	BOOL NAGLE_ON = TRUE;
	DWORD MAX_SESSIONS = 10000;

	printf("에코 서버를 시작합니다.\n");

	if (server.Start(BIND_IP, PORT, WORKER_THREADS, ACTIVE_THREADS, NAGLE_ON, MAX_SESSIONS))
	{
		printf("서버가 성공적으로 실행되었습니다.\n");
		printf("서버를 종료하려면 'q'를 누르세요\n");

		while (true)
		{
			if (_kbhit())
			{
				int ch = _getch();
				if (ch == 'q' || ch == 'Q')
				{
					break;
				}
			}
			Sleep(100);
		}

		printf("서버를 종료합니다.\n");
		server.Stop();
		printf("성공적으로 종료되었습니다.\n");
	}
	else
	{
		printf("서버 실행에 실패하였습니다.\n");
	}

	return 0;
}