#pragma once
// ================================================================
// ScenarioRunner.h - 시나리오 오케스트레이션 및 봇 행동 제어
// ================================================================

#include "ChaosConfig.h"
#include "BotEngine.h"

class ScenarioRunner
{
public:
    ScenarioRunner(BotEngine* engine);
    ~ScenarioRunner();

    void RunScenario(const ChaosConfig& config);
    void Stop();

private:
    BotEngine* mEngine = nullptr;
    bool mIsRunning = false;
};
