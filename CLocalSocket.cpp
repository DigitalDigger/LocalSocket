#include "LocalSocket.h"
#include <cstdio>
#include <iostream>
#include <sstream>


CLocalSocket::CLocalSocket(void) :  m_hInMappedBuffer(NULL),
									m_hOutMappedBuffer(NULL),
									m_hCommonMappedData(NULL),
									m_hDisconnectedMappedBuffer(NULL),
									m_bIsServerSocket(false),
									m_bAlreadyConnected(false),
									m_bIsNowAccepting(false),
									m_nConnectionAmount(1),
									m_nPacketsWrittenAmount(0),
									m_nCommonDataConnectionId(0),
									COMMON_DATA_MUTEX_POSTFIX("_MUT_COM"),
									COMMON_DATA_BUFFER_POSTFIX("_COM"),
									LOCAL_SOCKET_NAME_IN_POSTFIX("_In_"),
									LOCAL_SOCKET_NAME_OUT_POSTFIX("_Out_"),
									IN_DATA_EVENT_NAME_POSTFIX("_InEvent_"),
									OUT_DATA_EVENT_NAME_POSTFIX("_OutEvent_"),
									CONNECTION_EVENT_POSTFIX("_Con"),
									IN_DATA_READ_EVENT_NAME_POSTFIX("_RInEvent_"),
									OUT_DATA_READ_EVENT_NAME_POSTFIX("_ROutEvent_"),
									IN_DATA_MUTEX_NAME("_InMut_"),
									OUT_DATA_MUTEX_NAME("_OutMut_"),
									DISCONNECTED_MUTEX_NAME("_DisMut_"),
									DISCONNECTED_BUFFER_NAME("_DisBuf_"),
									m_nBufferSize(sizeof(s_PacketBuffer))
{
}


CLocalSocket::~CLocalSocket(void)
{
	close();
}

bool CLocalSocket::create_local(const char * szLocalSocketName)
{
	bool bRes = false;
    
	if (NULL == szLocalSocketName)
		return false;

	if (!IsServerSocket() && !IsAlreadyConnected())
	{
		SetIsServerSocket(true);
		m_strLocalSocketName = szLocalSocketName;
		bRes = InitializeCommon(true);
	}

	return bRes;
}

bool CLocalSocket::close(void)
{
	bool bRes = true;

	m_commonDataMutex.unlock();
	m_inDataMutex.unlock();
	m_outDataMutex.unlock();
	m_disconnectedMutex.unlock();

	m_connectionEvent.release();
	m_inDataWrittenEvent.release();
	m_outDataWrittenEvent.release();
	m_inDataHasBeenReadEvent.release();
	m_outDataHasBeenReadEvent.release();

	if (m_cCommonDataBuffer)
	{
		UnmapViewOfFile(m_cCommonDataBuffer);
		m_cCommonDataBuffer = NULL;
	}
	if (m_cInDataBuffer)
	{
		UnmapViewOfFile(m_cInDataBuffer);
		m_cInDataBuffer = NULL;
	}
	if (m_cOutDataBuffer)
	{
		UnmapViewOfFile(m_cOutDataBuffer);
		m_cOutDataBuffer = NULL;
	}
	if (m_cDisconnectedBuffer)
	{
		UnmapViewOfFile(m_cDisconnectedBuffer);
		m_cDisconnectedBuffer = NULL;
	}
	if (m_hCommonMappedData)
	{
		CloseHandle(m_hCommonMappedData);
		m_hCommonMappedData = NULL;
	}
	if (m_hInMappedBuffer)
	{
		CloseHandle(m_hInMappedBuffer);
		m_hInMappedBuffer = NULL;
	}
	if (m_hOutMappedBuffer)
	{
		CloseHandle(m_hOutMappedBuffer);
		m_hOutMappedBuffer = NULL;
	}

	if (m_hDisconnectedMappedBuffer)
	{
		CloseHandle(m_hDisconnectedMappedBuffer);
		m_hDisconnectedMappedBuffer = NULL;
	}


	SetAlreadyConnected(false);
	
	return bRes;
}


bool CLocalSocket::accept(CLocalSocket * c_LocSocket)
{
	bool bRes = false;

	if (NULL == c_LocSocket)
		return bRes;

	if (IsServerSocket() && !IsAlreadyConnected())
	{
		bool bIsTerminating = false;
		c_LocSocket->SetIsServerSocket(true);

		if (!c_LocSocket->Initialize(m_strLocalSocketName))
			return bRes;

		if (!c_LocSocket->OpenConnection())
			return bRes;

		m_bIsNowAccepting = true;

		m_connectionEvent = c_LocSocket->m_connectionEvent;
		m_connectionEvent.SetTimeout(INFINITE);
		if (!m_connectionEvent.get_signal())
		{
			m_bIsNowAccepting = false;
			return bRes;
		}

		m_bIsNowAccepting = false;

		if (m_commonDataMutex.lock())
		{
			bIsTerminating = m_cCommonDataBuffer->bIsToStopAccepting;
			// and recover the common data back
			m_cCommonDataBuffer->bIsToStopAccepting = false;

			m_commonDataMutex.unlock();
		}

		if (bIsTerminating)
			return bRes;

		c_LocSocket->SetAlreadyConnected(true);

		if (m_commonDataMutex.lock())
		{
			m_cCommonDataBuffer->nCommonDataBuffer = ++m_nConnectionAmount;
			m_commonDataMutex.unlock();
		}

		bRes = true;
	}
	
	return bRes;
}

bool CLocalSocket::connect(char * szLocalSocketName, int nTimeout)
{
	bool bRes = false;

	if (NULL == szLocalSocketName)
		return false;

	if (0 == m_strLocalSocketName.compare(szLocalSocketName) && IsAlreadyConnected())
		return true;

	if (!IsServerSocket() && !IsAlreadyConnected())
	{
		bRes = Initialize(szLocalSocketName);
		if (!bRes)
			return false;
		bRes = OpenConnection();
		if (!bRes)
			return false;
		SetAlreadyConnected(true);
		m_connectionEvent.fire_signal();
	}

	return bRes;
}

int CLocalSocket::write(const char * szDataBlock, int nSizeOfBlock)
{
	if (NULL == szDataBlock)
		return FUNCTION_WRONG_PARAMETER;

	if (IsAlreadyConnected())
	{
		HANDLE hDataReadEvent, hDataWrittenEvent;

		s_PacketBuffer * pDataBuffer			= GetWriteDataBuffer();
		CMutex dataAccessMutex					= GetWriteDataAccessMutex();
		CEvent dataReadEvent					= GetWriteDataReadEvent();
		CEvent dataWrittenEvent					= GetWriteDataWrittenEvent();

		int nAmountOfPackets	 = nSizeOfBlock / PACKET_SIZE;
		int nLastUsedPacketBytes = nSizeOfBlock % PACKET_SIZE;

		if (0 != nLastUsedPacketBytes && 0 != nAmountOfPackets)
		{
			nAmountOfPackets++;
		}

		if (0 == nAmountOfPackets)
			nAmountOfPackets++;

		if (!dataAccessMutex.lock())
			return LOCAL_SOCKET_ERROR;
		
		pDataBuffer->nPacketsToBeSent  = nAmountOfPackets;
		pDataBuffer->nBytesToBeSent	= PACKET_SIZE;

		dataAccessMutex.unlock();

		int nAmountOfBytesToBeWritten = PACKET_SIZE;
		int nPacketOffset = 0;

			for (int nCur = 0; nCur < nAmountOfPackets; nCur++)
			{
				dataAccessMutex.lock();

				pDataBuffer->nCurrentNumber = nCur;

				if (nCur == nAmountOfPackets - 1)
				{
					(nLastUsedPacketBytes == 0) ? (nLastUsedPacketBytes = PACKET_SIZE) : (pDataBuffer->nBytesToBeSent = nLastUsedPacketBytes);
					nAmountOfBytesToBeWritten = nLastUsedPacketBytes;
				}

				memcpy(&pDataBuffer->cStorage[0], &szDataBlock[nPacketOffset], nAmountOfBytesToBeWritten);
				nPacketOffset += PACKET_SIZE;
				dataWrittenEvent.fire_signal();
				
				dataAccessMutex.unlock();


				if (!dataReadEvent.get_signal())
					return LOCAL_SOCKET_ERROR;

				if (CheckForDisconnection())
					return LOCAL_SOCKET_ERROR;
			}			

			return (nSizeOfBlock);
	}

	return LOCAL_SOCKET_ERROR;
}

int CLocalSocket::read(char * szDataBlock, int nSizeOfBlock)
{
	if (NULL == szDataBlock)
		return FUNCTION_WRONG_PARAMETER;

	if (IsAlreadyConnected())
	{
		HANDLE hDataReadEvent, hDataWrittenEvent;

		s_PacketBuffer * pDataBuffer			= GetReadDataBuffer();
		CMutex dataAccessMutex					= GetReadDataAccessMutex();
		CEvent dataReadEvent					= GetReadDataReadEvent();
		CEvent dataWrittenEvent					= GetReadDataWrittenEvent();

		dataWrittenEvent.SetTimeout(INFINITE);

		if (!dataWrittenEvent.get_signal())
			return LOCAL_SOCKET_ERROR;

		if (CheckForDisconnection())
			return LOCAL_SOCKET_ERROR;

		int nAmountOfIncomingPackets = 0;

		if (!dataAccessMutex.lock())
			return LOCAL_SOCKET_ERROR;

		nAmountOfIncomingPackets = pDataBuffer->nPacketsToBeSent;		

		int nPacketOffset = 0;

		if (nAmountOfIncomingPackets > 1)
		{
			int nLastUsedPacketBytes = 0;
			for(int nCur = 0; nCur < nAmountOfIncomingPackets; nCur++)
			{
				if (nCur > 0)
				{
					if (!dataWrittenEvent.get_signal())
						return LOCAL_SOCKET_ERROR;

					if (CheckForDisconnection())
						return LOCAL_SOCKET_ERROR;

					if (!dataAccessMutex.lock())
						return LOCAL_SOCKET_ERROR;
				}

				if (nCur == nAmountOfIncomingPackets - 1)
					nLastUsedPacketBytes = pDataBuffer->nBytesToBeSent;

				int nAmountOfIncomingBytes = pDataBuffer->nBytesToBeSent;
				if (nAmountOfIncomingBytes > nSizeOfBlock)
					// truncate the data
					memcpy(&szDataBlock[nPacketOffset], &pDataBuffer->cStorage[0], nSizeOfBlock);
				else
					memcpy(&szDataBlock[nPacketOffset], &pDataBuffer->cStorage[0], nAmountOfIncomingBytes);

				nPacketOffset += PACKET_SIZE;

				dataAccessMutex.unlock();

				dataReadEvent.fire_signal();
			}

			return (nLastUsedPacketBytes == 0 ? nAmountOfIncomingPackets * PACKET_SIZE : nAmountOfIncomingPackets * PACKET_SIZE - PACKET_SIZE + nLastUsedPacketBytes);

		}
		else
		{
			int nAmountOfIncomingBytes = pDataBuffer->nBytesToBeSent;
			if (nAmountOfIncomingBytes > nSizeOfBlock)
				// truncate the data
				memcpy(szDataBlock, &pDataBuffer->cStorage[0], nSizeOfBlock);
			else
				memcpy(szDataBlock, &pDataBuffer->cStorage[0], nAmountOfIncomingBytes);

			dataAccessMutex.unlock();

			dataReadEvent.fire_signal();

			return nSizeOfBlock;
		}
	}

	return LOCAL_SOCKET_ERROR;
}

bool CLocalSocket::cancel_accept(void)
{
	if (IsServerSocket() && !IsAlreadyConnected() && false != m_bIsNowAccepting)
	{
		if (m_commonDataMutex.lock())
		{
			m_cCommonDataBuffer->bIsToStopAccepting = true;

			m_commonDataMutex.unlock();
		}

		m_connectionEvent.fire_signal();

		return true;
	}

	return false;
}

bool CLocalSocket::Initialize(const std::string strSocketName)
{
	DWORD dwConnectionID = 0;
	bool bRes = false;

	if (strSocketName.empty())
		return false;

	m_strLocalSocketName = strSocketName;
	
	bRes = InitializeCommon(false);
	if (!bRes)
		return false;

	if (!m_commonDataMutex.lock())
		return false;
	
	m_nConnectionAmount = m_cCommonDataBuffer->nCommonDataBuffer;
	m_commonDataMutex.unlock();

	if (!m_nConnectionAmount)
		return false;
	dwConnectionID = m_nConnectionAmount;

	m_strLocalSocketName							= strSocketName + Long2Str(dwConnectionID);
	m_strLocalSocketName_In							= strSocketName + LOCAL_SOCKET_NAME_IN_POSTFIX		+ Long2Str(dwConnectionID);
	m_strLocalSocketName_Out						= strSocketName + LOCAL_SOCKET_NAME_OUT_POSTFIX		+ Long2Str(dwConnectionID);
	m_strInDataEventName							= strSocketName + IN_DATA_EVENT_NAME_POSTFIX		+ Long2Str(dwConnectionID);
	m_strOutDataEventName							= strSocketName + OUT_DATA_EVENT_NAME_POSTFIX		+ Long2Str(dwConnectionID);
	m_strConnectionEventName						= strSocketName + CONNECTION_EVENT_POSTFIX;
	m_strInDataReadEventName						= strSocketName + IN_DATA_READ_EVENT_NAME_POSTFIX	+ Long2Str(dwConnectionID);
	m_strOutDataReadEventName						= strSocketName + OUT_DATA_READ_EVENT_NAME_POSTFIX	+ Long2Str(dwConnectionID);
	m_strInDataMutexName							= strSocketName + IN_DATA_MUTEX_NAME				+ Long2Str(dwConnectionID);
	m_strOutDataMutexName							= strSocketName	+ OUT_DATA_MUTEX_NAME				+ Long2Str(dwConnectionID);
	m_strDisconnectedMutexName						= strSocketName + DISCONNECTED_MUTEX_NAME			+ Long2Str(dwConnectionID);
	m_strDisconnectedBufferName						= strSocketName + DISCONNECTED_BUFFER_NAME			+ Long2Str(dwConnectionID);

	return true;
}

bool CLocalSocket::OpenConnection(void)
{
	bool bRes = false;

	if (!m_connectionEvent.create(m_strConnectionEventName))
		return bRes;
	
	if (!m_inDataHasBeenReadEvent.create(m_strInDataReadEventName))
		return bRes;

	if (!m_outDataHasBeenReadEvent.create(m_strOutDataReadEventName))
		return bRes;

	if (IsServerSocket())
	{
		m_hInMappedBuffer			 =  CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,	m_nBufferSize, m_strLocalSocketName_In.c_str());
		m_hOutMappedBuffer			 =  CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,	m_nBufferSize, m_strLocalSocketName_Out.c_str());
		m_hDisconnectedMappedBuffer  =  CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,	sizeof(s_CommonData), m_strDisconnectedBufferName.c_str());
	}
	else
	{
		m_hInMappedBuffer			=  OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, m_strLocalSocketName_In.c_str());
		m_hOutMappedBuffer			=  OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, m_strLocalSocketName_Out.c_str());
		m_hDisconnectedMappedBuffer =  OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, m_strDisconnectedBufferName.c_str());
		
	}

	if (NULL								== m_hInMappedBuffer
		|| INVALID_HANDLE_VALUE				== m_hInMappedBuffer
		|| NULL								== m_hOutMappedBuffer
		|| INVALID_HANDLE_VALUE				== m_hOutMappedBuffer
		|| NULL								== m_hDisconnectedMappedBuffer
		|| INVALID_HANDLE_VALUE				== m_hDisconnectedMappedBuffer
		)
	{
		// either server or client is unavailable
		return bRes;
	}
	
	if (!m_inDataWrittenEvent.create(m_strInDataEventName))
		return bRes;

	if (!m_outDataWrittenEvent.create(m_strOutDataEventName))
		return bRes;

	if (!m_inDataMutex.create(m_strInDataMutexName))
		return bRes;

	if (!m_outDataMutex.create(m_strOutDataMutexName))
		return bRes;

	if (!m_disconnectedMutex.create(m_strDisconnectedMutexName))
		return bRes;

	// Map to the file
	m_cInDataBuffer		  = (s_PacketBuffer*)MapViewOfFile(m_hInMappedBuffer,	FILE_MAP_ALL_ACCESS, 0, 0, m_nBufferSize);
	m_cOutDataBuffer	  = (s_PacketBuffer*)MapViewOfFile(m_hOutMappedBuffer,	FILE_MAP_ALL_ACCESS, 0, 0, m_nBufferSize);
	m_cDisconnectedBuffer = (s_CommonData*)MapViewOfFile(m_hDisconnectedMappedBuffer,	FILE_MAP_ALL_ACCESS, 0, 0, sizeof(s_CommonData));

	if (NULL							== m_cInDataBuffer
			|| NULL						== m_cOutDataBuffer
			|| NULL						== m_cDisconnectedBuffer
			)
		return bRes;

	bRes = true;

	return bRes;
}

void CLocalSocket::SetIsServerSocket(bool bServer)
{	
	m_bIsServerSocket = bServer;
}

bool CLocalSocket::IsServerSocket()
{
	return m_bIsServerSocket;
}

void CLocalSocket::SetAlreadyConnected(bool bIsConnected)
{
	m_bAlreadyConnected = bIsConnected;
}
bool CLocalSocket::IsAlreadyConnected()
{
	return m_bAlreadyConnected;
}

bool CLocalSocket::InitializeCommon(bool bAccept)
{
	bool bRes = false;


	m_strCommonDataMutexName = m_strLocalSocketName + COMMON_DATA_MUTEX_POSTFIX;
	m_commonDataMutex.create(m_strCommonDataMutexName, bAccept);

	m_strCommonDataBufferName = m_strLocalSocketName + COMMON_DATA_BUFFER_POSTFIX;

	if (bAccept)
	{
		// first let's try to open the share memory - if it is successful then the server with the same name
		// is already running or someone is using the same name for mapfile
		m_hCommonMappedData  =  OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, m_strCommonDataBufferName.c_str());

		if (m_hCommonMappedData)
			return false;

		m_hCommonMappedData	 =  CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,	sizeof(s_CommonData), m_strCommonDataBufferName.c_str());
	}
	else
		m_hCommonMappedData  =  OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, m_strCommonDataBufferName.c_str());

	if (m_hCommonMappedData == NULL || m_hCommonMappedData == INVALID_HANDLE_VALUE)
		return false;

	m_cCommonDataBuffer  = (s_CommonData*)MapViewOfFile(m_hCommonMappedData,	FILE_MAP_ALL_ACCESS, 0, 0, sizeof(s_CommonData));

	if (m_cCommonDataBuffer != NULL)
		bRes = true;

	if (bAccept)
	{
		m_cCommonDataBuffer->nCommonDataBuffer = m_nConnectionAmount;
		m_commonDataMutex.unlock();
	}
	
	return bRes;
}

bool CLocalSocket::CheckForDisconnection()
{
	bool bDisconnected = true;

	if (!m_disconnectedMutex.lock())
		return bDisconnected;

	bDisconnected = m_cDisconnectedBuffer->bIsDisconnected;

	m_disconnectedMutex.unlock();

	return bDisconnected;
}



long CLocalSocket::Str2Long(std::string strInputValue)
{
	long nRes = 0;
	std::istringstream stream(strInputValue);
	stream >> nRes;
	return nRes;
}

std::string CLocalSocket::Long2Str(int nValue)
{
	std::string strRes;

	std::ostringstream ostr; //output string stream
	ostr << nValue; //use the string stream just like cout, except the stream prints not to stdout but to a string.
	strRes = ostr.str(); 

	return strRes;
}

s_PacketBuffer * CLocalSocket::GetReadDataBuffer()
{
	if (IsServerSocket())
		return m_cInDataBuffer;

	return m_cOutDataBuffer;
}

CMutex CLocalSocket::GetReadDataAccessMutex()
{
	if (IsServerSocket())
		return m_inDataMutex;

	return m_outDataMutex;
}

CEvent CLocalSocket::GetReadDataReadEvent()
{
	if (IsServerSocket())
		return m_inDataHasBeenReadEvent;

	return m_outDataHasBeenReadEvent;
}

CEvent CLocalSocket::GetReadDataWrittenEvent()
{
	if (IsServerSocket())
		return m_inDataWrittenEvent;

	return m_outDataWrittenEvent;
}

s_PacketBuffer * CLocalSocket::GetWriteDataBuffer()
{
	if (IsServerSocket())
		return m_cOutDataBuffer;

	return m_cInDataBuffer;
}

CMutex CLocalSocket::GetWriteDataAccessMutex()
{
	if (IsServerSocket())
		return m_outDataMutex;

	return m_inDataMutex;
}

CEvent CLocalSocket::GetWriteDataReadEvent()
{
	if (IsServerSocket())
		return m_outDataHasBeenReadEvent;

	return m_inDataHasBeenReadEvent;
}

CEvent CLocalSocket::GetWriteDataWrittenEvent()
{
	if (IsServerSocket())
		return m_outDataWrittenEvent;

	return m_inDataWrittenEvent;
}

bool CLocalSocket::disconnect(void)
{
	bool bRes = false;
	if (m_bAlreadyConnected)
	{
		bool bDisconnected = false;

		if (!m_commonDataMutex.lock())
			return bRes;
		m_cDisconnectedBuffer->bIsDisconnected = true;
		m_commonDataMutex.unlock();
		
		SetAlreadyConnected(false);

		m_inDataWrittenEvent.fire_signal();
		m_outDataWrittenEvent.fire_signal();
		m_inDataHasBeenReadEvent.fire_signal();
		m_outDataHasBeenReadEvent.fire_signal();
		bRes = true;
	}
	return bRes;
}

CMutex::CMutex()
{
	m_lWaitTimeout = SOCKET_TIMEOUT;
	m_hMutex = NULL;
	m_hInternalDataMutex = NULL;
}

CMutex::CMutex(std::string strMutexName, bool bIsOwner, unsigned long lTimeoutInMs)
{
	create(strMutexName, lTimeoutInMs, bIsOwner);
}

CMutex::CMutex(const CMutex & copyMutex)
{
	// we make a reference not creating the second mutex!
	// So the result object might be used as alternative variable
	// or as a handle copy for the same initial mutex
	m_strMutexName			= copyMutex.m_strMutexName;
	m_lWaitTimeout			= copyMutex.m_lWaitTimeout;
	m_hMutex				= copyMutex.m_hMutex;
	m_hInternalDataMutex	= copyMutex.m_hInternalDataMutex;
}


CMutex& CMutex::operator=(const CMutex& rhs)
{
	if (this != &rhs)
	{
		m_strMutexName			= rhs.m_strMutexName;
		m_lWaitTimeout			= rhs.m_lWaitTimeout;
		m_hMutex				= rhs.m_hMutex;
		m_hInternalDataMutex	= rhs.m_hInternalDataMutex;
	}

	return *this;
}


bool CMutex::create(std::string strMutexName, bool bIsOwner, unsigned long lTimeoutInMs)
{
	bool bRes = false;

	m_strMutexName	= strMutexName;
	m_hMutex		= CreateMutex(NULL, bIsOwner, m_strMutexName.c_str());

	if (NULL						==	m_hMutex
		||	INVALID_HANDLE_VALUE	==  m_hMutex
		)
		return bRes;

	m_lWaitTimeout	= lTimeoutInMs;

	bRes = true;

	return bRes;
}

void CMutex::SetName(std::string strMutexName)
{
	m_strMutexName = strMutexName;
}

std::string CMutex::GetName()
{
	return m_strMutexName;
}

void CMutex::SetTimeout(unsigned long lTimeoutInMs)
{
	m_lWaitTimeout = lTimeoutInMs;
}

unsigned long CMutex::GetTimeout()
{
	return m_lWaitTimeout;
}

bool CMutex::lock()
{
	bool bRes = false;

	if (NULL						==	m_hMutex
		||	INVALID_HANDLE_VALUE	==  m_hMutex
		)
		return bRes;

	DWORD dwErr = WaitForSingleObject(m_hMutex, m_lWaitTimeout);
	if (dwErr != WAIT_OBJECT_0)
	{
		dwErr = GetLastError();
		return bRes;
	}

	bRes = true;


	return bRes;
}

void CMutex::unlock()
{
	ReleaseMutex(m_hMutex);
}

CMutex::~CMutex()
{
	if (m_hMutex)
		unlock();
}


CEvent::CEvent()
{
	m_lWaitTimeout = SOCKET_TIMEOUT;
	m_hEvent = NULL;
}


CEvent::CEvent(std::string strEventName, unsigned long lTimeoutInMs)
{
	create(strEventName, lTimeoutInMs);
}

CEvent::CEvent(const CEvent & copyEvent)
{
	// we make a copy not creating the second event!
	// So the result object might be used as alternative variable
	// or as a handle copy for the same initial event
	m_strEventName	= copyEvent.m_strEventName;
	m_lWaitTimeout	= copyEvent.m_lWaitTimeout;
	m_hEvent		= copyEvent.m_hEvent;
}


CEvent& CEvent::operator=(const CEvent& rhs)
{
	if (this != &rhs)
	{
		m_strEventName	= rhs.m_strEventName;
		m_lWaitTimeout	= rhs.m_lWaitTimeout;
		m_hEvent		= rhs.m_hEvent;
	}

	return *this;
}


CEvent::~CEvent()
{
}

void CEvent::SetName(std::string strEventName)
{
	m_strEventName = strEventName;
}

std::string CEvent::GetName()
{
	return m_strEventName;
}

void CEvent::SetTimeout(unsigned long lTimeoutInMs)
{
	m_lWaitTimeout = lTimeoutInMs;
}
unsigned long CEvent::GetTimeout()
{
	return m_lWaitTimeout;
}

bool CEvent::create(std::string strEventName,  unsigned long lTimeoutInMs)
{
	bool bRes = false;

	m_strEventName	= strEventName;
	m_hEvent		= CreateEvent(NULL, FALSE, FALSE, m_strEventName.c_str());

	if (NULL						==	m_hEvent
		||	INVALID_HANDLE_VALUE	==  m_hEvent
		)
		return bRes;


	m_lWaitTimeout	= lTimeoutInMs;

	bRes = true;

	return bRes;
}


bool CEvent::fire_signal()
{
	bool bRes = SetEvent(m_hEvent);

	return bRes;
}

bool CEvent::get_signal()
{
	bool bRes = false;

	DWORD dwErr = WaitForSingleObject(m_hEvent, m_lWaitTimeout);
	if (dwErr != WAIT_OBJECT_0)
	{
		dwErr = GetLastError();
		return bRes;
	}

	bRes = true;

	return bRes;
}

void CEvent::release()
{
	CloseHandle(m_hEvent);
	m_hEvent	= NULL;
}

