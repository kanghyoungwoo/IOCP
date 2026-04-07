#pragma once
#include "nlohmann/json.hpp"
#include "Define.h"
#include <fstream>
#include <string>
#include <cstdint>

class ConfigManager
{
public:
    struct Config
    {
        // Network
        uint16_t ServerPort = 11021;
        uint16_t MaxClient = 10000;
        uint32_t MaxIOWorkerThread = 8;

        // Thread
        uint32_t MaxWorkerThread = 4;
        uint32_t MaxLogicThread = 4;

        // Session
        uint64_t ReUseSessionWaitTimeSec = 3;
        uint64_t TimeoutMs = 60000;
        uint64_t PingIntervalMs = 30000;
        uint64_t CheckIntervalMs = 10000;
        uint32_t MaxPendingAccept = 100;

        // Room
        uint32_t StartRoomNumber = 0;
        uint32_t MaxRoomUserCount = 8;
        uint32_t MaxRoomCount = 1250;

        // Pool
        uint32_t JobPoolSize = 100000;
        uint32_t CallbackPoolSize = 4000;

        // Database
        std::string MySQLHost = "127.0.0.1";
        std::string MySQLUser = "root";
        std::string MySQLPassword = "1234";
        std::string MySQLDatabase = "chatserver_local";
        uint32_t MySQLPort = 3306;
        std::string RedisHost = "127.0.0.1";
        uint32_t RedisPort = 6379;
        bool TestMode = false;
    };

    static ConfigManager& GetInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    bool Load(const std::string& filePath)
    {
        if (mLoaded)
        {
            LOG_ERROR("[Config] 이미 로드 됨. 중복 요청콜을 무시합니다.\n");
            return false;
        }
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            LOG_ERROR("Config file not found: %s (using defaults)\n", filePath.c_str());
            return false;
        }

        try
        {
            nlohmann::json j;
            file >> j;

            // Network
            mConfig.ServerPort = j.value("/Network/ServerPort"_json_pointer, mConfig.ServerPort);
            mConfig.MaxClient = j.value("/Network/MaxClient"_json_pointer, mConfig.MaxClient);
            mConfig.MaxIOWorkerThread = j.value("/Network/MaxIOWorkerThread"_json_pointer, mConfig.MaxIOWorkerThread);
            mConfig.TestMode = j.value("/TestMode"_json_pointer, mConfig.TestMode);

            // Thread
            mConfig.MaxWorkerThread = j.value("/Thread/MaxWorkerThread"_json_pointer, mConfig.MaxWorkerThread);
            mConfig.MaxLogicThread = j.value("/Thread/MaxLogicThread"_json_pointer, mConfig.MaxLogicThread);

            // Session
            mConfig.ReUseSessionWaitTimeSec = j.value("/Session/ReUseSessionWaitTimeSec"_json_pointer, mConfig.ReUseSessionWaitTimeSec);
            mConfig.TimeoutMs = j.value("/Session/TimeoutMs"_json_pointer, mConfig.TimeoutMs);
            mConfig.PingIntervalMs = j.value("/Session/PingIntervalMs"_json_pointer, mConfig.PingIntervalMs);
            mConfig.CheckIntervalMs = j.value("/Session/CheckIntervalMs"_json_pointer, mConfig.CheckIntervalMs);
            mConfig.MaxPendingAccept = j.value("/Session/MaxPendingAccept"_json_pointer, mConfig.MaxPendingAccept);

            // Room
            mConfig.StartRoomNumber = j.value("/Room/StartRoomNumber"_json_pointer, mConfig.StartRoomNumber);
            mConfig.MaxRoomUserCount = j.value("/Room/MaxRoomUserCount"_json_pointer, mConfig.MaxRoomUserCount);
            mConfig.MaxRoomCount = j.value("/Room/MaxRoomCount"_json_pointer, mConfig.MaxRoomCount);

            // Pool
            mConfig.JobPoolSize = j.value("/Pool/JobPoolSize"_json_pointer, mConfig.JobPoolSize);
            mConfig.CallbackPoolSize = j.value("/Pool/CallbackPoolSize"_json_pointer, mConfig.CallbackPoolSize);

            // Database
            mConfig.MySQLHost = j.value("/Database/MySQLHost"_json_pointer, mConfig.MySQLHost);
            mConfig.MySQLUser = j.value("/Database/MySQLUser"_json_pointer, mConfig.MySQLUser);
            mConfig.MySQLPassword = j.value("/Database/MySQLPassword"_json_pointer, mConfig.MySQLPassword);
            mConfig.MySQLDatabase = j.value("/Database/MySQLDatabase"_json_pointer, mConfig.MySQLDatabase);
            mConfig.MySQLPort = j.value("/Database/MySQLPort"_json_pointer, mConfig.MySQLPort);
            mConfig.RedisHost = j.value("/Database/RedisHost"_json_pointer, mConfig.RedisHost);
            mConfig.RedisPort = j.value("/Database/RedisPort"_json_pointer, mConfig.RedisPort);
        }
        catch (const nlohmann::json::exception& e)
        {
            LOG_ERROR("Config load error: %s (using defaults)\n", e.what());
            return false;
        }

        if (!Validate())
        {
            LOG_ERROR("[Config] 유효하지 않은 값들이라 default로 초기화 합니다.\n");
        }
        LOG_DEBUG("[Config] Loaded: Port=%d, MaxClient=%d, IO=%d, Worker=%d, Logic=%d\n",
            mConfig.ServerPort, mConfig.MaxClient,
            mConfig.MaxIOWorkerThread, mConfig.MaxWorkerThread, mConfig.MaxLogicThread);
        
        mLoaded = true; // 성공 시에만 플래그 세팅 
        return true;
    }


    const Config& Get() const { return mConfig; }

private:

    bool Validate()
    {
        bool valid = true;

        // 스레드 수: 0이면 서버 구동 불가
        if (mConfig.MaxIOWorkerThread == 0)
        {
            LOG_ERROR("[Config] MaxIOWorkerThread cannot be 0, reset to default(8)\n");
            mConfig.MaxIOWorkerThread = 8;
            valid = false;
        }
        if (mConfig.MaxWorkerThread == 0)
        {
            LOG_ERROR("[Config] MaxWorkerThread cannot be 0, reset to default(4)\n");
            mConfig.MaxWorkerThread = 4;
            valid = false;
        }
        if (mConfig.MaxLogicThread == 0)
        {
            LOG_ERROR("[Config] MaxLogicThread cannot be 0, reset to default(4)\n");
            mConfig.MaxLogicThread = 4;
            valid = false;
        }

        // 클라이언트 수: 0이면 서버 의미 없음
        if (mConfig.MaxClient == 0)
        {
            LOG_ERROR("[Config] MaxClient cannot be 0, reset to default(10000)\n");
            mConfig.MaxClient = 10000;
            valid = false;
        }

        // 방 설정: 0이면 방 생성 불가
        if (mConfig.MaxRoomCount == 0)
        {
            LOG_ERROR("[Config] MaxRoomCount cannot be 0, reset to default(1250)\n");
            mConfig.MaxRoomCount = 1250;
            valid = false;
        }
        if (mConfig.MaxRoomUserCount == 0)
        {
            LOG_ERROR("[Config] MaxRoomUserCount cannot be 0, reset to default(8)\n");
            mConfig.MaxRoomUserCount = 8;
            valid = false;
        }

        // 풀 사이즈: 0이면 할당 즉시 실패
        if (mConfig.JobPoolSize == 0)
        {
            LOG_ERROR("[Config] JobPoolSize cannot be 0, reset to default(100000)\n");
            mConfig.JobPoolSize = 100000;
            valid = false;
        }
        if (mConfig.CallbackPoolSize == 0)
        {
            LOG_ERROR("[Config] CallbackPoolSize cannot be 0, reset to default(4000)\n");
            mConfig.CallbackPoolSize = 4000;
            valid = false;
        }

        // 타임아웃: PingInterval이 Timeout보다 크면 논리 오류
        if (mConfig.PingIntervalMs >= mConfig.TimeoutMs)
        {
            LOG_ERROR("[Config] PingIntervalMs(%llu) >= TimeoutMs(%llu), reset to defaults\n",
                mConfig.PingIntervalMs, mConfig.TimeoutMs);
            mConfig.TimeoutMs = 60000;
            mConfig.PingIntervalMs = 30000;
            valid = false;
        }

        return valid;
    }

    Config mConfig;
    bool mLoaded = false;
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
};
