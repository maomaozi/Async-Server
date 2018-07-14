#pragma once

#include <type_traits>
#include <cstdint>

#if defined(__INTEL_COMPILER)
#define AE_ICC
#elif defined(_MSC_VER)
#define AE_VCPP
#elif defined(__GNUC__)
#define AE_GCC
#endif

#define AE_ARCH_X64
#define AE_UNUSED(x) ((void)x)


#if defined(AE_VCPP) || defined(AE_ICC)
#define AE_FORCEINLINE __forceinline
#elif defined(AE_GCC)
#define AE_FORCEINLINE inline
#else
#define AE_FORCEINLINE inline
#endif


#if defined(AE_VCPP) || defined(AE_ICC)
#define AE_ALIGN(x) __declspec(align(x))
#elif defined(AE_GCC)
#define AE_ALIGN(x) __attribute__((aligned(x)))
#else
#define AE_ALIGN(x) __attribute__((aligned(x)))
#endif


enum memory_order {
	memory_order_relaxed,
	memory_order_acquire,
	memory_order_release,
	memory_order_acq_rel,
	memory_order_seq_cst,
	memory_order_sync = memory_order_seq_cst
};


#if (defined(AE_VCPP) && (_MSC_VER < 1700 || defined(__cplusplus_cli))) || defined(AE_ICC)
#include <intrin.h>

#if defined(AE_ARCH_X64) || defined(AE_ARCH_X86)
#define AeFullSync _mm_mfence
#define AeLiteSync _mm_mfence
#elif defined(AE_ARCH_IA64)
#define AeFullSync __mf
#define AeLiteSync __mf
#elif defined(AE_ARCH_PPC)
#include <ppcintrinsics.h>
#define AeFullSync __sync
#define AeLiteSync __lwsync
#endif


#ifdef AE_VCPP
#pragma warning(push)
#pragma warning(disable: 4365)
#ifdef __cplusplus_cli
#pragma managed(push, off)
#endif
#endif


AE_FORCEINLINE void compiler_fence(memory_order order){
	switch (order) {
		case memory_order_relaxed: break;
		case memory_order_acquire: _ReadBarrier(); break;
		case memory_order_release: _WriteBarrier(); break;
		case memory_order_acq_rel: _ReadWriteBarrier(); break;
		case memory_order_seq_cst: _ReadWriteBarrier(); break;
		default: break;
	}
}


#if defined(AE_ARCH_X86) || defined(AE_ARCH_X64)
AE_FORCEINLINE void fence(memory_order order){
	switch (order) {
		case memory_order_relaxed: break;
		case memory_order_acquire: _ReadBarrier(); break;
		case memory_order_release: _WriteBarrier(); break;
		case memory_order_acq_rel: _ReadWriteBarrier(); break;
		case memory_order_seq_cst:
			_ReadWriteBarrier();
			AeFullSync();
			_ReadWriteBarrier();
			break;
		default: break;
	}
}
#else
AE_FORCEINLINE void fence(memory_order order){
	switch (order) {
		case memory_order_relaxed:
			break;
		case memory_order_acquire:
			_ReadBarrier();
			AeLiteSync();
			_ReadBarrier();
			break;
		case memory_order_release:
			_WriteBarrier();
			AeLiteSync();
			_WriteBarrier();
			break;
		case memory_order_acq_rel:
			_ReadWriteBarrier();
			AeLiteSync();
			_ReadWriteBarrier();
			break;
		case memory_order_seq_cst:
			_ReadWriteBarrier();
			AeFullSync();
			_ReadWriteBarrier();
			break;
		default: break;
	}
}
#endif
#else
#include <atomic>


AE_FORCEINLINE void compiler_fence(memory_order order){
	switch (order) {
		case memory_order_relaxed: break;
		case memory_order_acquire: std::atomic_signal_fence(std::memory_order_acquire); break;
		case memory_order_release: std::atomic_signal_fence(std::memory_order_release); break;
		case memory_order_acq_rel: std::atomic_signal_fence(std::memory_order_acq_rel); break;
		case memory_order_seq_cst: std::atomic_signal_fence(std::memory_order_seq_cst); break;
		default: break;
	}
}

AE_FORCEINLINE void fence(memory_order order){
	switch (order) {
		case memory_order_relaxed: break;
		case memory_order_acquire: std::atomic_thread_fence(std::memory_order_acquire); break;
		case memory_order_release: std::atomic_thread_fence(std::memory_order_release); break;
		case memory_order_acq_rel: std::atomic_thread_fence(std::memory_order_acq_rel); break;
		case memory_order_seq_cst: std::atomic_thread_fence(std::memory_order_seq_cst); break;
		default: break;
	}
}


#endif


#if !defined(AE_VCPP) || (_MSC_VER >= 1700 && !defined(__cplusplus_cli))
#define AE_USE_STD_ATOMIC_FOR_WEAK_ATOMIC
#endif

#ifdef AE_USE_STD_ATOMIC_FOR_WEAK_ATOMIC
#include <atomic>
#endif
#include <utility>

template<typename T>
class weak_atomic
{
public:
	weak_atomic() { }
#ifdef AE_VCPP
#pragma warning(push)
#pragma warning(disable: 4100)
#endif
	template<typename U> weak_atomic(U&& x) : value(std::forward<U>(x)) {  }
#ifdef __cplusplus_cli
	weak_atomic(nullptr_t) : value(nullptr) {  }
#endif
	weak_atomic(weak_atomic const& other) : value(other.value) {  }
	weak_atomic(weak_atomic&& other) : value(std::move(other.value)) {  }
#ifdef AE_VCPP
#pragma warning(pop)
#endif

	AE_FORCEINLINE operator T() const { return load(); }


#ifndef AE_USE_STD_ATOMIC_FOR_WEAK_ATOMIC
	template<typename U> AE_FORCEINLINE weak_atomic const& operator=(U&& x) { value = std::forward<U>(x); return *this; }
	AE_FORCEINLINE weak_atomic const& operator=(weak_atomic const& other) { value = other.value; return *this; }

	AE_FORCEINLINE T load() const { return value; }

	AE_FORCEINLINE T fetch_add_acquire(T increment)
	{
#if defined(AE_ARCH_X64) || defined(AE_ARCH_X86)
		if (sizeof(T) == 4) return _InterlockedExchangeAdd((long volatile*)&value, (long)increment);
#if defined(_M_AMD64)
		else if (sizeof(T) == 8) return _InterlockedExchangeAdd64((long long volatile*)&value, (long long)increment);
#endif
#else
#error Unsupported platform
#endif
		return value;
	}

	AE_FORCEINLINE T fetch_add_release(T increment)
	{
#if defined(AE_ARCH_X64) || defined(AE_ARCH_X86)
		if (sizeof(T) == 4) return _InterlockedExchangeAdd((long volatile*)&value, (long)increment);
#if defined(_M_AMD64)
		else if (sizeof(T) == 8) return _InterlockedExchangeAdd64((long long volatile*)&value, (long long)increment);
#endif
#else
#error Unsupported platform
#endif
		return value;
	}
#else
	template<typename U>
	AE_FORCEINLINE weak_atomic const& operator=(U&& x){
		value.store(std::forward<U>(x), std::memory_order_relaxed);
		return *this;
	}

	AE_FORCEINLINE weak_atomic const& operator=(weak_atomic const& other){
		value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
		return *this;
	}

	AE_FORCEINLINE T load() const { return value.load(std::memory_order_relaxed); }

	AE_FORCEINLINE T fetch_add_acquire(T increment){
		return value.fetch_add(increment, std::memory_order_acquire);
	}

	AE_FORCEINLINE T fetch_add_release(T increment){
		return value.fetch_add(increment, std::memory_order_release);
	}
#endif


private:
#ifndef AE_USE_STD_ATOMIC_FOR_WEAK_ATOMIC
	volatile T value;
#else
	std::atomic<T> value;
#endif
};


#if defined(_WIN32)
extern "C" {
	struct _SECURITY_ATTRIBUTES;
	__declspec(dllimport) void* __stdcall CreateSemaphoreW(_SECURITY_ATTRIBUTES* lpSemaphoreAttributes, long lInitialCount, long lMaximumCount, const wchar_t* lpName);
	__declspec(dllimport) int __stdcall CloseHandle(void* hObject);
	__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* hHandle, unsigned long dwMilliseconds);
	__declspec(dllimport) int __stdcall ReleaseSemaphore(void* hSemaphore, long lReleaseCount, long* lpPreviousCount);
}
#elif defined(__MACH__)
#include <mach/mach.h>
#elif defined(__unix__)
#include <semaphore.h>
#endif
	namespace spsc_sema
	{
#if defined(_WIN32)
		class Semaphore
		{
		private:
		    void* m_hSema;

		    Semaphore(const Semaphore& other);
		    Semaphore& operator=(const Semaphore& other);

		public:
		    Semaphore(int initialCount = 0)
		    {
		        const long maxLong = 0x7fffffff;
		        m_hSema = CreateSemaphoreW(nullptr, initialCount, maxLong, nullptr);
		    }

		    ~Semaphore()
		    {
		        CloseHandle(m_hSema);
		    }

		    void wait()
		    {
		    	const unsigned long infinite = 0xffffffff;
		        WaitForSingleObject(m_hSema, infinite);
		    }

			bool try_wait()
			{
				const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
				return WaitForSingleObject(m_hSema, 0) != RC_WAIT_TIMEOUT;
			}

			bool timed_wait(std::uint64_t usecs)
			{
				const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
				return WaitForSingleObject(m_hSema, (unsigned long)(usecs / 1000)) != RC_WAIT_TIMEOUT;
			}

		    void signal(int count = 1)
		    {
		        ReleaseSemaphore(m_hSema, count, nullptr);
		    }
		};
#elif defined(__MACH__)
		class Semaphore
		{
		private:
		    semaphore_t m_sema;

		    Semaphore(const Semaphore& other);
		    Semaphore& operator=(const Semaphore& other);

		public:
		    Semaphore(int initialCount = 0){
		        semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
		    }

		    ~Semaphore(){
		        semaphore_destroy(mach_task_self(), m_sema);
		    }

		    void wait(){
		        semaphore_wait(m_sema);
		    }

			bool try_wait(){
				return timed_wait(0);
			}

			bool timed_wait(std::int64_t timeout_usecs){
				mach_timespec_t ts;
				ts.tv_sec = timeout_usecs / 1000000;
				ts.tv_nsec = (timeout_usecs % 1000000) * 1000;

				kern_return_t rc = semaphore_timedwait(m_sema, ts);

				return rc != KERN_OPERATION_TIMED_OUT;
			}

		    void signal(){
		        semaphore_signal(m_sema);
		    }

		    void signal(int count){
		        while (count-- > 0){
		            semaphore_signal(m_sema);
		        }
		    }
		};
#elif defined(__unix__)
		class Semaphore{
		private:
		    sem_t m_sema;

		    Semaphore(const Semaphore& other);
		    Semaphore& operator=(const Semaphore& other);

		public:
		    Semaphore(int initialCount = 0)
		    {
		        sem_init(&m_sema, 0, initialCount);
		    }

		    ~Semaphore()
		    {
		        sem_destroy(&m_sema);
		    }

		    void wait(){
		        int rc;
		        do{
		            rc = sem_wait(&m_sema);
		        }
		        while (rc == -1 && errno == EINTR);
		    }

			bool try_wait(){
				int rc;
				do {
					rc = sem_trywait(&m_sema);
				} while (rc == -1 && errno == EINTR);
				return !(rc == -1 && errno == EAGAIN);
			}

			bool timed_wait(std::uint64_t usecs){
				struct timespec ts;
				const int usecs_in_1_sec = 1000000;
				const int nsecs_in_1_sec = 1000000000;
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += usecs / usecs_in_1_sec;
				ts.tv_nsec += (usecs % usecs_in_1_sec) * 1000;
				if (ts.tv_nsec >= nsecs_in_1_sec) {
					ts.tv_nsec -= nsecs_in_1_sec;
					++ts.tv_sec;
				}

				int rc;
				do {
					rc = sem_timedwait(&m_sema, &ts);
				} while (rc == -1 && errno == EINTR);
				return !(rc == -1 && errno == ETIMEDOUT);
			}

		    void signal()
		    {
		        sem_post(&m_sema);
		    }

		    void signal(int count)
		    {
		        while (count-- > 0)
		        {
		            sem_post(&m_sema);
		        }
		    }
		};
#endif
		class LightweightSemaphore{
		public:
			typedef std::make_signed<std::size_t>::type ssize_t;

		private:
		    weak_atomic<ssize_t> m_count;
		    Semaphore m_sema;

		    bool waitWithPartialSpinning(std::int64_t timeout_usecs = -1)
		    {
		        ssize_t oldCount;
		        int spin = 10000;
		        while (--spin >= 0)
		        {
		            if (m_count.load() > 0)
		            {
		                m_count.fetch_add_acquire(-1);
		                return true;
		            }
		            compiler_fence(memory_order_acquire);
		        }
		        oldCount = m_count.fetch_add_acquire(-1);
				if (oldCount > 0)
					return true;
		        if (timeout_usecs < 0)
				{
					m_sema.wait();
					return true;
				}
				if (m_sema.timed_wait(timeout_usecs))
					return true;

				while (1){
					oldCount = m_count.fetch_add_release(1);
					if (oldCount < 0)
						return false;
					oldCount = m_count.fetch_add_acquire(-1);
					if (oldCount > 0 && m_sema.try_wait())
						return true;
				}
		    }

		public:
		    LightweightSemaphore(ssize_t initialCount = 0) : m_count(initialCount){}

		    bool tryWait(){
		        if (m_count.load() > 0){
		        	m_count.fetch_add_acquire(-1);
		        	return true;
		        }
		        return false;
		    }

		    void wait(){
		        if (!tryWait())
		            waitWithPartialSpinning();
		    }

			bool wait(std::int64_t timeout_usecs)
			{
				return tryWait() || waitWithPartialSpinning(timeout_usecs);
			}

		    void signal(ssize_t count = 1){
		        ssize_t oldCount = m_count.fetch_add_release(count);
		        if (oldCount < 0)
		        {
		            m_sema.signal(1);
		        }
		    }

		    ssize_t availableApprox() const{
		    	ssize_t count = m_count.load();
		    	return count > 0 ? count : 0;
		    }
		};
	}

#if defined(AE_VCPP) && (_MSC_VER < 1700 || defined(__cplusplus_cli))
#pragma warning(pop)
#ifdef __cplusplus_cli
#pragma managed(pop)
#endif
#endif
