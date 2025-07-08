#pragma once
#include"IOCP.h"
#include "Packet.h"
#include <deque>
#include <mutex>

class EchoServer : public IOCompletionPort
{
public:

	EchoServer() = default;
	virtual ~EchoServer() = default;

	virtual void OnConnect(const int clientIndex) override
	{
		printf("[OnConnect] Client Index : %d\n", clientIndex);
	}

	virtual void OnClose(const int clientIndex) override
	{
		printf("[OnClosed] Client Index : %d\n", clientIndex);
	}

	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData) override // onreceive를 통해 받은 
	{
		printf("[OnReceive] Ciient Index : %d , DataSize : %d, Recv Data : %s\n", clientIndex, size, pData);
		PacketData packetdata;
		packetdata.Set(clientIndex, size, pData); // 누가 받았는지, 크기는 얼마인지, 넘긴 데이터는 뭔지

		std::lock_guard<std::mutex>guard(mLock);
		mPacketDataQueue.push_back(packetdata);
	}
	 
	// 서버 시작할때 processpacket을 처리하는 쓰레드를 만듦
	void Run(const UINT32 maxClient)
	{
		mIsRunProcessThread = true;
		mProcessThread = std::thread([this]() { ProcessPacket();});

		StartServer(maxClient);
	}

	//서버 시작할때 processpacket을 처리하는 쓰레드를 만듦->queue에 있는 data를 읽어오고 그 데이터가 쓸 수 있는 데이터면(datasize가 있으면) send하고 아니라면(datasize)가 없다면 재움(cpu 낭비를 줄임)

private:
	void ProcessPacket()
	{
		while (mIsRunProcessThread)
		{
			auto packetData = DequePacketData();// queue에 있는 데이터를 읽어와서 

			if (packetData.DataSize != 0) // 쓸 수 있는 데이터면 sendmsg
			{
				SendMsg(packetData.ClientSessionIndex, packetData.DataSize, packetData.pPacketData);
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	PacketData DequePacketData()
	{
		PacketData packetdata;

		std::lock_guard<std::mutex> gurad(mLock);
		if (mPacketDataQueue.empty())
		{
			return PacketData();
		}
		packetdata.Set(mPacketDataQueue.front());
		mPacketDataQueue.front().Release(); // 굳이 release? 소멸자 사용은 ?
		mPacketDataQueue.pop_front();
		
		return packetdata;
	}


	std::thread mProcessThread;
	std::mutex mLock;
	std::deque<PacketData>mPacketDataQueue;
	bool mIsRunProcessThread = false;
};