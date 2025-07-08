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

	virtual void OnReceive(const UINT32 clientIndex, const UINT32 size, char* pData) override // onreceive�� ���� ���� 
	{
		printf("[OnReceive] Ciient Index : %d , DataSize : %d, Recv Data : %s\n", clientIndex, size, pData);
		PacketData packetdata;
		packetdata.Set(clientIndex, size, pData); // ���� �޾Ҵ���, ũ��� ������, �ѱ� �����ʹ� ����

		std::lock_guard<std::mutex>guard(mLock);
		mPacketDataQueue.push_back(packetdata);
	}
	 
	// ���� �����Ҷ� processpacket�� ó���ϴ� �����带 ����
	void Run(const UINT32 maxClient)
	{
		mIsRunProcessThread = true;
		mProcessThread = std::thread([this]() { ProcessPacket();});

		StartServer(maxClient);
	}

	//���� �����Ҷ� processpacket�� ó���ϴ� �����带 ����->queue�� �ִ� data�� �о���� �� �����Ͱ� �� �� �ִ� �����͸�(datasize�� ������) send�ϰ� �ƴ϶��(datasize)�� ���ٸ� ���(cpu ���� ����)

private:
	void ProcessPacket()
	{
		while (mIsRunProcessThread)
		{
			auto packetData = DequePacketData();// queue�� �ִ� �����͸� �о�ͼ� 

			if (packetData.DataSize != 0) // �� �� �ִ� �����͸� sendmsg
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
		mPacketDataQueue.front().Release(); // ���� release? �Ҹ��� ����� ?
		mPacketDataQueue.pop_front();
		
		return packetdata;
	}


	std::thread mProcessThread;
	std::mutex mLock;
	std::deque<PacketData>mPacketDataQueue;
	bool mIsRunProcessThread = false;
};