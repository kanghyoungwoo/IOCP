#pragma once
// ================================================================
// Statistics.h - Real-time metrics collection
// ================================================================

#include "Logger.h"
#include <atomic>
#include <cstdint>
#include <chrono>

struct BotStatistics
{
    // Connection
    std::atomic<uint64_t> connectAttempts{ 0 };
    std::atomic<uint64_t> connectSuccess{ 0 };
    std::atomic<uint64_t> connectFailed{ 0 };
    std::atomic<uint64_t> disconnects{ 0 };
    std::atomic<uint64_t> hardCloses{ 0 };           // RST sent

    // Packets
    std::atomic<uint64_t> packetsSent{ 0 };
    std::atomic<uint64_t> packetsReceived{ 0 };
    std::atomic<uint64_t> bytesSent{ 0 };
    std::atomic<uint64_t> bytesReceived{ 0 };
    std::atomic<uint64_t> sendErrors{ 0 };

    // Protocol
    std::atomic<uint64_t> loginRequests{ 0 };
    std::atomic<uint64_t> loginSuccess{ 0 };
    std::atomic<uint64_t> loginFailed{ 0 };
    std::atomic<uint64_t> roomEnterRequests{ 0 };
    std::atomic<uint64_t> roomEnterSuccess{ 0 };
    std::atomic<uint64_t> roomEnterFailed{ 0 };
    std::atomic<uint64_t> roomLeaveRequests{ 0 };
    std::atomic<uint64_t> chatMessagesSent{ 0 };
    std::atomic<uint64_t> chatNotifiesReceived{ 0 };
    std::atomic<uint64_t> pongsSent{ 0 };

    // Scenario Specific
    std::atomic<uint64_t> generationPumpCycles{ 0 };  // Scenario A
    std::atomic<uint64_t> pipelineBursts{ 0 };         // Scenario B
    std::atomic<uint64_t> zombieReconnects{ 0 };       // Scenario C
    std::atomic<uint64_t> unexpectedDisconnects{ 0 };  // Server-side disconnect

    // Proxy
    std::atomic<uint64_t> proxyFragments{ 0 };
    std::atomic<uint64_t> proxyDelays{ 0 };
    std::atomic<uint64_t> proxyDrops{ 0 };
    std::atomic<uint64_t> proxyReorders{ 0 };
    std::atomic<uint64_t> proxyHolds{ 0 };

    // Vulnerability Detection
    std::atomic<uint64_t> abaVulnerabilityDetected{ 0 };
    std::atomic<uint64_t> strandRaceDetected{ 0 };
    std::atomic<uint64_t> zombieRaceDetected{ 0 };
    std::atomic<uint64_t> serverCrashDetected{ 0 };

    // Scenario D
    std::atomic<uint64_t> routingBoundaryAttempts{ 0 };   // LeaveRoom + 즉시 Chat 전송 횟수
    std::atomic<uint64_t> routingBoundaryDrops{ 0 };      // 경계 Chat에 대한 unexpectedDisconnect 횟수         

    std::chrono::steady_clock::time_point startTime;

    void Reset()
    {
        connectAttempts = 0; connectSuccess = 0; connectFailed = 0;
        disconnects = 0; hardCloses = 0;
        packetsSent = 0; packetsReceived = 0;
        bytesSent = 0; bytesReceived = 0; sendErrors = 0;
        loginRequests = 0; loginSuccess = 0; loginFailed = 0;
        roomEnterRequests = 0; roomEnterSuccess = 0; roomEnterFailed = 0;
        roomLeaveRequests = 0;
        chatMessagesSent = 0; chatNotifiesReceived = 0; pongsSent = 0;
        generationPumpCycles = 0; pipelineBursts = 0; zombieReconnects = 0;
        unexpectedDisconnects = 0;
        proxyFragments = 0; proxyDelays = 0; proxyDrops = 0;
        proxyReorders = 0; proxyHolds = 0;
        abaVulnerabilityDetected = 0; strandRaceDetected = 0;
        zombieRaceDetected = 0; serverCrashDetected = 0;
        routingBoundaryAttempts = 0; routingBoundaryDrops = 0;
        startTime = std::chrono::steady_clock::now();
    }

    double ElapsedSeconds() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - startTime).count();
    }

    void PrintSummary() const
    {
        double elapsed = ElapsedSeconds();
        LOG_UI("\n");
        LOG_UI("=============================================================\n");
        LOG_UI("  CHAOS BOT - Statistics (%.1f sec elapsed)\n", elapsed);
        LOG_UI("=============================================================\n");

        LOG_UI("\n  [Connection]\n");
        LOG_UI("    Attempts: %llu  Success: %llu  Failed: %llu\n",
            connectAttempts.load(), connectSuccess.load(), connectFailed.load());
        LOG_UI("    Disconnects: %llu  Hard Close(RST): %llu\n",
            disconnects.load(), hardCloses.load());

        LOG_UI("\n  [Packet I/O]\n");
        LOG_UI("    Sent: %llu (%.2f MB)  Recv: %llu (%.2f MB)\n",
            packetsSent.load(), bytesSent.load() / (1024.0 * 1024.0),
            packetsReceived.load(), bytesReceived.load() / (1024.0 * 1024.0));
        LOG_UI("    Send Errors: %llu\n", sendErrors.load());
        if (elapsed > 0)
        {
            LOG_UI("    Throughput: %.0f pkt/s sent, %.0f pkt/s recv\n",
                packetsSent.load() / elapsed, packetsReceived.load() / elapsed);
        }

        LOG_UI("\n  [Protocol]\n");
        LOG_UI("    Login: %llu req, %llu ok, %llu fail\n",
            loginRequests.load(), loginSuccess.load(), loginFailed.load());
        LOG_UI("    Room Enter: %llu req, %llu ok, %llu fail\n",
            roomEnterRequests.load(), roomEnterSuccess.load(), roomEnterFailed.load());
        LOG_UI("    Room Leave: %llu  Chat Sent: %llu  Chat Recv: %llu\n",
            roomLeaveRequests.load(), chatMessagesSent.load(), chatNotifiesReceived.load());
        LOG_UI("    Pong: %llu\n", pongsSent.load());

        LOG_UI("\n  [Scenario Metrics]\n");
        LOG_UI("    Gen Pump Cycles: %llu  Pipeline Bursts: %llu\n",
            generationPumpCycles.load(), pipelineBursts.load());
        LOG_UI("    Zombie Reconnects: %llu  Unexpected Disconnects: %llu\n",
            zombieReconnects.load(), unexpectedDisconnects.load());

        LOG_UI("\n  [Proxy]\n");
        LOG_UI("    Fragments: %llu  Delays: %llu  Drops: %llu  Reorders: %llu  Holds: %llu\n",
            proxyFragments.load(), proxyDelays.load(), proxyDrops.load(),
            proxyReorders.load(), proxyHolds.load());

        LOG_UI("\n  *** VULNERABILITY DETECTION ***\n");
        LOG_UI("    [A] ABA Overflow:         %llu\n", abaVulnerabilityDetected.load());
        LOG_UI("    [B] Strand Race:          %llu\n", strandRaceDetected.load());
        LOG_UI("    [C] Zombie Race:          %llu\n", zombieRaceDetected.load());
        LOG_UI("    [D] Routing Boundary:     attempts=%llu  drops=%llu\n",
            routingBoundaryAttempts.load(), routingBoundaryDrops.load());
        LOG_UI("    Server Crash:             %llu\n", serverCrashDetected.load());
        LOG_UI("=============================================================\n\n");
    }

    void PrintLiveLine() const
    {
        double elapsed = ElapsedSeconds();
        uint64_t conn = connectSuccess.load();
        uint64_t sent = packetsSent.load();
        uint64_t recv = packetsReceived.load();
        uint64_t vuln = abaVulnerabilityDetected.load()
                      + strandRaceDetected.load()
                      + zombieRaceDetected.load();

        LOG_UI("\r[%.0fs] Conn:%llu Sent:%llu Recv:%llu Err:%llu Vuln:%llu   ",
            elapsed, conn, sent, recv, sendErrors.load(), vuln);
        fflush(stdout);
    }
};

inline BotStatistics& GetStats()
{
    static BotStatistics s_stats;
    return s_stats;
}
