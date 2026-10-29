#pragma once
#include "LockFreeMemoryPool.h"

namespace jeongu
{
	/*
	* LockFreeMemoryPoolTLS
	*/
	template <typename T>
	class LockFreeMemoryPoolTLS
	{
		using Pool = LockFreeMemoryPool<T>;
		using Block = typename Pool::Block;

		static constexpr DWORD CHUNK_SIZE = Pool::CHUNK_SIZE;
		static constexpr DWORD TLS_MAX_CHUNKS = 10;

		struct Cache
		{
			Block* cur = nullptr;
			DWORD	curCount = 0;
			Block* chunkTop = nullptr;
			DWORD	chunkCount = 0;

			~Cache()
			{
				LockFreeMemoryPoolTLS* pool = s_self;
				if (pool == nullptr)
					return;

				if (cur != nullptr)
				{
					cur->count = curCount;
					pool->m_pool.PushChunk(cur);
				}

				while (chunkTop != nullptr)
				{
					Block* next = chunkTop->chunkNext;
					pool->m_pool.PushChunk(chunkTop);
					chunkTop = next;
				}
			}
		};

		static thread_local Cache t_cache;
		static LockFreeMemoryPoolTLS* volatile s_self;

	public:
		/**
		 * @brief 풀 생성 (타입당 하나, 전역 / static 으로 생성)
		 * @param (placementNew)true : Alloc / Free 마다 생성자 / 소멸자 호출, false : 최초 1회 생성 후 재사용 (initialBlocks)초기 블록 수 (maxBlocks)최대 블록 수, 0 이면 무제한
		 * @note 블록 수는 300 단위로 올림. 최대 블록 수를 넘으면 크래시
		 */
		explicit LockFreeMemoryPoolTLS(bool placementNew = false, DWORD initialBlocks = 0, DWORD maxBlocks = 0) :
			m_placementNew(placementNew),
			m_pool(placementNew, initialBlocks, maxBlocks)
#if LFMP_STATS
			, m_useCount(0)
#endif
		{
			if (InterlockedCompareExchangePointer((PVOID volatile*)&s_self, this, nullptr) != nullptr)
				__debugbreak();		// 같은 타입의 풀이 이미 있음
		}

		LockFreeMemoryPoolTLS(const LockFreeMemoryPoolTLS&) = delete;
		LockFreeMemoryPoolTLS& operator=(const LockFreeMemoryPoolTLS&) = delete;

		/**
		 * @brief 객체 할당
		 * @param 없음
		 * @return (T*)할당된 객체. placementNew 가 false 면 직전 사용 때 상태 그대로
		 */
		T* Alloc()
		{
			Cache& cache = t_cache;
			if (cache.cur == nullptr)
				Refill(cache);

			Block* b = cache.cur;
			cache.cur = b->next;
			cache.curCount--;
			m_pool.ValidateAlloc(b);

			T* obj;
			void* storage = Pool::ToStorage(b);
			if (m_placementNew)
				obj = new (storage) T();
			else
				obj = reinterpret_cast<T*>(storage);

#if LFMP_STATS
			InterlockedIncrement64(&m_useCount);
#endif
			return obj;
		}

		/**
		 * @brief 객체 반환. 현재 스레드 캐시로 들어감
		 * @param (obj)반환할 객체 (이 풀에서 Alloc 한 포인터)
		 * @return 없음
		 */
		void Free(T* obj)
		{
			Block* b = Pool::ToBlock(obj);
			m_pool.ValidateFree(b);

			if (m_placementNew)
				obj->~T();

			Cache& cache = t_cache;
			b->next = cache.cur;
			cache.cur = b;
			if (++cache.curCount == CHUNK_SIZE)
				Seal(cache);

#if LFMP_STATS
			InterlockedDecrement64(&m_useCount);
#endif
		}

		/**
		 * @brief 확보한 블록 수 얻기
		 * @param 없음
		 * @return (DWORD)확보한 블록 수
		 */
		DWORD GetCapacityCount() const { return m_pool.GetCapacityCount(); }

#if LFMP_STATS
		/**
		 * @brief 사용 중인 블록 수 얻기
		 * @param 없음
		 * @return (LONG64)사용 중인 블록 수
		 */
		LONG64 GetUseCount() const { return m_useCount; }

		/**
		 * @brief 공용 풀에 청크를 반납한 횟수 / 가져온 횟수 얻기 (TLS_MAX_CHUNKS 조정 근거)
		 * @param 없음
		 * @return (LONG64)횟수
		 */
		LONG64 GetChunkPushCount() const { return m_pool.GetChunkPushCount(); }
		LONG64 GetChunkPopCount() const { return m_pool.GetChunkPopCount(); }
#endif

	private:
		void Refill(Cache& cache)
		{
			Block* head;
			if (cache.chunkTop != nullptr)
			{
				head = cache.chunkTop;
				cache.chunkTop = head->chunkNext;
				cache.chunkCount--;
			}
			else
			{
				head = m_pool.PopChunk();
			}

			cache.cur = head;
			cache.curCount = head->count;
		}

		void Seal(Cache& cache)
		{
			cache.cur->count = cache.curCount;
			cache.cur->chunkNext = cache.chunkTop;
			cache.chunkTop = cache.cur;
			cache.chunkCount++;
			cache.cur = nullptr;
			cache.curCount = 0;

			if (cache.chunkCount > TLS_MAX_CHUNKS)
			{
				Block* give = cache.chunkTop;
				cache.chunkTop = give->chunkNext;
				cache.chunkCount--;
				m_pool.PushChunk(give);
			}
		}

	private:
		const bool	m_placementNew;
		Pool		m_pool;
#if LFMP_STATS
		alignas(64) volatile LONG64 m_useCount;
#endif
	};

	template <typename T>
	thread_local typename LockFreeMemoryPoolTLS<T>::Cache LockFreeMemoryPoolTLS<T>::t_cache;

	template <typename T>
	LockFreeMemoryPoolTLS<T>* volatile LockFreeMemoryPoolTLS<T>::s_self = nullptr;
}
