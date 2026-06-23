#pragma once
#include "CLanServer.h"

class EchoServer : public CLanServer
{
public:
	EchoServer() = default;
	virtual ~EchoServer() = default;

private:
	virtual bool OnConnectionRequest(char* ip, SHORT port) override;
	virtual void OnAccept(DWORD64 sessionID) override;
	virtual void OnRelease(DWORD64 sessionID) override;
	virtual void OnRecv(DWORD64 sessionID, Packet* packet) override;
	virtual void OnError(int errorCode, std::wstring errorMsg) override;
};