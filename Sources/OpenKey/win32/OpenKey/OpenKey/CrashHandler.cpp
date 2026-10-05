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
#include "CrashHandler.h"

#include <DbgHelp.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")

namespace {
	wchar_t outputDirectory[MAX_PATH] = { 0 };
	LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = NULL;
	bool initialized = false;
	const size_t MAX_CRASH_DUMPS = 5;
	LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);

	struct CrashFile {
		FILETIME writeTime;
		std::wstring path;
	};

	bool newerThan(const FILETIME& left, const FILETIME& right) {
		return CompareFileTime(&left, &right) > 0;
	}

	void cleanupCrashFiles(const wchar_t* patternSuffix, const size_t maxExistingFiles) {
		wchar_t pattern[MAX_PATH] = { 0 };
		_snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\crash_*%s", outputDirectory, patternSuffix);
		WIN32_FIND_DATA data;
		HANDLE find = FindFirstFile(pattern, &data);
		if (find == INVALID_HANDLE_VALUE) {
			return;
		}

		std::vector<CrashFile> files;
		do {
			wchar_t path[MAX_PATH] = { 0 };
			_snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\%s", outputDirectory, data.cFileName);
			files.push_back({ data.ftLastWriteTime, path });
		} while (FindNextFile(find, &data));
		FindClose(find);

		std::sort(files.begin(), files.end(), [](const CrashFile& left, const CrashFile& right) {
			return newerThan(left.writeTime, right.writeTime);
		});
		for (size_t i = maxExistingFiles; i < files.size(); ++i) {
			DeleteFile(files[i].path.c_str());
		}
	}

	void cleanupOldCrashArtifacts() {
		// Reserve one slot for the current process so a new crash never pushes the total past the cap.
		const size_t maxExistingFiles = MAX_CRASH_DUMPS > 0 ? MAX_CRASH_DUMPS - 1 : 0;
		cleanupCrashFiles(L".dmp", maxExistingFiles);
		cleanupCrashFiles(L".txt", maxExistingFiles);
	}

	bool writeAll(HANDLE file, const BYTE* data, DWORD size) {
		DWORD offset = 0;
		while (offset < size) {
			DWORD written = 0;
			if (!WriteFile(file, data + offset, size - offset, &written, NULL) || written == 0) {
				return false;
			}
			offset += written;
		}
		return true;
	}

	void writeCrashNote(const wchar_t* path, EXCEPTION_POINTERS* exceptionInfo, const wchar_t* dumpPath,
		const bool dumpSucceeded, const DWORD dumpError) {
		HANDLE file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		const WORD bom = 0xFEFF;
		writeAll(file, reinterpret_cast<const BYTE*>(&bom), sizeof(bom));

		wchar_t line[1024] = { 0 };
		const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
		const void* address = exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionAddress : NULL;
		_snwprintf_s(line, _countof(line), _TRUNCATE,
			L"OpenKey unhandled exception\r\nExceptionCode=0x%08X\r\nAddress=%p\r\nDumpStatus=%s\r\nDumpError=%lu\r\nDump=%s\r\n",
			code, address, dumpSucceeded ? L"success" : L"failed", dumpError, dumpSucceeded ? dumpPath : L"(not created)");
		writeAll(file, reinterpret_cast<const BYTE*>(line), (DWORD)(wcslen(line) * sizeof(wchar_t)));
		FlushFileBuffers(file);
		CloseHandle(file);
	}

	LONG continueUnhandledException(EXCEPTION_POINTERS* exceptionInfo) {
		if (previousExceptionFilter && previousExceptionFilter != unhandledExceptionFilter) {
			LONG previousResult = previousExceptionFilter(exceptionInfo);
			if (previousResult != EXCEPTION_CONTINUE_SEARCH) {
				return previousResult;
			}
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}

	LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
		if (outputDirectory[0] == 0) {
			return continueUnhandledException(exceptionInfo);
		}

		SYSTEMTIME now;
		GetLocalTime(&now);
		wchar_t baseName[96] = { 0 };
		_snwprintf_s(baseName, _countof(baseName), _TRUNCATE, L"crash_%04d%02d%02d_%02d%02d%02d",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

		wchar_t dumpPath[MAX_PATH] = { 0 };
		wchar_t notePath[MAX_PATH] = { 0 };
		_snwprintf_s(dumpPath, _countof(dumpPath), _TRUNCATE, L"%s\\%s.dmp", outputDirectory, baseName);
		_snwprintf_s(notePath, _countof(notePath), _TRUNCATE, L"%s\\%s.txt", outputDirectory, baseName);

		bool dumpSucceeded = false;
		DWORD dumpError = ERROR_SUCCESS;
		HANDLE dump = CreateFile(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (dump != INVALID_HANDLE_VALUE) {
			MINIDUMP_EXCEPTION_INFORMATION exceptionData;
			exceptionData.ThreadId = GetCurrentThreadId();
			exceptionData.ExceptionPointers = exceptionInfo;
			exceptionData.ClientPointers = FALSE;
			dumpSucceeded = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
				(MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo), &exceptionData, NULL, NULL) != FALSE;
			if (!dumpSucceeded) {
				dumpError = GetLastError();
			} else if (!FlushFileBuffers(dump)) {
				dumpSucceeded = false;
				dumpError = GetLastError();
			}
			CloseHandle(dump);
			if (!dumpSucceeded) {
				DeleteFile(dumpPath);
			}
		} else {
			dumpError = GetLastError();
		}
		writeCrashNote(notePath, exceptionInfo, dumpPath, dumpSucceeded, dumpError);
		return continueUnhandledException(exceptionInfo);
	}
}

bool CrashHandler::initialize(const wchar_t* directory) {
	if (initialized) {
		return true;
	}
	if (!directory || directory[0] == 0 || wcsncpy_s(outputDirectory, _countof(outputDirectory), directory, _TRUNCATE) != 0) {
		return false;
	}
	cleanupOldCrashArtifacts();
	previousExceptionFilter = SetUnhandledExceptionFilter(unhandledExceptionFilter);
	initialized = true;
	return true;
}

void CrashHandler::shutdown() {
	if (!initialized) {
		return;
	}
	SetUnhandledExceptionFilter(previousExceptionFilter);
	previousExceptionFilter = NULL;
	outputDirectory[0] = 0;
	initialized = false;
}
