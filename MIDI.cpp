#include "MIDI.hpp"
#include "Thread_Queue.hpp"
#include <stdio.h>


/*
0x30~0x39:0~9
0x3A~0x40:Undef
0x41~0x5A:A~Z
*/
#define VK_NUM_BEG 0x30
#define VK_NUM_END (0x39+1)

#define VK_ALP_BEG 0x41
#define VK_ALP_END (0x5A+1)

#define Key_MAP_SIZE 256

UCHAR ucMapKeyToPitch[Key_MAP_SIZE] = {0};
bool bIsKeyDown[Key_MAP_SIZE] = {0};


void InitucMapKeyToPitch(void)
{
	constexpr const UCHAR ucPitch[4][7] =
	{
		{C2,D2,E2,F2,G2,A2,B2,},
		{C3,D3,E3,F3,G3,A3,B3,},
		{C4,D4,E4,F4,G4,A4,B4,},
		{C5,D5,E5,F5,G5,A5,B5,},
	};
	
	constexpr const UCHAR ucKey[4][8] =
	{
		"1234567",
		"QWERTYU",
		"ASDFGHJ",
		"ZXCVBNM",
	};

	constexpr const long lAddMap[4] =
	{
		{VK_NUM_BEG - (long)'0'},
		{VK_ALP_BEG - (long)'A'},
		{VK_ALP_BEG - (long)'A'},
		{VK_ALP_BEG - (long)'A'},
	};

	for (DWORD line = 0; line < 4; ++line)
	{
		for (DWORD i = 0; i < 7; ++i)
		{
			ucMapKeyToPitch[ucKey[line][i] + lAddMap[line]] = ucPitch[line][i];
		}
	}

}

//全局Midi输出句柄
HMIDIOUT hMidiOut = NULL;
DWORD dwLastTime = 0;

struct Note
{
	DWORD dwMidiData;
	DWORD dwDeltaTime;
};

//线程队列
Thread_Queue<Note, 512> queue;

//全局钩子回调函数
LRESULT CALLBACK KeyboardHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION)// 按键事件发生时的处理逻辑
	{
		KBDLLHOOKSTRUCT *pKeyInfo = (KBDLLHOOKSTRUCT *)lParam;
		DWORD vkCode = pKeyInfo->vkCode, dwNowTime = pKeyInfo->time;

		if (wParam == WM_KEYDOWN && !bIsKeyDown[vkCode])// 按键按下
		{
			bIsKeyDown[vkCode] = true;
			DWORD dwMidiData = NoteOn(0, ucMapKeyToPitch[vkCode] , 0xFF);
			midiOutShortMsg(hMidiOut, dwMidiData);

			queue.push(Note{dwMidiData,dwNowTime - dwLastTime});
			dwLastTime = dwNowTime;
		}
		else if (wParam == WM_KEYUP && bIsKeyDown[vkCode])// 按键弹起
		{
			bIsKeyDown[vkCode] = false;
			DWORD dwMidiData = NoteOff(0, ucMapKeyToPitch[vkCode], 0xFF);
			midiOutShortMsg(hMidiOut, dwMidiData);

			queue.push(Note{dwMidiData,dwNowTime - dwLastTime});
			dwLastTime = dwNowTime;
		}
	}

	//return TRUE;//丢弃消息不传递

	// 将事件传递给下一个钩子或默认处理
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

//文件写入线程
DWORD WINAPI FileWriteProc(LPVOID)
{
	//获取时间戳
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);

	//打开对应写入文件
	char cFileName[32];
	sprintf(cFileName, "MidiNote_%X%X.bin", ft.dwHighDateTime, ft.dwLowDateTime);
	FILE *fWrite = fopen(cFileName, "wb");
	if (fWrite == NULL)
	{
		return -1;
	}

	//循环写入
	Note nCurNote;
	while (true)
	{
		if (!queue.pop(nCurNote))
		{
			Sleep(0);
			continue;
		}

		fwrite(&nCurNote, sizeof(nCurNote), 1, fWrite);
		fflush(fWrite);
	}

	fclose(fWrite);
	return 0;
}

//文件读取线程
DWORD WINAPI FileReadProc(LPVOID pFileName)
{
	//打开对应输入文件
	FILE *fRead = fopen((char *)pFileName, "rb");
	if (fRead == NULL)
	{
		return -1;
	}

	//循环读取
	Note nCurNote;
	while (!feof(fRead))
	{
		fread(&nCurNote, sizeof(nCurNote), 1, fRead);
		while (!queue.push(std::move(nCurNote)))
		{
			Sleep(0);
			continue;
		}
	}

	fclose(fRead);
	return 0;
}



int main(int argc, char *argv[])
{
	//初始化midi输出句柄
	MMRESULT mRet = midiOutOpen(&hMidiOut, 0, NULL, NULL, CALLBACK_NULL);
	if (mRet != MMSYSERR_NOERROR)
	{
		return -1;
	}

	//创建文件读或写线程
	HANDLE hThread;

	if (argc == 2)//播放模式
	{
		hThread = CreateThread(NULL, 0, FileReadProc, argv[1], 0, NULL);
		if (hThread == NULL)
		{
			goto Ret0;
		}

		Note nPlayNote;
		while (true)
		{
			if (!queue.pop(nPlayNote))
			{
				if (WaitForSingleObject(hThread, 0) == WAIT_OBJECT_0)//线程已结束，文件打开失败或已经到文件尾
				{
					break;
				}
				Sleep(0);
				continue;
			}

			if (nPlayNote.dwDeltaTime != 0)
			{
				Sleep(nPlayNote.dwDeltaTime);
			}
			
			midiOutShortMsg(hMidiOut, nPlayNote.dwMidiData);
		}

		//等待最后一个音差不多结束
		Sleep(1000);
	Ret0:
		midiOutClose(hMidiOut);//关闭midi输出句柄
		CloseHandle(hThread);//关闭线程句柄
		return 1;
	}

	hThread = CreateThread(NULL, 0, FileWriteProc, NULL, CREATE_SUSPENDED, NULL);
	if (hThread == NULL)
	{
		goto Ret1;
	}

	//初始化映射数组
	InitucMapKeyToPitch();

	//初始化开始时间
	dwLastTime = GetTickCount();

	//安装全局键盘钩子
	HHOOK hKeyboardHook;
	hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookCallback, NULL, 0);
	if (hKeyboardHook == NULL)
	{
		// 处理钩子安装失败的情况...
		goto Ret1;
	}

	//开始运行线程
	ResumeThread(hThread);
	//如果提前退出则代表文件打开失败
	if (WaitForSingleObject(hThread, 0) == WAIT_OBJECT_0)
	{
		return -1;
	}

	//控制台没有消息循环，主动模拟消息循环，保持钩子运行
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

Ret1:
	UnhookWindowsHookEx(hKeyboardHook);//卸载钩子
	midiOutClose(hMidiOut);//关闭midi输出句柄
	CloseHandle(hThread);//关闭线程句柄
	return 0;
}