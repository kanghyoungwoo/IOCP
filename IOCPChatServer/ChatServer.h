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
		printf("[OnConnect] Client Index : %d\n", clientIndex_);
		PacketInfo packet{ clientIndex_, (UINT16)PACKET_ID::SYS_USER_CONNECT,0};
		m_pPacketManager->PushSystemPacket(packet);
	}

	virtual void OnClose(const int clientIndex_) override
	{
		printf("[OnClosed] Client Index : %d\n", clientIndex_);
		PacketInfo packet{ clientIndex_, (UINT16)PACKET_ID::SYS_USER_DISCONNECT, 0 };
		m_pPacketManager->PushSystemPacket(packet);

	}

	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) override
	{
		printf("[OnReceive] Ciient Index : %d , DataSize : %d\n", clientIndex_, size_);


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
		m_pPacketManager->End();
		DestroyThread();
	}


private:
	std::unique_ptr<PacketManager>m_pPacketManager;

};