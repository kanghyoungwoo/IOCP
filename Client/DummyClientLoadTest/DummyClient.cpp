#include "DummyClient.h"
#include "Packet.h"
#include "ErrorCode.h"
#include "LoadTestConfig.h"
#include "LoadTestStats.h"

DummyClient::DummyClient(int id_)
	: m_id(id_)
{
}

DummyClient::~DummyClient()
{
	CloseConnection();
}

void DummyClient::Run()
{
	auto& stats = LoadTestStats::Instance();
	stats.connectAttempted++;

	m_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (m_socket == INVALID_SOCKET)
	{
		printf("[%d] socket creation failed\n", m_id);
		stats.connectFailed++;
		return;
	}

	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	inet_pton(AF_INET, LoadTestConfig::SERVER_IP, &serverAddr.sin_addr);
	serverAddr.sin_port = htons(LoadTestConfig::SERVER_PORT);

	if (connect(m_socket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		printf("[%d] connect failed\n", m_id);
		stats.connectFailed++;
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
		return;
	}

	stats.connectSuccess++;
	m_isConnected = true;

	m_recvThread = std::thread(&DummyClient::ProcessRecv, this);

	Login();
	// 응답 올 때까지 대기 (최대 5초 타임아웃)
	auto loginStart = std::chrono::steady_clock::now();
	while (m_waitingLoginResponse && m_isConnected && g_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		auto elapsed = std::chrono::steady_clock::now() - loginStart;
		if (elapsed > std::chrono::seconds(5))
			break;  // 타임아웃
	}

	if (!m_isConnected || !g_running)
		return;
	/*std::this_thread::sleep_for(std::chrono::milliseconds(200));*/
	EnterRoom();
	auto roomStart = std::chrono::steady_clock::now();
	while (m_waitingRoomEnterResponse && m_isConnected && g_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		auto elapsed = std::chrono::steady_clock::now() - roomStart;
		if (elapsed > std::chrono::seconds(5))
			break;
	}
	if (!m_isConnected || !g_running)
		return;
	while (m_isConnected && g_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(LoadTestConfig::CHAT_INTERVAL_MS));
		if (m_isConnected && g_running)
		{
			if (m_needSendPong.exchange(false)) 
			{
				PACKET_HEADER pongHeader;
				pongHeader.PacketLength = sizeof(PACKET_HEADER);
				pongHeader.PacketId = (UINT16)PACKET_ID::SYS_PONG;
				pongHeader.PacketType = 0;
				SendPacket(reinterpret_cast<char*>(&pongHeader), pongHeader.PacketLength);
				
			}
			SendChat();
		}
	}

	CloseConnection();
	if (m_recvThread.joinable())
		m_recvThread.join();
}

void DummyClient::CloseConnection()
{
	m_isConnected = false;
	if (m_socket != INVALID_SOCKET)
	{
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
}

void DummyClient::SendPacket(const char* pPacket, int size)
{
	if (m_socket == INVALID_SOCKET)
		return;

	int totalSent = 0;
	while (totalSent < size)
	{
		int sent = send(m_socket, pPacket + totalSent, size - totalSent, 0);
		if (sent == SOCKET_ERROR)
		{
			m_isConnected = false;
			return;
		}
		totalSent += sent;
	}
}

void DummyClient::Login()
{
	auto& stats = LoadTestStats::Instance();

	std::string userID = "test_user" + std::to_string(m_id);
	LOGIN_REQUEST_PACKET packet{};
	packet.PacketId = (UINT16)PACKET_ID::LOGIN_REQUEST;
	packet.PacketLength = sizeof(packet);
	strcpy_s(packet.UserID, userID.c_str());
	strcpy_s(packet.UserPW, "password");

	m_loginSendTime = std::chrono::steady_clock::now();
	m_waitingLoginResponse = true;
	stats.loginSent++;

	SendPacket(reinterpret_cast<char*>(&packet), packet.PacketLength);
}

void DummyClient::EnterRoom()
{
	auto& stats = LoadTestStats::Instance();

	ROOM_ENTER_REQUEST_PACKET packet{};
	packet.PacketId = (UINT16)PACKET_ID::ROOM_ENTER_REQUEST;
	packet.PacketLength = sizeof(packet);
	// room distribution: client 0~3 -> room 0, 4~7 -> room 1, ...
	packet.RoomNumber = m_id / LoadTestConfig::MAX_USERS_PER_ROOM;

	m_roomEnterSendTime = std::chrono::steady_clock::now();
	m_waitingRoomEnterResponse = true;
	stats.roomEnterSent++;

	SendPacket(reinterpret_cast<char*>(&packet), packet.PacketLength);
}

void DummyClient::SendChat()
{
	auto& stats = LoadTestStats::Instance();

	ROOM_CHAT_REQUEST_PACKET packet{};
	packet.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_REQUEST;

	std::string message = "Hello from Client " + std::to_string(m_id);
	strcpy_s(packet.Message, message.c_str());
	packet.PacketLength = sizeof(PACKET_HEADER) + (UINT16)message.length() + 1;

	m_chatSendTime = std::chrono::steady_clock::now();
	m_waitingChatResponse = true;
	stats.chatSent++;

	SendPacket(reinterpret_cast<char*>(&packet), packet.PacketLength);
}

void DummyClient::ProcessRecv()
{
	auto& stats = LoadTestStats::Instance();

	char recvBuffer[65536];
	int bufferedLen = 0;

	while (m_isConnected)
	{
		int recvLen = recv(m_socket, recvBuffer + bufferedLen,
			sizeof(recvBuffer) - bufferedLen, 0);

		if (recvLen <= 0)
		{
			m_isConnected = false;
			stats.disconnected++;
			break;
		}

		bufferedLen += recvLen;

		int readOffset = 0;  // ← 여기에 선언

		// parse all complete packets in buffer
		while (bufferedLen - readOffset >= (int)sizeof(PACKET_HEADER))
		{
			auto pHeader = reinterpret_cast<PACKET_HEADER*>(recvBuffer + readOffset);
			UINT16 packetLen = pHeader->PacketLength;

			// validate packet length
			if (packetLen > sizeof(recvBuffer) || packetLen < sizeof(PACKET_HEADER))
			{
				m_isConnected = false;
				stats.disconnected++;
				return;
			}

			// incomplete packet - wait for more data
			if (bufferedLen - readOffset < (int)packetLen)
				break;

			// complete packet arrived - dispatch
			auto now = std::chrono::steady_clock::now();

			switch ((PACKET_ID)pHeader->PacketId)
			{
			case PACKET_ID::LOGIN_RESPONSE:
			{
				auto pLogin = reinterpret_cast<LOGIN_RESPONSE_PACKET*>(recvBuffer + readOffset);
				if (pLogin->Result == (UINT16)ERROR_CODE::NONE)
					stats.loginSuccess++;
				else
					stats.loginFailed++;

				if (m_waitingLoginResponse.exchange(false))
				{
					auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
						now - m_loginSendTime).count();
					stats.RecordLoginResponseTime(elapsed);
				}
				break;
			}
			case PACKET_ID::ROOM_ENTER_RESPONSE:
			{
				auto pRoom = reinterpret_cast<ROOM_ENTER_RESPONSE_PACKET*>(recvBuffer);
				if (pRoom->Result == (INT16)ERROR_CODE::NONE)
					stats.roomEnterSuccess++;
				else
					stats.roomEnterFailed++;

				if (m_waitingRoomEnterResponse.exchange(false))
				{
					auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
						now - m_roomEnterSendTime).count();
					stats.RecordRoomEnterResponseTime(elapsed);
				}
				break;
			}
			case PACKET_ID::ROOM_CHAT_RESPONSE:
			{
				auto pChat = reinterpret_cast<ROOM_CHAT_RESPONSE_PACKET*>(recvBuffer);
				if (pChat->Result == (INT16)ERROR_CODE::NONE)
					stats.chatSuccess++;
				else
					stats.chatFailed++;

				if (m_waitingChatResponse.exchange(false))
				{
					auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
						now - m_chatSendTime).count();
					stats.RecordChatResponseTime(elapsed);
				}
				break;
			}
			case PACKET_ID::ROOM_CHAT_NOTIFY:
			{
				stats.chatNotifyReceived++;
				break;
			}
			case PACKET_ID::SYS_PING:
			{
				m_needSendPong = true;  // 플래그만 세움
				//// 서버로 부터 PING을 받으면 즉시 PONG 하는 로직을 run스레드에서
				//PACKET_HEADER pongHeader;
				//pongHeader.PacketLength = sizeof(PACKET_HEADER);
				//pongHeader.PacketId = (UINT16)PACKET_ID::SYS_PONG;
				//pongHeader.PacketType = 0;

				//SendPacket(reinterpret_cast<char*>(&pongHeader), pongHeader.PacketLength);
				break;
			}
			default:
				break;
			}
			readOffset += packetLen;
			//// remove processed packet, shift remaining data forward
			//int remaining = bufferedLen - packetLen;
			//if (remaining > 0)
			//	memmove(recvBuffer, recvBuffer + packetLen, remaining);
			//bufferedLen = remaining;
			
		}
		bufferedLen -= readOffset;
		if (bufferedLen > 0)
			memmove(recvBuffer, recvBuffer + readOffset, bufferedLen);

	}
}
