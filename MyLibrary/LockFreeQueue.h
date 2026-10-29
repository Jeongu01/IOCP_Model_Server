#pragma once
#include <Windows.h>
#include "LockFreeMemoryPoolTLS.h"

/*
* LockFreeQueue
*/
template<typename T>
class LockFreeQueue
{
private:
    struct Node
    {
        T data;
        volatile DWORD64 next;
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
        return ((tag & TAG_MASK) << TAG_SHIFT) | ((DWORD64)(ptr)&PTR_MASK);
    }

public:
    LockFreeQueue()
    {
        _size = 0;

        Node* dummy = s_nodePool.Alloc();
        dummy->next = 0;

        _head = MakeTagged(0, dummy);
        _tail = _head;
    }

    ~LockFreeQueue()
    {
        Node* node = GetPointer(_head);
        while (node != nullptr)
        {
            Node* next = GetPointer(node->next);
            s_nodePool.Free(node);
            node = next;
        }
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    void Enqueue(T t)
    {
        Node* node = s_nodePool.Alloc();
        node->data = t;
        node->next = 0;

        while (true)
        {
            DWORD64 tailTagged = _tail;
            Node* tail = GetPointer(tailTagged);

            DWORD64 nextTagged = tail->next;
            Node* next = GetPointer(nextTagged);

            if (next == nullptr)
            {
                DWORD64 newNextTagged = MakeTagged(GetTag(nextTagged) + 1, node);
                if (InterlockedCompareExchange64((volatile LONG64*)&tail->next, newNextTagged, nextTagged) == nextTagged)
                {
                    DWORD64 newTailTagged = MakeTagged(GetTag(tailTagged) + 1, node);
                    InterlockedCompareExchange64((volatile LONG64*)&_tail, newTailTagged, tailTagged);
                    break;
                }
            }
            else
            {
                DWORD64 newTailTagged = MakeTagged(GetTag(tailTagged) + 1, next);
                InterlockedCompareExchange64((volatile LONG64*)&_tail, newTailTagged, tailTagged);
            }
        }

        InterlockedExchangeAdd(&_size, 1);
    }

    bool Dequeue(T& t)
    {
        int curSize = InterlockedDecrement(&_size);
        if (curSize < 0)
        {
            InterlockedIncrement(&_size);
            return false;
        }

        while (true)
        {
            DWORD64 headTagged = _head;
            DWORD64 tailTagged = _tail;

            Node* head = GetPointer(headTagged);
            Node* tail = GetPointer(tailTagged);

            DWORD64 nextTagged = head->next;
            Node* next = GetPointer(nextTagged);

            if (next == nullptr)
                continue;

            if (head == tail)
            {
                DWORD64 newTailTagged = MakeTagged(GetTag(tailTagged) + 1, next);
                InterlockedCompareExchange64((volatile LONG64*)&_tail, newTailTagged, tailTagged);
                continue;
            }

            T data = next->data;
            DWORD64 newHeadTagged = MakeTagged(GetTag(headTagged) + 1, next);
            if (InterlockedCompareExchange64((volatile LONG64*)&_head, newHeadTagged, headTagged) == headTagged)
            {
                t = data;
                s_nodePool.Free(head);
                break;
            }
        }
        return true;
    }

    LONG GetSize() const { return _size; }
private:
    volatile LONG _size;
    volatile DWORD64 _head;
    volatile DWORD64 _tail;

    static jeongu::LockFreeMemoryPoolTLS<Node> s_nodePool;
};

template<typename T>
jeongu::LockFreeMemoryPoolTLS<typename LockFreeQueue<T>::Node> LockFreeQueue<T>::s_nodePool(false, 0, 0);