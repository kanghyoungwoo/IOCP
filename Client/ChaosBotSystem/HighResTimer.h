#pragma once
// ================================================================
// HighResTimer.h - Microsecond precision timer control
// QueryPerformanceCounter based + Spin-Wait + Hybrid Sleep
// ================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <intrin.h>  // _mm_pause

namespace HiResTimer
{
    // QPC based timestamp (microsecond)
    inline int64_t NowMicroseconds()
    {
        static LARGE_INTEGER freq = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
        }();

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart * 1000000) / freq.QuadPart;
    }

    inline int64_t NowMilliseconds()
    {
        return NowMicroseconds() / 1000;
    }

    // Spin-Wait for microsecond precision
    inline void SpinWaitMicroseconds(int64_t us)
    {
        auto start = std::chrono::high_resolution_clock::now();
        auto target = std::chrono::microseconds(us);

        while (std::chrono::high_resolution_clock::now() - start < target)
        {
            _mm_pause();
        }
    }

    // Hybrid Wait: Sleep for long periods, Spin-Wait for the rest
    class HybridWaiter
    {
    public:
        HybridWaiter()
        {
            // Set system timer resolution to 1ms
            // Requires winmm.lib
        }

        ~HybridWaiter()
        {
        }

        void WaitMicroseconds(int64_t us) const
        {
            if (us <= 0) return;

            auto start = std::chrono::high_resolution_clock::now();
            auto target = std::chrono::microseconds(us);

            if (us > 1500)
            {
                int64_t sleepMs = (us - 500) / 1000;
                if (sleepMs > 0)
                {
                    Sleep(static_cast<DWORD>(sleepMs));
                }
            }

            while (std::chrono::high_resolution_clock::now() - start < target)
            {
                _mm_pause();
            }
        }

        void WaitMilliseconds(int64_t ms) const
        {
            WaitMicroseconds(ms * 1000);
        }

    private:
        HybridWaiter(const HybridWaiter&) = delete;
        HybridWaiter& operator=(const HybridWaiter&) = delete;
    };

    class Stopwatch
    {
    public:
        void Start()
        {
            mStart = std::chrono::high_resolution_clock::now();
        }

        int64_t ElapsedMicroseconds() const
        {
            auto now = std::chrono::high_resolution_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(now - mStart).count();
        }

        int64_t ElapsedMilliseconds() const
        {
            return ElapsedMicroseconds() / 1000;
        }

        double ElapsedSeconds() const
        {
            return ElapsedMicroseconds() / 1000000.0;
        }

    private:
        std::chrono::high_resolution_clock::time_point mStart;
    };

    inline int64_t NowMs()
    {
        return NowMilliseconds();
    }
}
