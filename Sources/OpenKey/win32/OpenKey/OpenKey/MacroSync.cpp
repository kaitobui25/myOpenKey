/*----------------------------------------------------------
OpenKey - The Cross platform Open source Vietnamese Keyboard application.

Copyright (C) 2019 Mai Vu Tuyen
Contact: maivutuyen.91@gmail.com
Github: https://github.com/tuyenvm/OpenKey
Fanpage: https://www.facebook.com/OpenKeyVN

This file is belong to the OpenKey project, Win32 version
which is released under GPL license.
You can fork, modify, improve this program. If you
redistribute your new version, it MUST be open source.
-----------------------------------------------------------*/
#include "stdafx.h"
#include "MacroSync.h"
#include "../../../engine/Macro.h"

namespace {
	const wchar_t* SYNC_FILE_PATH = L"C:\\Mana Design Work\\07_Kinh nghiem\\linh tinh\\01_openkey\\OpenKeyMacroSync.bin";
	const wchar_t* SYNC_TEMP_FILE_PATH = L"C:\\Mana Design Work\\07_Kinh nghiem\\linh tinh\\01_openkey\\OpenKeyMacroSync.bin.tmp";
	const DWORD MAX_SYNC_FILE_SIZE = 16 * 1024 * 1024;
	const DWORD SYNC_POLL_INTERVAL_MS = 2000;

	vector<Byte> lastSyncedData;
	vector<Byte> pendingLocalData;
	vector<Byte> pendingRemoteData;
	CRITICAL_SECTION syncLock;
	HANDLE syncWakeEvent = NULL;
	HANDLE syncThread = NULL;
	bool syncInitialized = false;
	bool stopping = false;
	bool hasPendingLocalData = false;
	bool hasPendingRemoteData = false;
	unsigned long long pendingLocalVersion = 0;

	bool isValidUtf8(const Byte* data, size_t size) {
		if (size == 0) {
			return true;
		}
		return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)data, (int)size, NULL, 0) > 0;
	}

	bool syncFileMissing() {
		DWORD attributes = GetFileAttributes(SYNC_FILE_PATH);
		return attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
	}

	bool readSyncFile(vector<Byte>& outData) {
		outData.clear();

		HANDLE file = CreateFile(
			SYNC_FILE_PATH,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		LARGE_INTEGER size;
		if (!GetFileSizeEx(file, &size) || size.QuadPart < 2 || size.QuadPart > MAX_SYNC_FILE_SIZE) {
			CloseHandle(file);
			return false;
		}

		outData.resize((size_t)size.QuadPart);
		DWORD bytesRead = 0;
		BOOL readOk = ReadFile(file, outData.data(), (DWORD)outData.size(), &bytesRead, NULL);
		CloseHandle(file);

		if (!readOk || bytesRead != (DWORD)outData.size()) {
			outData.clear();
			return false;
		}
		return true;
	}

	bool validateMacroData(const vector<Byte>& data) {
		if (data.size() < 2) {
			return false;
		}

		size_t cursor = 0;
		Uint16 macroCount = (Uint16)data[cursor] | ((Uint16)data[cursor + 1] << 8);
		cursor += 2;

		for (Uint16 i = 0; i < macroCount; ++i) {
			if (cursor >= data.size()) {
				return false;
			}

			Uint8 macroTextSize = data[cursor++];
			if (macroTextSize == 0 || cursor + macroTextSize > data.size()) {
				return false;
			}
			if (!isValidUtf8(data.data() + cursor, macroTextSize)) {
				return false;
			}
			cursor += macroTextSize;

			if (cursor + 2 > data.size()) {
				return false;
			}
			Uint16 macroContentSize = (Uint16)data[cursor] | ((Uint16)data[cursor + 1] << 8);
			cursor += 2;
			if (cursor + macroContentSize > data.size()) {
				return false;
			}
			if (!isValidUtf8(data.data() + cursor, macroContentSize)) {
				return false;
			}
			cursor += macroContentSize;
		}

		return cursor == data.size();
	}

	bool writeSyncFile(const vector<Byte>& data) {
		HANDLE file = CreateFile(
			SYNC_TEMP_FILE_PATH,
			GENERIC_WRITE,
			0,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (file == INVALID_HANDLE_VALUE) {
			return false;
		}

		DWORD bytesWritten = 0;
		BOOL writeOk = WriteFile(file, data.data(), (DWORD)data.size(), &bytesWritten, NULL);
		if (writeOk && bytesWritten == (DWORD)data.size()) {
			writeOk = FlushFileBuffers(file);
		}
		CloseHandle(file);

		if (!writeOk || bytesWritten != (DWORD)data.size()) {
			DeleteFile(SYNC_TEMP_FILE_PATH);
			return false;
		}

		if (!MoveFileEx(SYNC_TEMP_FILE_PATH, SYNC_FILE_PATH, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			DeleteFile(SYNC_TEMP_FILE_PATH);
			return false;
		}
		return true;
	}

	void applyMacroData(const vector<Byte>& data) {
		initMacroMap(data.data(), (int)data.size());
		OpenKeyHelper::setRegBinary(_T("macroData"), data.data(), (int)data.size());
	}

	DWORD WINAPI syncThreadProc(LPVOID) {
		while (true) {
			WaitForSingleObject(syncWakeEvent, SYNC_POLL_INTERVAL_MS);

			vector<Byte> localData;
			unsigned long long localVersion = 0;
			bool shouldWriteLocal = false;

			EnterCriticalSection(&syncLock);
			if (stopping) {
				LeaveCriticalSection(&syncLock);
				return 0;
			}
			if (hasPendingLocalData) {
				localData = pendingLocalData;
				localVersion = pendingLocalVersion;
				shouldWriteLocal = true;
			}
			LeaveCriticalSection(&syncLock);

			if (shouldWriteLocal) {
				if (writeSyncFile(localData)) {
					EnterCriticalSection(&syncLock);
					lastSyncedData = localData;
					if (hasPendingLocalData && pendingLocalVersion == localVersion) {
						hasPendingLocalData = false;
						pendingLocalData.clear();
					}
					LeaveCriticalSection(&syncLock);
				}
				continue;
			}

			vector<Byte> sharedData;
			if (!readSyncFile(sharedData) || !validateMacroData(sharedData)) {
				if (syncFileMissing()) {
					vector<Byte> seedData;
					EnterCriticalSection(&syncLock);
					seedData = lastSyncedData;
					LeaveCriticalSection(&syncLock);
					if (!seedData.empty()) {
						writeSyncFile(seedData);
					}
				}
				continue;
			}

			EnterCriticalSection(&syncLock);
			if (!hasPendingLocalData && sharedData != lastSyncedData) {
				lastSyncedData = sharedData;
				pendingRemoteData = sharedData;
				hasPendingRemoteData = true;
			}
			LeaveCriticalSection(&syncLock);
		}
	}
}

void MacroSync::initialize() {
	if (syncInitialized) {
		return;
	}

	InitializeCriticalSection(&syncLock);
	syncWakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (syncWakeEvent == NULL) {
		DeleteCriticalSection(&syncLock);
		return;
	}

	getMacroSaveData(lastSyncedData);
	stopping = false;
	syncThread = CreateThread(NULL, 0, syncThreadProc, NULL, 0, NULL);
	if (syncThread == NULL) {
		CloseHandle(syncWakeEvent);
		syncWakeEvent = NULL;
		DeleteCriticalSection(&syncLock);
		lastSyncedData.clear();
		return;
	}

	syncInitialized = true;
	SetEvent(syncWakeEvent);
}

void MacroSync::shutdown() {
	if (!syncInitialized) {
		return;
	}

	EnterCriticalSection(&syncLock);
	stopping = true;
	syncInitialized = false;
	LeaveCriticalSection(&syncLock);
	SetEvent(syncWakeEvent);

	DWORD waitResult = WaitForSingleObject(syncThread, INFINITE);
	if (waitResult == WAIT_OBJECT_0) {
		CloseHandle(syncThread);
		CloseHandle(syncWakeEvent);
		syncThread = NULL;
		syncWakeEvent = NULL;
		DeleteCriticalSection(&syncLock);
	}
}

bool MacroSync::saveCurrent() {
	vector<Byte> data;
	getMacroSaveData(data);
	if (!validateMacroData(data)) {
		return false;
	}
	if (!syncInitialized) {
		return false;
	}

	EnterCriticalSection(&syncLock);
	pendingLocalData = data;
	hasPendingLocalData = true;
	++pendingLocalVersion;
	hasPendingRemoteData = false;
	pendingRemoteData.clear();
	LeaveCriticalSection(&syncLock);
	SetEvent(syncWakeEvent);
	return true;
}

bool MacroSync::checkForRemoteUpdate() {
	if (!syncInitialized) {
		return false;
	}

	vector<Byte> sharedData;
	EnterCriticalSection(&syncLock);
	if (!hasPendingRemoteData || hasPendingLocalData) {
		LeaveCriticalSection(&syncLock);
		return false;
	}
	sharedData = pendingRemoteData;
	hasPendingRemoteData = false;
	pendingRemoteData.clear();
	LeaveCriticalSection(&syncLock);

	applyMacroData(sharedData);
	return true;
}
