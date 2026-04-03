#include "ScenarioRunner.h"
#include "Statistics.h"
#include "Logger.h"
#include <windows.h>
#include <iostream>

ScenarioRunner::ScenarioRunner(BotEngine* engine) : mEngine(engine)
{
}

ScenarioRunner::~ScenarioRunner()
{
    Stop();
}

void ScenarioRunner::RunScenario(const ChaosConfig& config)
{
    mIsRunning = true;
    LOG_DEBUG("[ScenarioRunner] Connecting %u bots in batches...", config.engine.totalBots);

    for (uint32_t i = 0; i < config.engine.totalBots; ++i)
    {
        if (!mIsRunning) break;

        mEngine->ConnectBot(i);
        mEngine->GetBot(i)->SetTargetRoom(i % (std::max)(1U, config.engine.roomCount));

        if (config.scenario.type == ScenarioType::SCENARIO_A_ABA_OVERFLOW)
        {
            if (i == 0) mEngine->GetBot(i)->SetBehavior(BotBehavior::HOLD_VICTIM);
            else {
                mEngine->GetBot(i)->SetBehavior(BotBehavior::GENERATION_PUMP);
                mEngine->GetBot(i)->SetMaxCycles(config.scenario.scenarioA.reconnectCycleCount);
                // 기본값 70000 → 16bit 카운터(65536) 래핑 유도
            }
        }
        else if (config.scenario.type == ScenarioType::SCENARIO_B_STRAND_REENTRY)
        {
            mEngine->GetBot(i)->SetBehavior(BotBehavior::PIPELINE_BURST);
            mEngine->GetBot(i)->SetMaxCycles(config.scenario.scenarioB.repeatCount);
            // 기본값 1000
        }
        else if (config.scenario.type == ScenarioType::SCENARIO_C_ZOMBIE_RACE)
        {
            mEngine->GetBot(i)->SetBehavior(BotBehavior::ZOMBIE_ATTACK);
            mEngine->GetBot(i)->SetMaxCycles(config.scenario.scenarioC.zombieBotCount);
            // 또는 별도 반복 횟수 필드 추가
        }

        if (i % config.engine.connectBatchSize == 0 && i > 0)
        {
            Sleep(config.engine.connectIntervalMs);
        }
    }

    LOG_DEBUG("[ScenarioRunner] All bots deployed for scenario.");
}

void ScenarioRunner::Stop()
{
    mIsRunning = false;
}
