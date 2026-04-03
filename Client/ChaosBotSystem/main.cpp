#include "BotEngine.h"
#include "ScenarioRunner.h"
#include "Statistics.h"
#include "Logger.h"
#include <iostream>
#include <conio.h>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

bool LoadConfig(const std::string& filename, ChaosConfig& config)
{
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    
    try
    {
        json j;
        file >> j;
        
        if (j.contains("engine"))
        {
            auto& e = j["engine"];
            config.engine.serverHost = e.value("serverHost", "127.0.0.1");
            config.engine.serverPort = e.value("serverPort", 11021);
            config.engine.totalBots = e.value("totalBots", 100);
            config.engine.ioWorkerThreads = e.value("ioWorkerThreads", 4);
            config.engine.connectBatchSize = e.value("connectBatchSize", 50);
            config.engine.connectIntervalMs = e.value("connectIntervalMs", 100);
        }
        if (j.contains("proxy"))
        {
            auto& p = j["proxy"];
            config.proxy.enabled = p.value("enabled", false);
            config.proxy.fragmentEnabled = p.value("fragmentEnabled", false);
            config.proxy.fragmentSize = p.value("fragmentSize", 1);
            config.proxy.fragmentDelayUs = p.value("fragmentDelayUs", 50);
            config.proxy.delayEnabled = p.value("delayEnabled", false);
            config.proxy.delayMinUs = p.value("delayMinUs", 100);
            config.proxy.delayMaxUs = p.value("delayMaxUs", 500);
            config.proxy.delayProbability = p.value("delayProbability", 0.1);
            config.proxy.dropEnabled = p.value("dropEnabled", false);
            config.proxy.dropProbability = p.value("dropProbability", 0.01);
            config.proxy.reorderEnabled = p.value("reorderEnabled", false);
            config.proxy.reorderProbability = p.value("reorderProbability", 0.05);
            config.proxy.holdEnabled = p.value("holdEnabled", false);
            config.proxy.holdPacketId = p.value("holdPacketId", 215);
            config.proxy.holdTargetSessionIndex = p.value("holdTargetSessionIndex", 0);
            config.proxy.holdDurationMs = p.value("holdDurationMs", 5000);
        }
        if (j.contains("scenario"))
        {
            auto& s = j["scenario"];
            int type = s.value("type", 1);
            config.scenario.type = static_cast<ScenarioType>(type);

            // 시나리오별 세부 설정 읽기
            config.scenario.scenarioA.reconnectCycleCount = s.value("reconnectCycleCount", 70000);
            config.scenario.scenarioB.repeatCount = s.value("repeatCount", 1000);
            config.scenario.scenarioC.zombieBotCount = s.value("zombieBotCount", 200);
            config.scenario.durationSec = s.value("durationSec", 60);
        }
    }
    catch (const std::exception& e)
    {
        printf("[Config] JSON parse error: %s\n", e.what());
        return false;
    }
    return true;
}

void SetupDefaultConfig(ChaosConfig& config)
{
    config.engine.serverHost = "127.0.0.1";
    config.engine.serverPort = 11021;
    config.engine.totalBots = 500;
    config.engine.ioWorkerThreads = 8;
    config.proxy.enabled = true;
    config.scenario.type = ScenarioType::SCENARIO_A_ABA_OVERFLOW;
    config.proxy.holdEnabled = true;
    config.proxy.holdPacketId = static_cast<uint16_t>(PACKET_ID::ROOM_LEAVE_REQUEST);
    config.proxy.holdTargetSessionIndex = 0;
    config.proxy.holdDurationMs = 5000;
}

int main()
{
    LOG_UI("=============================================================\n");
    LOG_UI("        CHAOS BOT SYSTEM - Starting Destruction Phase        \n");
    LOG_UI("=============================================================\n");

    ChaosConfig config;
    if (!LoadConfig("config.json", config))
    {
        LOG_INFO("Warning: Failed to load config.json. Using default scenario A config.");
        SetupDefaultConfig(config);
    }
    else
    {
        LOG_INFO("Successfully loaded config.json");
    }

    BotEngine engine;
    if (!engine.Init(config))
    {
        LOG_ERROR("Engine initialization failed!");
        return -1;
    }

    engine.Run();

    ScenarioRunner runner(&engine);
    runner.RunScenario(config);

    LOG_UI("\n[Main] Running... Press 'ESC' to stop, 'S' for Summary.\n");

    while (true)
    {
        GetStats().PrintLiveLine();

        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 27) break; // ESC
            if (ch == 's' || ch == 'S') GetStats().PrintSummary();
        }
        Sleep(100);
    }

    LOG_UI("\n[Main] Stopping system...\n");
    runner.Stop();
    engine.Stop();

    GetStats().PrintSummary();
    LOG_INFO("Chaos simulation finished.");

    return 0;
}
