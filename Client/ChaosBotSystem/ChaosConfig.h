#pragma once
// ================================================================
// ChaosConfig.h - Chaos Bot System Configuration
// ================================================================

#include "Protocol.h"
#include <string>
#include <cstdint>

struct ChaosProxyConfig
{
    bool     enabled = false;
    uint16_t listenPort = 12021;
    std::string targetHost = "127.0.0.1";
    uint16_t targetPort = 11021;

    bool     fragmentEnabled = false;
    uint32_t fragmentSize = 1;
    uint32_t fragmentDelayUs = 100;

    bool     delayEnabled = false;
    uint32_t delayMinUs = 0;
    uint32_t delayMaxUs = 1000;
    double   delayProbability = 0.1;

    bool     dropEnabled = false;
    double   dropProbability = 0.01;

    bool     reorderEnabled = false;
    double   reorderProbability = 0.05;
    uint32_t reorderBufferSize = 4;

    bool     holdEnabled = false;
    uint16_t holdPacketId = 0;
    uint32_t holdDurationMs = 0;
    uint32_t holdTargetSessionIndex = 0;
};

struct BotEngineConfig
{
    std::string serverHost = "127.0.0.1";
    uint16_t serverPort = 11021;

    uint32_t totalBots = 100;
    uint32_t ioWorkerThreads = 4;
    uint32_t connectBatchSize = 50;
    uint32_t connectIntervalMs = 100;

    uint32_t roomCount = 100;
    uint32_t maxRoomUsers = 8;
    uint32_t chatIntervalMs = 500;
    uint32_t actionIntervalMs = 200;

    bool     tcpNoDelay = true;
};

enum class ScenarioType
{
    NONE = 0,
    SCENARIO_A_ABA_OVERFLOW,
    SCENARIO_B_STRAND_REENTRY,
    SCENARIO_C_ZOMBIE_RACE,
    STRESS_TEST,
    ALL_SCENARIOS
};

struct ScenarioConfig
{
    ScenarioType type = ScenarioType::STRESS_TEST;

    struct
    {
        uint32_t targetSessionSlot = 0;
        uint32_t pumpingBotCount = 50;
        uint32_t targetGeneration = 10;
        uint32_t reconnectCycleCount = 70000;
        uint32_t holdPacketId = static_cast<uint32_t>(PACKET_ID::ROOM_LEAVE_REQUEST);
    } scenarioA;

    struct
    {
        uint32_t pipeliningBotCount = 100;
        uint32_t burstSize = 4;
        uint32_t repeatCount = 1000;
    } scenarioB;

    struct
    {
        uint32_t zombieBotCount = 200;
        uint32_t broadcastFloodCount = 50;
        uint32_t reconnectDelayUs = 100;
        bool     useHardClose = true;
    } scenarioC;

    uint32_t durationSec = 60;
};

struct ChaosConfig
{
    BotEngineConfig engine;
    ChaosProxyConfig proxy;
    ScenarioConfig scenario;

    bool verboseLog = false;
    bool logToFile = true;
    std::string logFilePath = "chaos_bot.log";
};
