#pragma once

#include "IOCP.h"
#include "Packet.h"
#include "PacketManager.h"

#include <iostream>
#include <deque>
#include <mutex>
#include <vector>
#include <memory>
#include <thread>


class ChatServer : public IOCompletionPort
{
public:

	ChatServer() = default;
	virtual ~ChatServer() = default;

	virtual void OnConnect(const int clientIndex_, const UINT32 generation_) override
	{
		LOG_DEBUG("[OnConnect] Client Index : %d\n", clientIndex_);
		PacketInfo packet;
		packet.ClientIndex = clientIndex_;
		packet.Generation = generation_;  // Connect 시점엔 아직 의미 없음
		packet.PacketId = (UINT16)PACKET_ID::SYS_USER_CONNECT;
		packet.DataSize = 0;
		m_pPacketManager->PushSystemPacket(packet);
	}

	virtual void OnClose(const int clientIndex_, const UINT32 generation_) override
	{
		LOG_DEBUG("[OnClosed] Client Index : %d\n", clientIndex_);
		//PacketInfo packet{ clientIndex_, (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0 };
		PacketInfo packet;
		packet.ClientIndex = clientIndex_;
		packet.Generation = generation_;  // 
		packet.PacketId = (UINT16)PACKET_ID::SYS_USER_DISCONNECT;
		packet.DataSize = 0;

		m_pPacketManager->PushSystemPacket(packet);

	}

	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 generation_, const UINT32 size_, char* pData_) override
	{
		LOG_DEBUG("[OnReceive] Ciient Index : %d , DataSize : %d\n", clientIndex_, size_);

		if (!m_pPacketManager->ReceivePacketData(clientIndex_, generation_, size_, pData_))
		{
			// 버퍼오버플로우 -> 악의적 클라이언트, 연결 해제
			DisconnectClient(clientIndex_);
		}
		//m_pPacketManager->ReceivePacketData(clientIndex_, size_, pData_);
	}

	void Run(const UINT32 maxClient)
	{
		auto sendPacketFunc = [&](UINT32 clientIndex_, UINT32 gen_, UINT32 packetSize_, char* pSendPacket)
		{
			SendMsg(clientIndex_, gen_, packetSize_, pSendPacket);
		};

		// 유효 패킷 처리 시 활동 시간 갱신 콜백
		auto updateActivityFunc = [&](UINT32 clientIndex_)
		{
			UpdateClientActivity(clientIndex_);
		};

		m_pPacketManager = std::make_unique<PacketManager>();
		m_pPacketManager->SendPacketFunc = sendPacketFunc;
		m_pPacketManager->UpdateActivityFunc = updateActivityFunc;
		m_pPacketManager->Init(maxClient);
		m_pPacketManager->Run();

		StartServer(maxClient);
	}

	void End()
	{
		bool expected = false;
		if (!mIsEnded.exchange(true))
		{
			// 벤치마크 (콘솔 + server.log + 별도 파일)
			LOG_INFO("=== Benchmark Result ===");
			LOG_INFO("SendPool Alloc Fail: %llu", GetSendPoolAllocFailCount());
			LOG_INFO("========================");

			FILE* fp = nullptr;
			fopen_s(&fp, "benchmark_result.txt", "a");
			if (fp) {
				fprintf(fp, "SendPool Alloc Fail: %llu\n\n", GetSendPoolAllocFailCount());
				fclose(fp);
			}

			DestroyThread();			// step1~5, 네트워크 레이어 종료
			m_pPacketManager->End();	// step6: 패킷매니저 + DB매니저
		}
		// 역순 종료 방식
		//m_pPacketManager->End();
		//DestroyThread();

	}


private:
	std::unique_ptr<PacketManager>m_pPacketManager;
	std::atomic<bool>mIsEnded{ false };
};
