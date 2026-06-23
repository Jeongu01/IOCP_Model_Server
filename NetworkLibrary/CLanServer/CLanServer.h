#pragma once

#pragma comment(lib, "ws2_32")
#include <WinSock2.h>
#include <PacketBuffer.h>
#include <string>
#include <unordered_map>
#include "Session.h"

class CLanServer
{
public:
	CLanServer();
	virtual ~CLanServer();

	/**
	 * @brief 서버 시작
	 * @param (ip)오픈IP (port)포트 (worketCount)워커스레드 생성수 (activeCount)워커스레드 러닝수 (nagle)나글옵션 (maxSessionCount)최대접속자 수
	 * @return (bool)성공 여부
	 */
	bool Start(const CHAR* ip, SHORT port, DWORD workerCount, DWORD activeCount, BOOL nagle, DWORD maxSessionCount);

	/**
	 * @brief 서버 종료
	 * @param 없음
	 * @return 없음
	 */
	void Stop();

	/**
	 * @brief 현재 세션 수 얻기
	 * @param 없음
	 * @return (int)현재 세션 수
	 */
	int GetSessionCount();

	/**
	 * @brief 세션 연결 끊기
	 * @param (sessionID)대상 세션ID
	 * @return (bool)성공 여부
	 */
	bool Disconnect(DWORD64 sessionID);

	/**
	 * @brief 패킷 전송
	 * @param (sessionID)대상 세션ID (packet)전송할 패킷
	 * @return (bool)성공 여부
	 */
	bool SendPacket(DWORD64 sessionID, Packet* packet);

	virtual bool OnConnectionRequest(char* ip, SHORT port) = 0; // accept 직후 호출
	virtual void OnAccept(DWORD64 sessionID) = 0; // (Client 정보 / SessionID / 기타등등) = 0; // Accept 후 접속처리 완료 후 호출
	virtual void OnRelease(DWORD64 sessionID) = 0; // Release 후 호출
	virtual void OnRecv(DWORD64 sessionID, Packet* packet) = 0; // 패킷 수신 완료 후 호출
	virtual void OnError(int errorCode, std::wstring errorMsg) = 0;

	int getAcceptTPS();
	int getRecvMessageTPS();
	int getSendMessageTPS();

private:
	// 스레드 진입점
	static unsigned int WINAPI AcceptThread(LPVOID arg);
	static unsigned int WINAPI WorkerThread(LPVOID arg);

	void AcceptThreadMain();
	void WorkerThreadMain();

	Session* FindSession(DWORD64 sessionID);
	void ReleaseSession(DWORD64 sessionID);
	void RecvProc(Session* session, DWORD cbTransferred);
	void SendProc(Session* session, DWORD cbTransferred);
	
private:
	HANDLE m_hcp;
	SOCKET m_listenSock;
	bool m_isRunning;
	std::vector<HANDLE> m_threads;

	std::unordered_map<ULONGLONG, Session*> m_sessionMap;
	SRWLOCK m_sessionMapLock;

	DWORD64 m_sessionIDGenerator;

	// 설정 옵션 보관
	DWORD m_maxSessionCount;
	BOOL m_nagle;

};