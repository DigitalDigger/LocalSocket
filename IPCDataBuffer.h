#include <windows.h>

#define PACKET_SIZE 4096 
#define LIST_SIZE	512

struct s_CommonData
{
	s_CommonData()
	{
		nCommonDataBuffer = 0;
		bIsToStopAccepting = false;
		bIsDisconnected = false;
	}
	int nCommonDataBuffer;
	bool bIsToStopAccepting;
	bool bIsDisconnected;
};

struct s_PacketBuffer
{
	s_PacketBuffer()
	{
		nBytesToBeSent = 0;
		nPacketsToBeSent = 0;
		nCurrentNumber = 0;
		ZeroMemory(cStorage, PACKET_SIZE);
	}

	int nBytesToBeSent;
	int nPacketsToBeSent;
	int nCurrentNumber;
	char  cStorage[PACKET_SIZE];	// additional data string
};
