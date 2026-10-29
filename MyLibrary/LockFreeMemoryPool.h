#pragma once
#include <Windows.h>
#include <malloc.h>
#include <cstddef>
#include <new>

#ifndef LFMP_VALIDATE
#define LFMP_VALIDATE 1		// Double Free / 다른 풀의 포인터 / 잘못된 포인터 탐지
#endif

#ifndef LFMP_GUARD
#define LFMP_GUARD 1		// 객체 앞뒤 침범 탐지
#endif

#ifndef LFMP_STATS
#define LFMP_STATS 0		// 통계 수집
#endif

namespace jeongu
{
	/*
	* LockFreeMemoryPool
	*/
	template <typename T>
	class LockFreeMemoryPool
	{
	public:
		static constexpr DWORD CHUNK_SIZE = 300;

		struct Block
		{
			Block* next;
			Block* volatile	chunkNext;
			DWORD			count;
#if LFMP_VALIDATE
			volatile LONG	useFlag;
			const void* owner;
#endif
#if LFMP_GUARD
			const void* frontGuard;
#endif
			alignas(T) char	object[sizeof(T)];
#if LFMP_GUARD
			const void* backGuard;
#endif
		};

		static void* ToStorage(Block* b) { return b->object; }
		static Block* ToBlock(T* p) { return (Block*)((char*)p - offsetof(Block, object)); }

		/**
		 * @brief 공용 풀 생성
		 * @param (placementNew)true : Alloc / Free 마다 생성자 / 소멸자 호출, false : 블록 생성 시 1회 생성 후 재사용 (initialBlocks)초기 블록 수 (maxBlocks)최대 블록 수, 0 이면 무제한
		 * @note 블록 수는 300 단위로 올림
		 */
		LockFreeMemoryPool(bool placementNew, DWORD initialBlocks, DWORD maxBlocks) :
			m_placementNew(placementNew),
			m_maxChunks((LONG)((maxBlocks + CHUNK_SIZE - 1) / CHUNK_SIZE)),
			m_chunkCount(0),
#if LFMP_STATS
			m_pushCount(0),
			m_popCount(0),
#endif
			m_top(0)
		{
			DWORD initChunks = (initialBlocks + CHUNK_SIZE - 1) / CHUNK_SIZE;
			for (DWORD i = 0; i < initChunks; i++)
			{
				PushChunk(NewChunk());
			}
		}

		LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;
		LockFreeMemoryPool& operator=(const LockFreeMemoryPool&) = delete;

		/**
		 * @brief 확보한 블록 수 얻기
		 * @param 없음
		 * @return (DWORD)확보한 블록 수
		 */
		DWORD GetCapacityCount() const { return (DWORD)m_chunkCount * CHUNK_SIZE; }

#if LFMP_STATS
		/**
		 * @brief 공용 풀에 청크를 넣은 횟수 / 꺼낸 횟수 얻기
		 * @param 없음
		 * @return (LONG64)횟수. 초기 블록 푸시 포함, 새로 만든 청크는 미포함 (GetCapacityCount 참고)
		 */
		LONG64 GetChunkPushCount() const { return m_pushCount; }
		LONG64 GetChunkPopCount() const { return m_popCount; }
#endif

		/**
		 * @brief Alloc 직후 블록 검증. 사용 중으로 표시
		 * @param (b)할당할 블록
		 * @return 없음
		 */
		__forceinline void ValidateAlloc(Block* b)
		{
#if LFMP_VALIDATE
			if (b->owner != this) __debugbreak();
			if (b->useFlag != 0) __debugbreak();
			b->useFlag = 1;
#endif
#if LFMP_GUARD
			if (b->frontGuard != this) __debugbreak();
			if (b->backGuard != this) __debugbreak();
#endif
			(void)b;
		}

		/**
		 * @brief Free 직전(소멸자 호출 전) 블록 검증. 풀에 있음으로 표시
		 * @param (b)반환할 블록
		 * @return 없음
		 */
		__forceinline void ValidateFree(Block* b)
		{
#if LFMP_VALIDATE
			if (b->owner != this) __debugbreak();
			if (InterlockedExchange(&b->useFlag, 0) != 1) __debugbreak();
#endif
#if LFMP_GUARD
			if (b->frontGuard != this) __debugbreak();
			if (b->backGuard != this) __debugbreak();
#endif
			(void)b;
		}

		/**
		 * @brief 청크 넣기
		 * @param (head)청크 head 블록. head->count 에 블록 수가 들어 있어야 함
		 * @return 없음
		 */
		void PushChunk(Block* head)
		{
			LONG64 oldTop;
			LONG64 newTop;

			do
			{
				oldTop = m_top;
				head->chunkNext = GetPointer(oldTop);
				newTop = MakeTagged(GetTag(oldTop) + 1, head);
			} while (InterlockedCompareExchange64(&m_top, newTop, oldTop) != oldTop);

#if LFMP_STATS
			InterlockedIncrement64(&m_pushCount);
#endif
		}

		/**
		 * @brief 청크 꺼내기. 비어 있으면 새 청크 생성
		 * @param 없음
		 * @return (Block*)청크 head 블록. nullptr 반환 없음
		 */
		Block* PopChunk()
		{
			while (true)
			{
				LONG64 oldTop = m_top;
				Block* head = GetPointer(oldTop);
				if (head == nullptr)
					return NewChunk();

				Block* next = head->chunkNext;
				LONG64 newTop = MakeTagged(GetTag(oldTop) + 1, next);
				if (InterlockedCompareExchange64(&m_top, newTop, oldTop) == oldTop)
				{
#if LFMP_STATS
					InterlockedIncrement64(&m_popCount);
#endif
					return head;
				}
			}
		}

	private:
		static Block* GetPointer(LONG64 tagged) { return (Block*)(tagged & 0x00007FFFFFFFFFFF); }
		static LONG64 GetTag(LONG64 tagged) { return (tagged >> 47) & 0x1FFFF; }
		static LONG64 MakeTagged(LONG64 tag, Block* ptr) { return ((tag & 0x1FFFF) << 47) | ((LONG64)ptr & 0x00007FFFFFFFFFFF); }

		Block* NewChunk()
		{
			LONG chunkCount = InterlockedIncrement(&m_chunkCount);
			if (m_maxChunks != 0 && chunkCount > m_maxChunks)
				__debugbreak();

			char* mem = (char*)_aligned_malloc(sizeof(Block) * CHUNK_SIZE, alignof(Block));
			if (mem == nullptr)
				__debugbreak();

			Block* head = nullptr;
			for (int i = CHUNK_SIZE - 1; i >= 0; i--)
			{
				Block* b = (Block*)(mem + i * sizeof(Block));
				b->next = head;
				b->chunkNext = nullptr;
#if LFMP_VALIDATE
				b->useFlag = 0;
				b->owner = this;
#endif
#if LFMP_GUARD
				b->frontGuard = this;
				b->backGuard = this;
#endif
				head = b;
			}
			head->count = CHUNK_SIZE;

			if (!m_placementNew)
			{
				for (Block* b = head; b != nullptr; b = b->next)
				{
					new (ToStorage(b)) T();
				}
			}

			return head;
		}

	private:
		const bool		m_placementNew;
		const LONG		m_maxChunks;
		volatile LONG	m_chunkCount;
#if LFMP_STATS
		volatile LONG64	m_pushCount;
		volatile LONG64	m_popCount;
#endif
		alignas(64) volatile LONG64 m_top;
	};
}