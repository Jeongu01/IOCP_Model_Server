#pragma once
#include <WinSock2.h>

namespace jeongu
{
	template <typename T> class LockFreeMemoryPool;
	template <typename T> class LockFreeMemoryPoolTLS;
}

/*
* - LanServerHeader : 서버 간 통신. 길이만
* - NetServerHeader : 클라이언트 통신. 인코딩 정보 포함
*/
#pragma pack(push, 1)
struct LanServerHeader
{
	unsigned short _len;
};

struct NetServerHeader
{
	unsigned char _byteCode;
	unsigned short _len;
	unsigned char _randomKey;
	unsigned char _checkSum;
};
#pragma pack(pop)

/*
* Packet
*/

class Packet
{
	friend class CLanServer;
	friend class CNetServer;

	friend class jeongu::LockFreeMemoryPool<Packet>;
	friend class jeongu::LockFreeMemoryPoolTLS<Packet>;

public:
	enum en_PACKET
	{
		eBUFFER_DEFAULT = 1400,		// 패킷의 기본 버퍼 사이즈.
		eHEADER_RESERVE = sizeof(NetServerHeader)	// 버퍼 앞 헤더 예약 영역. 가장 큰 헤더 기준
	};

	/**
	* @brief 풀에서 패킷 할당. 참조 카운트 1 로 시작 (할당한 쪽이 참조 하나를 가짐)
	* @param 없음
	* @return (Packet*)비어 있는 패킷
	*/
	static Packet* Alloc(void);

	/**
	 * @brief 참조 반납. 0 이 되면 풀로 반환
	 * @param (packet)반납할 패킷
	 * @return 없음
	 */
	static void Free(Packet* packet);

	/**
	 * @brief 참조 추가. 패킷을 다른 곳(세션 sendQ, 다른 스레드 등)에 넘길 때 호출
	 * @param 없음
	 * @return 없음
	 */
	void AddRef(void);

	// 풀 통계 (GetUseCount 는 LFMP_STATS 가 꺼져 있으면 -1)
	static LONG64 GetUseCount(void);
	static DWORD GetCapacityCount(void);

	Packet(const Packet&) = delete;

	void Clear(void);

	//////////////////////////////////////////////////////////////////////////
	// 버퍼 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)패킷 버퍼 사이즈 얻기.
	//////////////////////////////////////////////////////////////////////////
	int	GetBufferSize(void) const { return m_iBufferSize - eHEADER_RESERVE; }
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

private:
	Packet();
	Packet(int iBufferSize);
	virtual	~Packet();

protected:
	/**
	 * @brief Lan 헤더 기록 (길이만). 페이로드 바로 앞에 붙임
	 */
	void SetLanHeader(void);

	/**
	 * @brief Net 헤더 기록 + 인코딩 (1회만)
	 * @note 틀만 잡아 둠. 헤더 필드 값 / 인코딩 방식은 추후 구현
	 *       플래그 확인이 원자적이지 않음 : 여러 스레드가 같은 패킷을 동시에 SendPacket 하면 이중 인코딩 (추후 결정)
	 */
	void SetNetHeader(void);
	void Encode(void);
	void Decode(void);

	char* GetSendPtr(void) { return m_chpBuffer + eHEADER_RESERVE - m_iHeaderSize; }
	int GetSendSize(void) const { return m_iHeaderSize + (m_iWritePos - eHEADER_RESERVE); }

	__forceinline bool _Push(const void* pSrc, int iSize)
	{
		if (iSize <= 0) return true;
		int freeSize = m_iBufferSize - m_iWritePos;
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
			m_iWritePos = eHEADER_RESERVE;
			m_iReadPos = eHEADER_RESERVE;
		}

		return true;
	}


protected:
	char* m_chpBuffer;
	int	m_iBufferSize;
	int	m_iDataSize;
	int m_iReadPos;
	int m_iWritePos;
	int m_iHeaderSize;
	bool m_bEncoded;
	volatile LONG m_refCount;
};
