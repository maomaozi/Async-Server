#pragma once

#include "atomic_ops.h"
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <new>
#include <cstdint>
#include <cstdlib>
#include <chrono>


#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif


template<typename T, size_t MAX_BLOCK_SIZE = 512>
class lockFreeQueue{
public:
	explicit lockFreeQueue(size_t maxSize = 15)
#ifndef NDEBUG
		: enqueuing(false)
		,dequeuing(false)
#endif
	{

		Block* firstBlock = nullptr;

		largestBlkSz = ceilToPow2(maxSize + 1);
		if (largestBlkSz > MAX_BLOCK_SIZE * 2) {
			size_t initialBlockCount = (maxSize + MAX_BLOCK_SIZE * 2 - 3) / (MAX_BLOCK_SIZE - 1);
			largestBlkSz = MAX_BLOCK_SIZE;
			Block* lastBlock = nullptr;
			for (size_t i = 0; i != initialBlockCount; ++i) {
				auto block = make_block(largestBlkSz);
				if (block == nullptr) {
					abort();
				}
				if (firstBlock == nullptr) {
					firstBlock = block;
				}
				else {
					lastBlock->next = block;
				}
				lastBlock = block;
				block->next = firstBlock;
			}
		}
		else {
			firstBlock = make_block(largestBlkSz);
			if (firstBlock == nullptr) {
				abort();
			}
			firstBlock->next = firstBlock;
		}
		frontBlock = firstBlock;
		tailBlock = firstBlock;
		fence(memory_order_sync);
	}

	lockFreeQueue(lockFreeQueue&& other)
		: frontBlock(other.frontBlock.load()),
		tailBlock(other.tailBlock.load()),
		largestBlkSz(other.largestBlkSz)
#ifndef NDEBUG
		,enqueuing(false)
		,dequeuing(false)
#endif
	{
		other.largestBlkSz = 32;
		Block* b = other.make_block(other.largestBlkSz);
		if (b == nullptr) {
			abort();
		}
		b->next = b;
		other.frontBlock = b;
		other.tailBlock = b;
	}

	lockFreeQueue& operator=(lockFreeQueue&& other)
	{
		Block* b = frontBlock.load();
		frontBlock = other.frontBlock.load();
		other.frontBlock = b;
		b = tailBlock.load();
		tailBlock = other.tailBlock.load();
		other.tailBlock = b;
		std::swap(largestBlkSz, other.largestBlkSz);
		return *this;
	}

	~lockFreeQueue(){
		fence(memory_order_sync);
		Block* frontBlock_ = frontBlock;
		Block* block = frontBlock_;
		do {
			Block* nextBlock = block->next;
			size_t blockFront = block->front;
			size_t blockTail = block->tail;

			for (size_t i = blockFront; i != blockTail; i = (i + 1) & block->sizeMask) {
				auto element = reinterpret_cast<T*>(block->data + i * sizeof(T));
				element->~T();
				(void)element;
			}

			auto rawBlock = block->rawThis;
			block->~Block();
			std::free(rawBlock);
			block = nextBlock;
		} while (block != frontBlock_);
	}


	AE_FORCEINLINE bool try_enqueue(T const& element){
		return inner_enqueue<CannotAlloc>(element);
	}

	AE_FORCEINLINE bool try_enqueue(T&& element){
		return inner_enqueue<CannotAlloc>(std::forward<T>(element));
	}


	AE_FORCEINLINE bool enqueue(T const& element){
		return inner_enqueue<CanAlloc>(element);
	}

	AE_FORCEINLINE bool enqueue(T&& element){
		return inner_enqueue<CanAlloc>(std::forward<T>(element));
	}


	template<typename U>
	bool try_dequeue(U& result)
	{
#ifndef NDEBUG
		ReentrantGuard guard(this->dequeuing);
#endif

		Block* frontBlock_ = frontBlock.load();
		size_t blockTail = frontBlock_->localTail;
		size_t blockFront = frontBlock_->front.load();

		if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
			fence(memory_order_acquire);

		non_empty_front_block:
			auto element = reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
			result = std::move(*element);
			element->~T();

			blockFront = (blockFront + 1) & frontBlock_->sizeMask;

			fence(memory_order_release);
			frontBlock_->front = blockFront;
		}
		else if (frontBlock_ != tailBlock.load()) {
			fence(memory_order_acquire);

			frontBlock_ = frontBlock.load();
			blockTail = frontBlock_->localTail = frontBlock_->tail.load();
			blockFront = frontBlock_->front.load();
			fence(memory_order_acquire);

			if (blockFront != blockTail) {
				goto non_empty_front_block;
			}
			Block* nextBlock = frontBlock_->next;

			size_t nextBlockFront = nextBlock->front.load();
			size_t nextBlockTail = nextBlock->localTail = nextBlock->tail.load();
			fence(memory_order_acquire);

			AE_UNUSED(nextBlockTail);

			fence(memory_order_release);
			frontBlock = frontBlock_ = nextBlock;

			compiler_fence(memory_order_release);

			auto element = reinterpret_cast<T*>(frontBlock_->data + nextBlockFront * sizeof(T));

			result = std::move(*element);
			element->~T();

			nextBlockFront = (nextBlockFront + 1) & frontBlock_->sizeMask;

			fence(memory_order_release);
			frontBlock_->front = nextBlockFront;
		}
		else {

			return false;
		}

		return true;
	}

	T* peek()
	{
#ifndef NDEBUG
		ReentrantGuard guard(this->dequeuing);
#endif
		Block* frontBlock_ = frontBlock.load();
		size_t blockTail = frontBlock_->localTail;
		size_t blockFront = frontBlock_->front.load();

		if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
			fence(memory_order_acquire);
		non_empty_front_block:
			return reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
		}
		else if (frontBlock_ != tailBlock.load()) {
			fence(memory_order_acquire);
			frontBlock_ = frontBlock.load();
			blockTail = frontBlock_->localTail = frontBlock_->tail.load();
			blockFront = frontBlock_->front.load();
			fence(memory_order_acquire);

			if (blockFront != blockTail) {
				goto non_empty_front_block;
			}

			Block* nextBlock = frontBlock_->next;

			size_t nextBlockFront = nextBlock->front.load();
			fence(memory_order_acquire);

			return reinterpret_cast<T*>(nextBlock->data + nextBlockFront * sizeof(T));
		}

		return nullptr;
	}

	bool pop()
	{
#ifndef NDEBUG
		ReentrantGuard guard(this->dequeuing);
#endif

		Block* frontBlock_ = frontBlock.load();
		size_t blockTail = frontBlock_->localTail;
		size_t blockFront = frontBlock_->front.load();

		if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
			fence(memory_order_acquire);

		non_empty_front_block:
			auto element = reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
			element->~T();

			blockFront = (blockFront + 1) & frontBlock_->sizeMask;

			fence(memory_order_release);
			frontBlock_->front = blockFront;
		}
		else if (frontBlock_ != tailBlock.load()) {
			fence(memory_order_acquire);
			frontBlock_ = frontBlock.load();
			blockTail = frontBlock_->localTail = frontBlock_->tail.load();
			blockFront = frontBlock_->front.load();
			fence(memory_order_acquire);

			if (blockFront != blockTail) {
				goto non_empty_front_block;
			}

			Block* nextBlock = frontBlock_->next;

			size_t nextBlockFront = nextBlock->front.load();
			size_t nextBlockTail = nextBlock->localTail = nextBlock->tail.load();
			fence(memory_order_acquire);

			AE_UNUSED(nextBlockTail);

			fence(memory_order_release);
			frontBlock = frontBlock_ = nextBlock;

			compiler_fence(memory_order_release);

			auto element = reinterpret_cast<T*>(frontBlock_->data + nextBlockFront * sizeof(T));
			element->~T();

			nextBlockFront = (nextBlockFront + 1) & frontBlock_->sizeMask;

			fence(memory_order_release);
			frontBlock_->front = nextBlockFront;
		}
		else {
			return false;
		}

		return true;
	}

	inline size_t size_approx() const{
		size_t result = 0;
		Block* frontBlock_ = frontBlock.load();
		Block* block = frontBlock_;
		do {
			fence(memory_order_acquire);
			size_t blockFront = block->front.load();
			size_t blockTail = block->tail.load();
			result += (blockTail - blockFront) & block->sizeMask;
			block = block->next.load();
		} while (block != frontBlock_);
		return result;
	}


private:
	enum AllocationMode { CanAlloc, CannotAlloc };

	template<AllocationMode canAlloc, typename U>
	bool inner_enqueue(U&& element)
	{
#ifndef NDEBUG
		ReentrantGuard guard(this->enqueuing);
#endif

		Block* tailBlock_ = tailBlock.load();
		size_t blockFront = tailBlock_->localFront;
		size_t blockTail = tailBlock_->tail.load();

		size_t nextBlockTail = (blockTail + 1) & tailBlock_->sizeMask;
		if (nextBlockTail != blockFront || nextBlockTail != (tailBlock_->localFront = tailBlock_->front.load())) {
			fence(memory_order_acquire);
			char* location = tailBlock_->data + blockTail * sizeof(T);
			new (location) T(std::forward<U>(element));

			fence(memory_order_release);
			tailBlock_->tail = nextBlockTail;
		}
		else {
			fence(memory_order_acquire);
			if (tailBlock_->next.load() != frontBlock) {

				fence(memory_order_acquire);
				Block* tailBlockNext = tailBlock_->next.load();
				size_t nextBlockFront = tailBlockNext->localFront = tailBlockNext->front.load();
				nextBlockTail = tailBlockNext->tail.load();
				fence(memory_order_acquire);

				tailBlockNext->localFront = nextBlockFront;

				char* location = tailBlockNext->data + nextBlockTail * sizeof(T);
				new (location) T(std::forward<U>(element));

				tailBlockNext->tail = (nextBlockTail + 1) & tailBlockNext->sizeMask;

				fence(memory_order_release);
				tailBlock = tailBlockNext;
			}
			else if (canAlloc == CanAlloc) {
				auto newBlockSize = largestBlkSz >= MAX_BLOCK_SIZE ? largestBlkSz : largestBlkSz * 2;
				auto newBlock = make_block(newBlockSize);
				if (newBlock == nullptr) {
					return false;
				}
				largestBlkSz = newBlockSize;

				new (newBlock->data) T(std::forward<U>(element));

				newBlock->tail = newBlock->localTail = 1;

				newBlock->next = tailBlock_->next.load();
				tailBlock_->next = newBlock;
				fence(memory_order_release);
				tailBlock = newBlock;
			}
			else if (canAlloc == CannotAlloc) {
				return false;
			}
			else {
				return false;
			}
		}

		return true;
	}

	lockFreeQueue(lockFreeQueue const&) {  }

	lockFreeQueue& operator=(lockFreeQueue const&) {  }



	AE_FORCEINLINE static size_t ceilToPow2(size_t x){
		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		for (size_t i = 1; i < sizeof(size_t); i <<= 1) {
			x |= x >> (i << 3);
		}
		++x;
		return x;
	}

	template<typename U>
	static AE_FORCEINLINE char* align_for(char* ptr){
		const std::size_t alignment = std::alignment_of<U>::value;
		return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
	}
private:
#ifndef NDEBUG
	struct ReentrantGuard{
		ReentrantGuard(bool& _inSection)
			: inSection(_inSection){
			inSection = true;
		}

		~ReentrantGuard() { inSection = false; }

	private:
		ReentrantGuard& operator=(ReentrantGuard const&);

	private:
		bool& inSection;
	};
#endif

	struct Block{
		weak_atomic<size_t> front;
		size_t localTail;

		char cachelineFiller0[CACHE_LINE_SIZE - sizeof(weak_atomic<size_t>) - sizeof(size_t)];
		weak_atomic<size_t> tail;
		size_t localFront;

		char cachelineFiller1[CACHE_LINE_SIZE - sizeof(weak_atomic<size_t>) - sizeof(size_t)];
		weak_atomic<Block*> next;

		char* data;

		const size_t sizeMask;

		Block(size_t const& _size, char* _rawThis, char* _data)
			: front(0), localTail(0), tail(0), localFront(0), next(nullptr), data(_data), sizeMask(_size - 1), rawThis(_rawThis)
		{
		}

	private:
		Block& operator=(Block const&);

	public:
		char* rawThis;
	};


	static Block* make_block(size_t capacity){
		auto size = sizeof(Block) + std::alignment_of<Block>::value - 1;
		size += sizeof(T) * capacity + std::alignment_of<T>::value - 1;
		auto newBlockRaw = static_cast<char*>(std::malloc(size));
		if (newBlockRaw == nullptr) {
			return nullptr;
		}

		auto newBlockAligned = align_for<Block>(newBlockRaw);
		auto newBlockData = align_for<T>(newBlockAligned + sizeof(Block));
		return new (newBlockAligned) Block(capacity, newBlockRaw, newBlockData);
	}

private:
	weak_atomic<Block*> frontBlock;

	char cachelineFiller[CACHE_LINE_SIZE - sizeof(weak_atomic<Block*>)];
	weak_atomic<Block*> tailBlock;

	size_t largestBlkSz;

#ifndef NDEBUG
	bool enqueuing;
	bool dequeuing;
#endif
};


template<typename T, size_t MAX_BLOCK_SIZE = 512>
class BlockinglockFreeQueue
{
private:
	using lockFreeQueue_t = lockFreeQueue<T, MAX_BLOCK_SIZE>;

public:
	explicit BlockinglockFreeQueue(size_t maxSize = 15)
		: inner(maxSize)
	{ }


	AE_FORCEINLINE bool try_enqueue(T const& element)
	{
		if (inner.try_enqueue(element)) {
			sema.signal();
			return true;
		}
		return false;
	}

	AE_FORCEINLINE bool try_enqueue(T&& element)
	{
		if (inner.try_enqueue(std::forward<T>(element))) {
			sema.signal();
			return true;
		}
		return false;
	}


	AE_FORCEINLINE bool enqueue(T const& element)
	{
		if (inner.enqueue(element)) {
			sema.signal();
			return true;
		}
		return false;
	}


	AE_FORCEINLINE bool enqueue(T&& element)
	{
		if (inner.enqueue(std::forward<T>(element))) {
			sema.signal();
			return true;
		}
		return false;
	}


	template<typename U>
	bool try_dequeue(U& result)
	{
		if (sema.tryWait()) {
			bool success = inner.try_dequeue(result);
			AE_UNUSED(success);
			return true;
		}
		return false;
	}

	template<typename U>
	void wait_dequeue(U& result)
	{
		sema.wait();
		bool success = inner.try_dequeue(result);
		AE_UNUSED(result);
		AE_UNUSED(success);
	}

	template<typename U>
	bool wait_dequeue_timed(U& result, std::int64_t timeout_usecs)
	{
		if (!sema.wait(timeout_usecs)) {
			return false;
		}
		bool success = inner.try_dequeue(result);
		AE_UNUSED(result);
		AE_UNUSED(success);
		return true;
	}


#if __cplusplus > 199711L || _MSC_VER >= 1700
	template<typename U, typename Rep, typename Period>
	inline bool wait_dequeue_timed(U& result, std::chrono::duration<Rep, Period> const& timeout)
	{
        return wait_dequeue_timed(result, std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
	}
#endif

	AE_FORCEINLINE T* peek()
	{
		return inner.peek();
	}

	AE_FORCEINLINE bool pop()
	{
		if (sema.tryWait()) {
			bool result = inner.pop();
			AE_UNUSED(result);
			return true;
		}
		return false;
	}
	AE_FORCEINLINE size_t size_approx() const
	{
		return sema.availableApprox();
	}


private:
	BlockinglockFreeQueue(lockFreeQueue_t const&) {  }
	BlockinglockFreeQueue& operator=(lockFreeQueue_t const&) {  }

private:
	lockFreeQueue_t inner;
	spsc_sema::LightweightSemaphore sema;
};
