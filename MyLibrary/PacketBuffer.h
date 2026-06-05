#pragma once
/////////////////////////////////////////////////////////////////////
// www.gamecodi.com						이주행 master@gamecodi.com
//
//
/////////////////////////////////////////////////////////////////////
/*---------------------------------------------------------------

	Packet.

	네트워크 패킷용 클래스.
	간편하게 패킷에 순서대로 데이타를 In, Out 한다.

	- 사용법.

	Packet cPacket;  or CMessage Message;

	넣기.
	clPacket << 40030;		or	clPacket << iValue;	(int 넣기)
	clPacket << 1.4;		or	clPacket << fValue;	(float 넣기)


	빼기.
	clPacket >> iValue;		(int 빼기)
	clPacket >> byValue;		(BYTE 빼기)
	clPacket >> fValue;		(float 빼기)

	Packet Packet2;

	!.	삽입되는 데이타 FIFO 순서로 관리된다.
		환형 큐는 아니므로, 넣기(<<).빼기(>>) 를 혼합해서 사용하지 않도록 한다



	* 실제 패킷 프로시저에서의 처리

	BOOL	netPacketProc_CreateMyCharacter(Packet *clpPacket)
	{
		DWORD dwSessionID;
		short shX, shY;
		char chHP;
		BYTE byDirection;

//		*clpPacket >> dwSessionID >> byDirection >> shX >> shY >> chHP;


		*clpPacket >> dwSessionID;
		*clpPacket >> byDirection;
		*clpPacket >> shX;
		*clpPacket >> shY;
		*clpPacket >> chHP;

		...
		...
	}


	* 실제 메시지(패킷) 생성부에서의 처리

	Packet MoveStart;
	mpMoveStart(&MoveStart, dir, x, y);
	SendPacket(&MoveStart);


	void	mpMoveStart(Packet *clpPacket, BYTE byDirection, short shX, short shY)
	{
		st_NETWORK_PACKET_HEADER	stPacketHeader;
		stPacketHeader.byCode = dfNETWORK_PACKET_CODE;
		stPacketHeader.bySize = 5;
		stPacketHeader.byType = dfPACKET_CS_MOVE_START;

		clpPacket->PutData((char *)&stPacketHeader, dfNETWORK_PACKET_HEADER_SIZE);

		*clpPacket << byDirection;
		*clpPacket << shX;
		*clpPacket << shY;

	}

----------------------------------------------------------------*/
#ifndef  __PACKET__
#define  __PACKET__
#include <WinSock2.h>

class Packet
{
public:

	/*---------------------------------------------------------------
	Packet Enum.

	----------------------------------------------------------------*/
	enum en_PACKET
	{
		eBUFFER_DEFAULT = 1400		// 패킷의 기본 버퍼 사이즈.
	};

	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Return:
	//////////////////////////////////////////////////////////////////////////
	Packet();
	Packet(int iBufferSize);

	virtual	~Packet();


	//////////////////////////////////////////////////////////////////////////
	// 패킷 청소.
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void Clear(void);


	//////////////////////////////////////////////////////////////////////////
	// 버퍼 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)패킷 버퍼 사이즈 얻기.
	//////////////////////////////////////////////////////////////////////////
	int	GetBufferSize(void) const { return m_iBufferSize; }
	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)사용중인 데이타 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int GetDataSize(void) const { return m_iDataSize; }
	//////////////////////////////////////////////////////////////////////////
	// 버퍼 읽기 포인터 얻기.
	//
	// Parameters: 없음.
	// Return: (char *)버퍼 포인터.
	//////////////////////////////////////////////////////////////////////////
	char* GetReadPtr(void) { return m_chpBuffer + m_iReadPos; }
	//////////////////////////////////////////////////////////////////////////
	// 버퍼 읽기 포인터 얻기.
	//
	// Parameters: 없음.
	// Return: (char *)버퍼 포인터.
	//////////////////////////////////////////////////////////////////////////
	char* GetWritePtr(void) { return m_chpBuffer + m_iWritePos; }


	//////////////////////////////////////////////////////////////////////////
	// 버퍼 Pos 이동. (음수이동은 안됨)
	// GetBufferPtr 함수를 이용하여 외부에서 강제로 버퍼 내용을 수정할 경우 사용. 
	//
	// Parameters: (int) 이동 사이즈.
	// Return: (int) 이동된 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int	MoveWritePos(int iSize);
	int	MoveReadPos(int iSize);


	/* ============================================================================= */
	// 연산자 오버로딩
	/* ============================================================================= */
	Packet& operator = (Packet& clSrcPacket);

	//////////////////////////////////////////////////////////////////////////
	// 넣기.	각 변수 타입마다 모두 만듬.
	//////////////////////////////////////////////////////////////////////////
	__forceinline Packet& operator << (unsigned char byValue)
	{
		_Push(&byValue, sizeof(byValue));
		return *this;
	}
	__forceinline Packet& operator << (char chValue) 
	{
		_Push(&chValue, sizeof(chValue));
		return *this;
	}
	__forceinline Packet& operator<<(short shValue)
	{
		_Push(&shValue, sizeof(shValue));
		return *this;
	}
	__forceinline Packet& operator<<(unsigned short wValue)
	{
		_Push(&wValue, sizeof(wValue));
		return *this;
	}
	__forceinline Packet& operator<<(int iValue)
	{
		_Push(&iValue, sizeof(iValue));
		return *this;
	}
	__forceinline Packet& operator<<(long lValue)
	{
		_Push(&lValue, sizeof(lValue));
		return *this;
	}
	__forceinline Packet& operator<<(unsigned long dwValue)
	{
		_Push(&dwValue, sizeof(dwValue));
		return *this;
	}
	__forceinline Packet& operator<<(float fValue)
	{
		_Push(&fValue, sizeof(fValue));
		return *this;
	}
	__forceinline Packet& operator<<(__int64 iValue)
	{
		_Push(&iValue, sizeof(iValue));
		return *this;
	}
	__forceinline Packet& operator<<(double dValue)
	{
		_Push(&dValue, sizeof(dValue));
		return *this;
	}

	//////////////////////////////////////////////////////////////////////////
	// 빼기.	각 변수 타입마다 모두 만듬.
	//////////////////////////////////////////////////////////////////////////

	__forceinline Packet& operator>>(BYTE& byValue)
	{
		_Pop(&byValue, sizeof(byValue));
		return *this;
	}
	__forceinline Packet& operator>>(char& chValue)
	{
		_Pop(&chValue, sizeof(chValue));
		return *this;
	}
	__forceinline Packet& operator>>(short& shValue)
	{
		_Pop(&shValue, sizeof(shValue));
		return *this;
	}
	__forceinline Packet& operator>>(WORD& wValue)
	{
		_Pop(&wValue, sizeof(wValue));
		return *this;
	}
	__forceinline Packet& operator>>(int& iValue)
	{
		_Pop(&iValue, sizeof(iValue));
		return *this;
	}
	__forceinline Packet& operator>>(long& lValue)
	{
		_Pop(&lValue, sizeof(lValue));
		return *this;
	}
	__forceinline Packet& operator>>(DWORD& dwValue)
	{
		_Pop(&dwValue, sizeof(dwValue));
		return *this;
	}
	__forceinline Packet& operator>>(float& fValue)
	{
		_Pop(&fValue, sizeof(fValue));
		return *this;
	}
	__forceinline Packet& operator>>(__int64& iValue)
	{
		_Pop(&iValue, sizeof(iValue));
		return *this;
	}
	__forceinline Packet& operator>>(double& dValue)
	{
		_Pop(&dValue, sizeof(dValue));
		return *this;
	}



	//////////////////////////////////////////////////////////////////////////
	// 데이타 얻기.
	//
	// Parameters: (char *)Dest 포인터. (int)Size.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int	GetData(char* chpDest, int iSize);

	//////////////////////////////////////////////////////////////////////////
	// 데이타 삽입.
	//
	// Parameters: (char *)Src 포인터. (int)SrcSize.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int	PutData(char* chpSrc, int iSrcSize);

protected:
	__forceinline bool _Push(const void* pSrc, int iSize)
	{
		if (iSize <= 0) return true;
		int freeSize = m_iBufferSize - m_iDataSize;
		if (freeSize < iSize) return false;

		memcpy(m_chpBuffer + m_iWritePos, pSrc, iSize);

		m_iDataSize += iSize;
		m_iWritePos += iSize;

		return true;
	}

	__forceinline bool _Pop(void* pDest, int iSize)
	{
		if (iSize <= 0) return true;
		if (m_iDataSize < iSize) return false;

		memcpy(pDest, m_chpBuffer + m_iReadPos, iSize);

		m_iDataSize -= iSize;
		m_iReadPos += iSize;

		if (m_iDataSize == 0)
		{
			m_iWritePos = 0;
			m_iReadPos = 0;
		}

		return true;
	}


protected:
	char* m_chpBuffer;
	int	m_iBufferSize;

	//------------------------------------------------------------
	// 현재 버퍼에 사용중인 사이즈.
	//------------------------------------------------------------
	int	m_iDataSize;
	int m_iReadPos;
	int m_iWritePos;
};



#endif
