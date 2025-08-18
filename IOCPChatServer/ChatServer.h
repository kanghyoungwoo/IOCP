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

	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) override // onreceive를 통해 받은 
	{
		printf("[OnReceive] Ciient Index : %d , DataSize : %d\n", clientIndex_, size_);

		//printf("수신 패킷 PacketLength : %u, PacketId : %u\n", ,pData_->PacketLength, pHeader->PacketId);

		m_pPacketManager->ReceivePacketData(clientIndex_, size_, pData_);
	}
	 
	// 서버 시작할때 processpacket을 처리하는 쓰레드를 만듦
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
		//mIsRunProcessThread = false;
		//if (mProcessThread.joinable())
		//{
		//	mProcessThread.join();
		//}
		DestroyThread();
	}

	//서버 시작할때 processpacket을 처리하는 쓰레드를 만듦->queue에 있는 data를 읽어오고 그 데이터가 쓸 수 있는 데이터면(datasize가 있으면) send하고 아니라면(datasize)가 없다면 재움(cpu 낭비를 줄임)

private:
	//void ProcessPacket()
	//{
	//	while (mIsRunProcessThread)
	//	{
	//		auto packetData = DequePacketData();// queue에 있는 데이터를 읽어와서 

	//		if (packetData.DataSize != 0) // 쓸 수 있는 데이터면 sendmsg
	//		{
	//			SendMsg(packetData.ClientSessionIndex, packetData.DataSize, packetData.pPacketData);
	//		}
	//		else
	//		{
	//			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	//		}
	//	}
	//}

	//RawPacketData DequePacketData()
	//{
	//	RawPacketData packetdata;

	//	std::lock_guard<std::mutex> gurad(mLock);
	//	if (mPacketDataQueue.empty())
	//	{
	//		return RawPacketData();
	//	}
	//	packetdata.Set(mPacketDataQueue.front());
	//	mPacketDataQueue.front().Release(); // 굳이 release? 소멸자 사용은 ?
	//	mPacketDataQueue.pop_front();
	//	
	//	return packetdata;
	//}


	//std::thread mProcessThread;
	//std::mutex mLock;
	//std::deque<RawPacketData>mPacketDataQueue;
	//bool mIsRunProcessThread = false;


	std::unique_ptr<PacketManager>m_pPacketManager;

};