#pragma once

//DB Connector Class
//외부에서 TLS로 인덱스 1회 할당 및 공용 락 받아서 호출해야함
//스레드 최초 생성시, 스레드 지역에 선언
//함수내부에선 getConnectorInstance로 객체 받아서 쓴다. 할당받지 않았을시 예외처리


class CDBConnector
{
public:

	enum en_DB_CONNECTOR
	{
		eQUERY_MAX_LEN = 2048

	};

	CDBConnector(const WCHAR* DBIP, const WCHAR* User, const WCHAR* Password, const WCHAR* DBName, int DBPort, int TLSIndex, SRWLOCK& InitLock)
	{
		wcscpy_s(this->DBIP, DBIP);
		wcscpy_s(this->DBUser, User);
		wcscpy_s(this->DBPassword, Password);
		wcscpy_s(this->DBName, DBName);
		this->DBPort = DBPort;
		maxQueryTime = 0;

		TLS_DBConnectorIndex = TLSIndex;

		AcquireSRWLockExclusive(&InitLock);
		mysql_init(&MySQL_conn);
		ReleaseSRWLockExclusive(&InitLock);

		TlsSetValue(TLS_DBConnectorIndex, this);

		bool reconnect = true;
		mysql_options(&MySQL_conn, MYSQL_OPT_RECONNECT, &reconnect);
	}

	CDBConnector(const WCHAR* DBIP, const WCHAR* User, const WCHAR* Password, const WCHAR* DBName, int DBPort)
	{
		wcscpy_s(this->DBIP, DBIP);
		wcscpy_s(this->DBUser, User);
		wcscpy_s(this->DBPassword, Password);
		wcscpy_s(this->DBName, DBName);
		this->DBPort = DBPort;
		maxQueryTime = 0;

		mysql_init(&MySQL_conn);

		bool reconnect = true;
		mysql_options(&MySQL_conn, MYSQL_OPT_RECONNECT, &reconnect);
	}

	virtual		~CDBConnector() {};

	//////////////////////////////////////////////////////////////////////
	// MySQL DB 연결
	//////////////////////////////////////////////////////////////////////
	bool		Connect(void)
	{
		char m_DBIP[16];
		char m_DBUser[64];
		char m_DBPassword[64];
		char m_DBName[64];
		int ret1 = WideCharToMultiByte(CP_ACP, 0, DBIP, -1, m_DBIP, 16, NULL, NULL);
		int ret2 = WideCharToMultiByte(CP_ACP, 0, DBUser, -1, m_DBUser, 64, NULL, NULL);
		int ret3 = WideCharToMultiByte(CP_ACP, 0, DBPassword, -1, m_DBPassword, 64, NULL, NULL);
		int ret4 = WideCharToMultiByte(CP_ACP, 0, DBName, -1, m_DBName, 64, NULL, NULL);
		pMySQL_conn = mysql_real_connect(&MySQL_conn, m_DBIP, m_DBUser, m_DBPassword, m_DBName, DBPort, (char*)NULL, 0);
		if (pMySQL_conn == NULL)
		{
			SaveLastError();
			return false;
		}
		return true;
	}

	bool		Connect(SRWLOCK& InitLock)
	{
		char m_DBIP[16];
		char m_DBUser[64];
		char m_DBPassword[64];
		char m_DBName[64];
		int ret1 = WideCharToMultiByte(CP_ACP, 0, DBIP, -1, m_DBIP, 16, NULL, NULL);
		int ret2 = WideCharToMultiByte(CP_ACP, 0, DBUser, -1, m_DBUser, 64, NULL, NULL);
		int ret3 = WideCharToMultiByte(CP_ACP, 0, DBPassword, -1, m_DBPassword, 64, NULL, NULL);
		int ret4 = WideCharToMultiByte(CP_ACP, 0, DBName, -1, m_DBName, 64, NULL, NULL);
		AcquireSRWLockExclusive(&InitLock);
		pMySQL_conn = mysql_real_connect(&MySQL_conn, m_DBIP, m_DBUser, m_DBPassword, m_DBName, DBPort, (char*)NULL, 0);
		ReleaseSRWLockExclusive(&InitLock);
		if (pMySQL_conn == NULL)
		{
			SaveLastError();
			return false;
		}
		return true;
	}

	//////////////////////////////////////////////////////////////////////
	// MySQL DB 끊기
	//////////////////////////////////////////////////////////////////////
	bool		Disconnect(void)
	{
		mysql_close(pMySQL_conn);
		return true;
	}


	//////////////////////////////////////////////////////////////////////
	// 쿼리 날리고 결과셋 임시 보관
	//
	//////////////////////////////////////////////////////////////////////
	bool		sendQuery(LPCWSTR szStringFormat, ...)
	{
		va_list argList;
		va_start(argList, szStringFormat);
		StringCchVPrintf(Query, eQUERY_MAX_LEN, szStringFormat, argList);
		WideCharToMultiByte(CP_UTF8, 0, Query, eQUERY_MAX_LEN, QueryUTF8, eQUERY_MAX_LEN, NULL, NULL);
		ULONGLONG startTime = GetTickCount64();
		int query_stat = mysql_query(pMySQL_conn, QueryUTF8);
		if (query_stat != 0)
		{
			SaveLastError();
			return false;
		}
		ULONGLONG timePeriod = GetTickCount64() - startTime;
		//시간이 오래걸리면 로그남기기
		maxQueryTime = max(maxQueryTime, timePeriod);
		return true;
	}
	

	bool		sendQuery_Save(LPCWSTR szStringFormat, ...)
	{
		va_list argList;
		va_start(argList, szStringFormat);
		StringCchVPrintf(Query, eQUERY_MAX_LEN, szStringFormat, argList);
		WideCharToMultiByte(CP_UTF8, 0, Query, eQUERY_MAX_LEN, QueryUTF8, eQUERY_MAX_LEN, NULL, NULL);
		ULONGLONG startTime = GetTickCount64();
		int query_stat = mysql_query(pMySQL_conn, QueryUTF8);
		if (query_stat != 0)
		{
			SaveLastError();
			return false;
		}

		ULONGLONG timePeriod = GetTickCount64() - startTime;
		//시간이 오래걸리면 로그남기기
		maxQueryTime = max(maxQueryTime, timePeriod);

		pSqlResult = mysql_store_result(pMySQL_conn);
		return true;
	}
	
	void Ping(void)
	{
		mysql_ping(&MySQL_conn);
	}

	
	// DBWriter 스레드의 Save 쿼리 전용
	//쿼리 가변인자 스트링 완성
	//쿼리 UTF - 16->UTF - 8
	//에러 발생시 로그(쿼리문 전체, 에러코드, 에러메시지)
	//간단한 프로파일링 쿼리 실행시간 측정 ms->시간 초과시 로그



	//////////////////////////////////////////////////////////////////////
	// 쿼리를 날린 뒤에 결과 뽑아오기.
	//
	// 결과가 없다면 NULL 리턴.
	//////////////////////////////////////////////////////////////////////
	MYSQL_ROW	FetchRow(void)
	{
		return mysql_fetch_row(pSqlResult);
	}

	//////////////////////////////////////////////////////////////////////
	// 한 쿼리에 대한 결과 모두 사용 후 정리.
	//////////////////////////////////////////////////////////////////////
	void		FreeResult(void)
	{
		mysql_free_result(pSqlResult);
	}



	//////////////////////////////////////////////////////////////////////
	// Error 얻기.한 쿼리에 대한 결과 모두 사용 후 정리.
	//////////////////////////////////////////////////////////////////////
	int			GetLastError(void) { return LastError; };
	WCHAR* GetLastErrorMsg(void) { return LastErrorMsg; }


	//////////////////////////////////////////////////////////////////////
	// Connector Instance를 컨텐츠쪽에서 받아서 쓰는 static 멤버함수
	//////////////////////////////////////////////////////////////////////
	static CDBConnector* getConnectorInstance(int TLSindex)
	{
		return (CDBConnector*)TlsGetValue(TLSindex);
	}

private:

	//////////////////////////////////////////////////////////////////////
	// mysql 의 LastError 를 맴버변수로 저장한다.
	//////////////////////////////////////////////////////////////////////
	void		SaveLastError(void)
	{
		strcpy_s(m_LastErrorMsg, mysql_error(&MySQL_conn));
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, m_LastErrorMsg, 128, LastErrorMsg, 128);
	}

private:



	//-------------------------------------------------------------
	// MySQL 연결객체 본체
	//-------------------------------------------------------------
	MYSQL		MySQL_conn;

	//-------------------------------------------------------------
	// MySQL 연결객체 포인터. 위 변수의 포인터임. 
	// 이 포인터의 null 여부로 연결상태 확인.
	//-------------------------------------------------------------
	MYSQL* pMySQL_conn;

	//-------------------------------------------------------------
	// 쿼리를 날린 뒤 Result 저장소.
	//
	//-------------------------------------------------------------
	MYSQL_RES*  pSqlResult;

	WCHAR		DBIP[16];
	WCHAR		DBUser[64];
	WCHAR		DBPassword[64];
	WCHAR		DBName[64];
	int			DBPort;


	WCHAR		Query[eQUERY_MAX_LEN];
	char		QueryUTF8[eQUERY_MAX_LEN];

	int			LastError;
	WCHAR		LastErrorMsg[128];
	char m_LastErrorMsg[128];

	ULONGLONG maxQueryTime;
	int TLS_DBConnectorIndex;
};