#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <chrono>
#include <atomic>

// defined in main.cpp - global stop signal
extern std::atomic<bool> g_running;

class DummyClient
{
public:
	DummyClient(int id_);
	~DummyClient();

	void Run();

private:
	void SendPacket(const char* pPacket, int size);
	void Login();
	void EnterRoom();
	void SendChat();
	void ProcessRecv();
	void CloseConnection();

	int m_id;
	SOCKET m_socket = INVALID_SOCKET;
	std::thread m_recvThread;
	std::atomic<bool> m_isConnected{ false };

	// timestamps for response time measurement
	std::chrono::steady_clock::time_point m_loginSendTime;
	std::chrono::steady_clock::time_point m_roomEnterSendTime;
	std::chrono::steady_clock::time_point m_chatSendTime;

	// flags: which response are we waiting for
	std::atomic<bool> m_waitingLoginResponse{ false };
	std::atomic<bool> m_waitingRoomEnterResponse{ false };
	std::atomic<bool> m_waitingChatResponse{ false };

	std::atomic<bool> m_needSendPong{ false };
};
