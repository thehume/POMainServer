#pragma once

#define MAX_THREAD 1000

struct st_Player;

class PlayerInfo
{
public:
	bool isValid = false;


private:
	st_Player* pPlayer = NULL;
};

struct st_Player
{
	INT64 SessionID;
	INT64 AccountNo;
	PlayerInfo* playerInfo;
};

struct st_ThreadMoveInfo
{
	short srcThread;
	short desThread;
	st_Player* pPlayer;
};


class CThreadManager;
class CThreadHandler;

class CParentThread
{


public:
	CParentThread(DWORD TimeInterval, short ThreadNum) : TimeInterval(TimeInterval), ThreadNumber(ThreadNum) { }

	DWORD EnterUserTPS;
	DWORD Frame;
	void UpdateTPS()
	{
		EnterUserTPS = 0;
		Frame = 0;
	}

	DWORD getPlayerSize()
	{
		return this->PlayerList.size();
	}

	DWORD getFrame()
	{
		return this->Frame;
	}

	DWORD getEnterUserTPS()
	{
		return this->EnterUserTPS;
	}

	DWORD getEnterQueueSize()
	{
		return this->enterQueue.size();
	}

	static DWORD WINAPI ThreadFunction(CParentThread* Instance); // ������ �Լ�. 
	//void JobProcess(void); // ���罺���忡 �Ҽӵ� �÷��̾���� ���� ��ȸ�ϸ鼭 ����
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket) = 0;
	virtual void Update(void) = 0;
	virtual void onThreadJoin(INT64 sessionID, INT64 accountNo, PlayerInfo* pPlayerInfo) {};

	void setPlayerInfo(st_Player* pPlayer, PlayerInfo* playerInfo) // Player ��ü�� Info�� �ٿ��ش�.
	{
		pPlayer->playerInfo = playerInfo;
	}
	void setAccountNo(st_Player* pPlayer, INT64 AccountNo)// Player��ü�� accountNo ���� �ٲ��ش�
	{
		pPlayer->AccountNo = AccountNo;
	}


	void attachServer(CNetServer* pNetServer)
	{
		this->pServer = pNetServer;
	}

	void sendPacket(INT64 SessionID, CPacket* pPacket)
	{
		pServer->sendPacket(SessionID, pPacket);
	}


	


	static DWORD PlayerPoolSize()
	{
		return PlayerPool.getUseSize();
	}


	friend class CThreadManager;

protected:

	queue<st_ThreadMoveInfo> enterQueue; //������ ���� ��� ť
	SRWLOCK enterQueueLock;
	list<st_Player*> PlayerList; // �÷��̾� ����Ʈ
	CNetServer* pServer;
	volatile BOOL shutdown;
	short ThreadNumber;

	static CMemoryPoolBucket<st_Player> PlayerPool;
	CThreadManager* pThreadManager;
	CThreadHandler* pThreadHandler;
	DWORD TimeInterval;
};

class CLoginThread : public CParentThread
{
public:
	CLoginThread(DWORD TimeInterval, short ThreadNum) : CParentThread(TimeInterval, ThreadNum) {};
	static DWORD WINAPI LoginThreadFunction(CLoginThread* Instance);
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket) = 0;
	virtual void Update(void) = 0;
	
	void onLoginSucess(INT64 sessionID, INT64 AccountNo);
};

class CThreadManager
{
public:
	void registThread(short threadNo, CParentThread* pThread, bool isLoginThread=false);
	bool MovePlayer(short srcThreadNo, short DesThreadNo, st_Player* pPlayer);
	void attachServerInstance(CNetServer* pNetServer);
	void attachHandler(CThreadHandler* pThreadHandler)
	{
		this->pThreadHandler = pThreadHandler;
	}
	void start();

	CThreadHandler* pThreadHandler;

	DWORD LoginTPS;
	DWORD ThreadMoveTPS;
	DWORD LoginThread_JobTPS;
	DWORD GameThread_JobTPS;

	void updateMonitor()
	{
		LoginTPS = 0;
		ThreadMoveTPS = 0;
		LoginThread_JobTPS = 0;
		GameThread_JobTPS = 0;
	}


private:
	CNetServer* pServer;
	unordered_map<short, CLoginThread*> LoginThreadList;
	unordered_map<short, CParentThread*> GameThreadList;

	HANDLE ThreadHANDLE[MAX_THREAD];
	short Threadindex = 0;
};

class CThreadHandler
{
public:
	virtual PlayerInfo* onPlayerJoin(INT64 sessionID) = 0;
	virtual void onPlayerMove(INT64 accountNo, short srcThread, short desThread) = 0;
	virtual void onPlayerLeave(INT64 accountNo, PlayerInfo* pPlayerInfo) = 0;
};