#pragma once
#include <Windows.h>

#define ID_MAX_SIZE			20
#define NICK_NAME_MAX_SIZE	20
#define SESSION_KEY_SIZE	64
#define INVALID_SECTOR		0xffff

struct Player
{
	DWORD64 sessionId;
	INT64 accountNo;
	WCHAR id[ID_MAX_SIZE];
	WCHAR nickname[NICK_NAME_MAX_SIZE];
	char sessionKey[SESSION_KEY_SIZE];

	WORD sectorX;
	WORD sectorY;
	DWORD sectorIndex;
	
	ULONGLONG lastRecvTime;
	BOOL isLogin;
	BOOL isDisconnecting;

	void Init(DWORD64 sessionId, ULONGLONG now)
	{
		this->sessionId = sessionId;
		accountNo = 0;
		id[0] = '\0';
		nickname[0] = '\0';
		sectorX = INVALID_SECTOR;
		sectorY = INVALID_SECTOR;
		sectorIndex = 0;
		lastRecvTime = now;
		isLogin = FALSE;
		isDisconnecting = FALSE;
	}
};

