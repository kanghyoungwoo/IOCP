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

	virtual void OnConnect(const int clientIndex_) override
	{
		LOG_DEBUG("[OnConnect] Client Index : %d\n", clientIndex_);
		PacketInfo packet{ clientIndex_, (UINT16)PACKET_ID::SYS_USER_CONNECT,0};
		m_pPacketManager->PushSystemPacket(packet);
	}

	virtual void OnClose(const int clientIndex_) override
	{
		LOG_DEBUG("[OnClosed] Client Index : %d\n", clientIndex_);
		PacketInfo packet{ clientIndex_, (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0 };
		m_pPacketManager->PushSystemPacket(packet);

	}

	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) override
	{
		LOG_DEBUG("[OnReceive] Client Index : %d , DataSize : %d\n", clientIndex_, size_);


		m_pPacketManager->ReceivePacketData(clientIndex_, size_, pData_);
	}
	 
	void Run(const UINT32 maxClient)
	{
		auto sendPacketFunc = [&](UINT32 clientIndex_, UINT16 packetSize_, char* pSendPacket)
		{
			SendMsg(clientIndex_, packetSize_, pSendPacket);
		};

		m_pPacketManager = std::make_unique<PacketManager>();
		m_pPacketManager->SendPacketFunc = sendPacketFunc;
		m_pPacketManager->Init(maxClient);
		m_pPacketManager->Run();

		StartServer(maxClient);
	}

	void End()
	{
		DestroyThread();			// step1~5, 네트워크 레이어 종료
		m_pPacketManager->End();	// step6: 패킷매니저 + DB매니저

		// 역순 종료 방식
		//m_pPacketManager->End();
		//DestroyThread();

		// 벤치마크 (콘솔 + 파일)
		printf("\n=== Benchmark Result ===\n");
		printf("IOCP Workers: %d\n", MAX_WORKERTHREAD);
		printf("SendPool Alloc Fail: %llu\n", GetSendPoolAllocFailCount());
		printf("========================\n\n");

		FILE* fp = nullptr;
		fopen_s(&fp, "benchmark_result.txt", "a");
		if (fp) {
			fprintf(fp, "=== Benchmark Result ===\n");
			fprintf(fp, "IOCP Workers: %d\n", MAX_WORKERTHREAD);
			fprintf(fp, "SendPool Alloc Fail: %llu\n", GetSendPoolAllocFailCount());
			fprintf(fp, "========================\n\n");
			fclose(fp);
		}
	}


private:
	std::unique_ptr<PacketManager>m_pPacketManager;

};