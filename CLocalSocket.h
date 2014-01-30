#pragma once
#include "IPCDataBuffer.h"
#include <string>

#define CANNOT_COPY(class)\
	private:\
		class(const class&);\
		void operator= (const class&)

const unsigned int SOCKET_TIMEOUT = 5000;

class CMutex
{
public:
	CMutex();
	CMutex(std::string strMutexName, bool bIsOwner = false, unsigned long lTimeoutInMs = SOCKET_TIMEOUT);
	CMutex(const CMutex & copyMutex);
	CMutex& operator=(const CMutex& rhs);
	~CMutex();

	void SetName(std::string strMutexName);
	std::string GetName();
	void SetTimeout(unsigned long lTimeoutInMs);
	unsigned long GetTimeout();

	bool create(std::string strMutexName,  bool bIsOwner = false, unsigned long lTimeoutInMs = SOCKET_TIMEOUT);
	bool lock();
	void unlock();


protected:
	HANDLE m_hMutex;
	HANDLE m_hInternalDataMutex;
	std::string m_strMutexName;

	unsigned long m_lWaitTimeout;
};


class CEvent
{
public:
	CEvent();
	CEvent(std::string strEventName, unsigned long lTimeoutInMs);
	CEvent(const CEvent & eventMutex);
	CEvent& operator=(const CEvent& rhs);
	~CEvent();

	void SetName(std::string strEventName);
	std::string GetName();
	void SetTimeout(unsigned long lTimeoutInMs);
	unsigned long GetTimeout();

	bool create(std::string strEventName,  unsigned long lTimeoutInMs = SOCKET_TIMEOUT);
	bool fire_signal();
	bool get_signal();
	void release();


protected:
	HANDLE m_hEvent;
	std::string m_strEventName;

	unsigned long m_lWaitTimeout;
};



class CLocalSocket
{
	CANNOT_COPY(CLocalSocket);

public:
	CLocalSocket(void);
	virtual ~CLocalSocket(void);
	bool create_local(const char * szLocalSocketName);
	bool accept(CLocalSocket * c_LocSocket);
	bool connect(char * szLocalSocketName, int nTimeout);
	int write(const char * szDataBlock, int nSizeOfBlock);
	int read(char * szDataBlock, int nSizeOfBlock);
	bool cancel_accept(void);
	bool disconnect(void);

	enum eErrorReturnValues
	{
		LOCAL_SOCKET_ERROR			= -1,
		FUNCTION_WRONG_PARAMETER	= 0
	};

protected:

	bool Initialize(const std::string strSocketName);
	bool OpenConnection(void);
	void SetIsServerSocket(bool bIsServer);
	bool IsServerSocket();
	void SetAlreadyConnected(bool bIsConnected);
	bool IsAlreadyConnected();
	bool InitializeCommon(bool bAccept);
	bool CheckForDisconnection();
	s_PacketBuffer * GetWriteDataBuffer();
	CMutex GetWriteDataAccessMutex();
	CEvent GetWriteDataReadEvent();
	CEvent GetWriteDataWrittenEvent();
	s_PacketBuffer * GetReadDataBuffer();
	CMutex GetReadDataAccessMutex();
	CEvent GetReadDataReadEvent();
	CEvent GetReadDataWrittenEvent();
	bool close(void);

	const std::string COMMON_DATA_MUTEX_POSTFIX;
	const std::string COMMON_DATA_BUFFER_POSTFIX;
	const std::string LOCAL_SOCKET_NAME_IN_POSTFIX;
	const std::string LOCAL_SOCKET_NAME_OUT_POSTFIX;
	const std::string IN_DATA_EVENT_NAME_POSTFIX;
	const std::string OUT_DATA_EVENT_NAME_POSTFIX;
	const std::string CONNECTION_EVENT_POSTFIX;
	const std::string IN_DATA_READ_EVENT_NAME_POSTFIX;
	const std::string OUT_DATA_READ_EVENT_NAME_POSTFIX;
	const std::string IN_DATA_MUTEX_NAME;
	const std::string OUT_DATA_MUTEX_NAME;
	const std::string DISCONNECTED_MUTEX_NAME;
	const std::string DISCONNECTED_BUFFER_NAME;


	s_PacketBuffer	  * m_cInDataBuffer;
	s_PacketBuffer	  * m_cOutDataBuffer;
	s_CommonData  * m_cCommonDataBuffer;
	s_CommonData  * m_cDisconnectedBuffer;
	std::string m_strLocalSocketName;
	std::string m_strLocalSocketName_In;
	std::string m_strLocalSocketName_Out;
	std::string m_strDisconnectedBufferName;
	std::string m_strDisconnectedMutexName;
	std::string m_strCommonDataMutexName;
	std::string m_strCommonDataBufferName;
	std::string m_strInDataEventName;
	std::string m_strOutDataEventName;
	std::string m_strConnectionEventName;
	std::string m_strInDataMutexName;
	std::string m_strOutDataMutexName;
	std::string m_strInDataReadEventName;
	std::string m_strOutDataReadEventName;

	CMutex m_commonDataMutex;
	CMutex m_disconnectedMutex;
	CMutex m_inDataMutex;
	CMutex m_outDataMutex;

	CEvent m_connectionEvent;
	CEvent m_inDataWrittenEvent;
	CEvent m_outDataWrittenEvent;
	CEvent m_inDataHasBeenReadEvent;
	CEvent m_outDataHasBeenReadEvent;

	HANDLE m_hCommonMappedData;
	HANDLE m_hInMappedBuffer;
	HANDLE m_hOutMappedBuffer;
	HANDLE m_hDisconnectedMappedBuffer;

	bool m_bIsServerSocket;
	bool m_bAlreadyConnected;
	bool m_bIsNowAccepting;
	int  m_nCommonDataConnectionId;
	int  m_nConnectionAmount;
	int  m_nPacketsWrittenAmount;

	int  m_nBufferSize;

private:
	long Str2Long(std::string strInputValue);
	std::string Long2Str(int nValue);	
};
