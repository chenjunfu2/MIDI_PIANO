#pragma once
#include <atomic>

template<typename T, size_t S>
class Thread_Queue
{
private:
	std::atomic<unsigned long> ulBeg;
	std::atomic<unsigned long> ulEnd;
	T tData[S];
public:
	Thread_Queue(void) :ulBeg(0), ulEnd(0), tData{}
	{}
	~Thread_Queue(void) = default;

	bool push(T&& tPush)//尾部插入
	{
		unsigned long ulCurr = ulEnd.load();
		unsigned long ulNext = (ulCurr + 1) % S;
		if (ulNext == ulBeg.load())
		{
			return false;
		}

		tData[ulCurr] = std::move(tPush);
		ulEnd.store(ulNext);//更新end

		return true;
	}

	bool pop(T& tPop)//前面弹出
	{
		unsigned long ulCurr = ulBeg.load();
		unsigned long ulNext = (ulCurr + 1) % S;
		if (ulCurr == ulEnd.load())
		{
			return false;
		}


		tPop = std::move(tData[ulCurr]);
		ulBeg.store(ulNext);

		return true;
	}

	void Clear(void)
	{
		unsigned long ulBegTemp, ulEndTemp;
		do
		{
			ulBegTemp = ulBeg.load();
			ulEndTemp = ulEnd.load();

		} while (!ulBeg.compare_exchange_weak(ulBegTemp, ulEndTemp) || ulEndTemp != ulEnd.load());
	}

	bool Empty(void)
	{
		return ulBeg.load() == ulEnd.load();
	}
};
