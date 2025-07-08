#pragma once

typedef struct _stPacketData
{
	int ClientSessionIndex = 0;
	int DataSize = 0;
	char* pPacketData = nullptr;

	void Set(_stPacketData& value)
	{
		ClientSessionIndex = value.ClientSessionIndex;
		DataSize = value.DataSize;
		pPacketData = new char[value.DataSize];
		memcpy(pPacketData, value.pPacketData, value.DataSize);
	}
	
	void Set(int ClientSessionIndex_, int DataSize_, char *pData_)
	{
		ClientSessionIndex = ClientSessionIndex_;
		DataSize = DataSize_;

		if (pPacketData != nullptr)
		{
			delete[] pPacketData;
			pPacketData = nullptr;
		}
		pPacketData = new char[DataSize_];
		memcpy(pPacketData, pData_, DataSize_);
	}

	void Release()
	{
		delete[] pPacketData;
		pPacketData = nullptr;

	}

	//~_stPacketData()
	//{
	//	if (pPacketData != nullptr)
	//	{
	//		delete[] pPacketData;
	//		pPacketData = nullptr;
	//	}
	//}


}PacketData;