#pragma once

class CRingBuffer
{
public:
    CRingBuffer(void);
    CRingBuffer(int BufferSize);
    ~CRingBuffer();

    int GetBufferSize(void);

    int GetUseSize(void); //���� ������� �뷮
    int GetFreeSize(void); //���� �뷮

    int DirectEnqueueSize(void); // �������� ���� ����
    int DirectDequeueSize(void); // �������� ���� ����

    int Enqueue(char* chpData, int Size);
    int Dequeue(char* chpDest, int dequeue_size);

    int	Peek(char* chpDest, int peek_size);

    int MoveRear(int Size);
    int MoveFront(int Size);
    void IfEndMoveToFront(char** pCurAddress);

    void ClearBuffer(void);

    char* GetFrontBufferPtr(void);
    char* GetRearBufferPtr(void);
    char* GetBeginPtr(void);

private:
    int size;
    int usableSize;
    char* begin;
    char* end;
    char* front;
    char* rear;
};