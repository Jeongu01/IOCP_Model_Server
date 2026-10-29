#pragma once
#include <Windows.h>
#include "LockFreeMemoryPoolTLS.h"

/*
* LockFreeStack
*/

template<typename T>
class LockFreeStack
{
private:
	struct Node
	{
		T _Data = 0;
		Node* _Next = nullptr;
	};

	static const DWORD64 PTR_MASK = 0x00007fffffffffff;
	static const int TAG_SHIFT = 47;
	static const DWORD64 TAG_MASK = 0x1ffff;

	inline Node* GetPointer(DWORD64 taggedPtr)
	{
		return (Node*)(taggedPtr & PTR_MASK);
	}

	inline DWORD64 GetTag(DWORD64 taggedPtr)
	{
		return (taggedPtr >> TAG_SHIFT) & TAG_MASK;
	}

	inline DWORD64 MakeTagged(DWORD64 tag, Node* ptr)
	{
		return ((tag & TAG_MASK) << TAG_SHIFT) | ((DWORD64)(ptr) & PTR_MASK);
	}

public:
	LockFreeStack() : _Top(0) {}
	~LockFreeStack() 
	{
		Node* current = GetPointer(_Top);
		while (current)
		{
			Node* next = current->_Next;
			s_nodePool.Free(current);
			current = next;
		}
	}

	LockFreeStack(const LockFreeStack&) = delete;
	LockFreeStack& operator=(const LockFreeStack&) = delete;

	void Push(T data)
	{
		Node* newNode = s_nodePool.Alloc();
		newNode->_Data = data;

		Node* currentTopPtr = nullptr;
		DWORD64 currentTopTagged = 0;
		DWORD64 newTopTagged = 0;
		DWORD64 nextTag = 0;

		do
		{
			currentTopTagged = _Top;
			currentTopPtr = GetPointer(currentTopTagged);
			newNode->_Next = currentTopPtr;
			
			nextTag = GetTag(currentTopTagged) + 1;
			newTopTagged = MakeTagged(nextTag, newNode);
		} while (InterlockedCompareExchange(&_Top, newTopTagged, currentTopTagged) != currentTopTagged);
	}

	bool Pop(T* ret)
	{
		DWORD64 oldTopTagged = 0;
		DWORD64 nextTopTagged = 0;
		Node* oldTopPtr = nullptr;
		Node* nextTopPtr = nullptr;
		DWORD64 nextTopTag = 0;

		do
		{
			oldTopTagged = _Top;
			oldTopPtr = GetPointer(oldTopTagged);
			if (oldTopPtr == nullptr)
				return false;

			nextTopPtr = oldTopPtr->_Next;
			nextTopTag = GetTag(oldTopTagged) + 1;
			nextTopTagged = MakeTagged(nextTopTag, nextTopPtr);
		} while (InterlockedCompareExchange(&_Top, nextTopTagged, oldTopTagged) != oldTopTagged);

		(*ret) = oldTopPtr->_Data;
		s_nodePool.Free(oldTopPtr);
		return true;
	}

private:
	volatile DWORD64 _Top;
	static jeongu::LockFreeMemoryPoolTLS<Node> s_nodePool;
};

template<typename T>
jeongu::LockFreeMemoryPoolTLS<typename LockFreeStack<T>::Node> LockFreeStack<T>::s_nodePool(false, 0, 0);