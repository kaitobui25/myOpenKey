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
#include "DiagnosticLog.h"
#include "CrashHandler.h"

#include <ShlObj.h>
#include <algorithm>
#include <stdarg.h>
#include <string.h>

namespace {
	const DWORD WRITER_WAKE_INTERVAL_MS = 500;
	const DWORD FLUSH_INTERVAL_MS = 1000;
	const ULONGLONG SEGMENT_SECONDS = 10;
	const ULONGLONG RETENTION_SECONDS = 5 * 60;
	// Keep one extra partial segment so the retained window never falls short of five minutes.
	const ULONGLONG RETENTION_BUCKETS = (RETENTION_SECONDS / SEGMENT_SECONDS) + 1;
	const size_t MAX_PENDING_LINES = 2048;
	const DWORD MAX_EXPORTED_SEGMENT_BYTES = 2 * 1024 * 1024;
	const ULONGLONG MAX_ABNORMAL_ARCHIVE_BYTES = 64ULL * 1024 * 1024;

	CRITICAL_SECTION queueLock;
	HANDLE wakeEvent = NULL;
	HANDLE flushDoneEvent = NULL;
	HANDLE writerThread = NULL;
	HANDLE currentFile = INVALID_HANDLE_VALUE;
	bool initialized = false;
	bool stopRequested = false;
	bool flushRequested = false;
	ULONGLONG currentBucket = 0;
	ULONGLONG lastFlushTick = 0;
	ULONGLONG lastCleanupBucket = 0;
	bool currentFileDirty = false;
	LONG droppedLines = 0;
	std::vector<std::wstring> pendingLines;
	wchar_t logDirectory[MAX_PATH] = { 0 };

	struct SegmentInfo {
		ULONGLONG bucket;
		std::wstring path;
	};

	ULONGLONG getCurrentBucket() {
		FILETIME fileTime;
		ULARGE_INTEGER value;
		GetSystemTimeAsFileTime(&fileTime);
		value.LowPart = fileTime.dwLowDateTime;
		value.HighPart = fileTime.dwHighDateTime;
		return value.QuadPart / (10000000ULL * SEGMENT_SECONDS);
	}

	ULONGLONG getOldestRetainedBucket(const ULONGLONG newestBucket) {
		const ULONGLONG retainedHistory = RETENTION_BUCKETS > 0 ? RETENTION_BUCKETS - 1 : 0;
		return newestBucket > retainedHistory ? newestBucket - retainedHistory : 0;
	}

	bool buildPathInLogDirectory(const wchar_t* fileName, wchar_t* path, const size_t pathCount) {
		return _snwprintf_s(path, pathCount, _TRUNCATE, L"%s\\%s", logDirectory, fileName) >= 0;
	}

	bool buildLogDirectory() {
		wchar_t localAppData[MAX_PATH] = { 0 };
		if (FAILED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, localAppData))) {
			return false;
		}

		wchar_t openKeyDirectory[MAX_PATH] = { 0 };
		if (_snwprintf_s(openKeyDirectory, _countof(openKeyDirectory), _TRUNCATE, L"%s\\OpenKey", localAppData) < 0) {
			return false;
		}
		CreateDirectory(openKeyDirectory, NULL);

		if (_snwprintf_s(logDirectory, _countof(logDirectory), _TRUNCATE, L"%s\\Diagnostics", openKeyDirectory) < 0) {
			return false;
		}
		if (!CreateDirectory(logDirectory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
			return false;
		}
		return true;
	}

	bool buildSegmentPath(const ULONGLONG bucket, wchar_t* path, const size_t pathCount) {
		return _snwprintf_s(path, pathCount, _TRUNCATE, L"%s\\segment_%llu.log", logDirectory, bucket) >= 0;
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

	bool parseSegmentBucket(const wchar_t* fileName, ULONGLONG& bucket) {
		const wchar_t prefix[] = L"segment_";
		const wchar_t suffix[] = L".log";
		const size_t nameLength = wcslen(fileName);
		const size_t prefixLength = _countof(prefix) - 1;
		const size_t suffixLength = _countof(suffix) - 1;
		if (nameLength <= prefixLength + suffixLength || wcsncmp(fileName, prefix, prefixLength) != 0) {
			return false;
		}

		wchar_t* end = NULL;
		bucket = _wcstoui64(fileName + prefixLength, &end, 10);
		return end != NULL && wcscmp(end, suffix) == 0;
	}

	void cleanupExpiredSegments(const ULONGLONG newestBucket) {
		const ULONGLONG oldestBucket = getOldestRetainedBucket(newestBucket);
		wchar_t pattern[MAX_PATH] = { 0 };
		_snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\segment_*.log", logDirectory);

		WIN32_FIND_DATA data;
		HANDLE find = FindFirstFile(pattern, &data);
		if (find == INVALID_HANDLE_VALUE) {
			return;
		}

		do {
			ULONGLONG bucket = 0;
			if (!parseSegmentBucket(data.cFileName, bucket)) {
				continue;
			}
			if (bucket >= oldestBucket) {
				continue;
			}

			wchar_t path[MAX_PATH] = { 0 };
			_snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\%s", logDirectory, data.cFileName);
			DeleteFile(path);
		} while (FindNextFile(find, &data));
		FindClose(find);
	}

	bool purgeSegmentFiles() {
		wchar_t pattern[MAX_PATH] = { 0 };
		_snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\segment_*.log", logDirectory);

		WIN32_FIND_DATA data;
		HANDLE find = FindFirstFile(pattern, &data);
		if (find == INVALID_HANDLE_VALUE) {
			return GetLastError() == ERROR_FILE_NOT_FOUND;
		}

		bool ok = true;
		do {
			ULONGLONG bucket = 0;
			if (!parseSegmentBucket(data.cFileName, bucket)) {
				continue;
			}
			wchar_t path[MAX_PATH] = { 0 };
			_snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\%s", logDirectory, data.cFileName);
			if (!DeleteFile(path) && GetLastError() != ERROR_FILE_NOT_FOUND) {
				ok = false;
			}
		} while (FindNextFile(find, &data));
		FindClose(find);
		return ok;
	}

	bool ensureCurrentSegment() {
		const ULONGLONG bucket = getCurrentBucket();
		if (currentFile != INVALID_HANDLE_VALUE && currentBucket == bucket) {
			return true;
		}

		if (currentFile != INVALID_HANDLE_VALUE) {
			if (currentFileDirty) {
				FlushFileBuffers(currentFile);
			}
			CloseHandle(currentFile);
			currentFile = INVALID_HANDLE_VALUE;
			currentFileDirty = false;
		}

		wchar_t path[MAX_PATH] = { 0 };
		if (!buildSegmentPath(bucket, path, _countof(path))) {
			return false;
		}

		currentFile = CreateFile(
			path,
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (currentFile == INVALID_HANDLE_VALUE) {
			return false;
		}

		currentBucket = bucket;
		cleanupExpiredSegments(bucket);
		return true;
	}

	bool writeWideAsUtf8(HANDLE file, const std::wstring& text) {
		if (file == INVALID_HANDLE_VALUE || text.empty()) {
			return file != INVALID_HANDLE_VALUE;
		}
		const int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
		if (byteCount <= 0) {
			return false;
		}
		std::vector<char> utf8((size_t)byteCount);
		if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), byteCount, NULL, NULL) <= 0) {
			return false;
		}
		return writeAll(file, reinterpret_cast<const BYTE*>(utf8.data()), (DWORD)utf8.size());
	}

	void drainPendingLines() {
		std::vector<std::wstring> lines;
		LONG dropped = 0;
		EnterCriticalSection(&queueLock);
		lines.swap(pendingLines);
		dropped = InterlockedExchange(&droppedLines, 0);
		LeaveCriticalSection(&queueLock);

		if (lines.empty() && dropped == 0) {
			return;
		}
		if (!ensureCurrentSegment()) {
			EnterCriticalSection(&queueLock);
			for (size_t i = 0; i < lines.size() && pendingLines.size() < MAX_PENDING_LINES; ++i) {
				pendingLines.push_back(lines[i]);
			}
			if (dropped > 0) {
				InterlockedExchangeAdd(&droppedLines, dropped);
			}
			LeaveCriticalSection(&queueLock);
			return;
		}

		if (dropped > 0) {
			wchar_t droppedLine[128];
			_snwprintf_s(droppedLine, _countof(droppedLine), _TRUNCATE, L"[LOGGER] dropped=%ld events because queue was full\r\n", dropped);
			writeWideAsUtf8(currentFile, droppedLine);
			currentFileDirty = true;
		}
		for (size_t i = 0; i < lines.size(); ++i) {
			writeWideAsUtf8(currentFile, lines[i]);
			currentFileDirty = true;
		}
	}

	DWORD WINAPI writerThreadProc(LPVOID) {
		while (true) {
			WaitForSingleObject(wakeEvent, WRITER_WAKE_INTERVAL_MS);
			drainPendingLines();

			const ULONGLONG now = GetTickCount64();
			if (currentFile != INVALID_HANDLE_VALUE && currentFileDirty && now - lastFlushTick >= FLUSH_INTERVAL_MS) {
				FlushFileBuffers(currentFile);
				currentFileDirty = false;
				lastFlushTick = now;
			}

			const ULONGLONG wallClockBucket = getCurrentBucket();
			if (wallClockBucket != lastCleanupBucket) {
				cleanupExpiredSegments(wallClockBucket);
				lastCleanupBucket = wallClockBucket;
			}

			bool shouldStop = false;
			bool shouldSignalFlush = false;
			EnterCriticalSection(&queueLock);
			shouldStop = stopRequested;
			if (flushRequested) {
				flushRequested = false;
				shouldSignalFlush = true;
			}
			LeaveCriticalSection(&queueLock);

			if (shouldSignalFlush) {
				if (currentFile != INVALID_HANDLE_VALUE && currentFileDirty) {
					FlushFileBuffers(currentFile);
					currentFileDirty = false;
				}
				SetEvent(flushDoneEvent);
			}

			if (shouldStop) {
				drainPendingLines();
				if (currentFile != INVALID_HANDLE_VALUE) {
					if (currentFileDirty) {
						FlushFileBuffers(currentFile);
					}
					CloseHandle(currentFile);
					currentFile = INVALID_HANDLE_VALUE;
					currentFileDirty = false;
				}
				return 0;
			}
		}
	}

	bool flushWriter() {
		if (!initialized) {
			return false;
		}
		ResetEvent(flushDoneEvent);
		EnterCriticalSection(&queueLock);
		flushRequested = true;
		LeaveCriticalSection(&queueLock);
		SetEvent(wakeEvent);
		return WaitForSingleObject(flushDoneEvent, 2000) == WAIT_OBJECT_0;
	}

	std::vector<SegmentInfo> findAllSegments() {
		std::vector<SegmentInfo> segments;
		wchar_t pattern[MAX_PATH] = { 0 };
		_snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\segment_*.log", logDirectory);

		WIN32_FIND_DATA data;
		HANDLE find = FindFirstFile(pattern, &data);
		if (find == INVALID_HANDLE_VALUE) {
			return segments;
		}
		do {
			ULONGLONG bucket = 0;
			if (!parseSegmentBucket(data.cFileName, bucket)) {
				continue;
			}
			wchar_t path[MAX_PATH] = { 0 };
			_snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\%s", logDirectory, data.cFileName);
			segments.push_back({ bucket, path });
		} while (FindNextFile(find, &data));
		FindClose(find);

		std::sort(segments.begin(), segments.end(), [](const SegmentInfo& left, const SegmentInfo& right) {
			return left.bucket < right.bucket;
		});
		return segments;
	}

	std::vector<SegmentInfo> findRecentSegments() {
		std::vector<SegmentInfo> segments = findAllSegments();
		const ULONGLONG oldestBucket = getOldestRetainedBucket(getCurrentBucket());
		segments.erase(std::remove_if(segments.begin(), segments.end(), [oldestBucket](const SegmentInfo& segment) {
			return segment.bucket < oldestBucket;
		}), segments.end());
		return segments;
	}

	std::vector<SegmentInfo> findLastRetainedSegmentsFromDisk(const ULONGLONG sessionStartBucket) {
		std::vector<SegmentInfo> segments = findAllSegments();
		if (!segments.empty()) {
			const ULONGLONG retentionFloor = getOldestRetainedBucket(segments.back().bucket);
			const ULONGLONG oldestBucket = sessionStartBucket > retentionFloor ? sessionStartBucket : retentionFloor;
			segments.erase(std::remove_if(segments.begin(), segments.end(), [oldestBucket](const SegmentInfo& segment) {
				return segment.bucket < oldestBucket;
			}), segments.end());
		}
		return segments;
	}

	bool copyPathToFile(const std::wstring& path, HANDLE destination, const ULONGLONG maxBytes) {
		HANDLE source = CreateFile(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (source == INVALID_HANDLE_VALUE) {
			return false;
		}
		LARGE_INTEGER size;
		if (!GetFileSizeEx(source, &size) || size.QuadPart < 0 || (ULONGLONG)size.QuadPart > maxBytes) {
			CloseHandle(source);
			return false;
		}

		BYTE buffer[64 * 1024];
		bool ok = true;
		while (true) {
			DWORD read = 0;
			if (!ReadFile(source, buffer, sizeof(buffer), &read, NULL)) {
				ok = false;
				break;
			}
			if (read == 0) {
				break;
			}
			if (!writeAll(destination, buffer, read)) {
				ok = false;
				break;
			}
		}
		CloseHandle(source);
		return ok;
	}

	bool copySegmentToFile(const SegmentInfo& segment, HANDLE destination) {
		return copyPathToFile(segment.path, destination, MAX_EXPORTED_SEGMENT_BYTES);
	}

	bool sessionMarkerExists() {
		wchar_t path[MAX_PATH] = { 0 };
		if (!buildPathInLogDirectory(L"session.active", path, _countof(path))) {
			return false;
		}
		DWORD attributes = GetFileAttributes(path);
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	ULONGLONG readSessionStartBucket() {
		wchar_t path[MAX_PATH] = { 0 };
		if (!buildPathInLogDirectory(L"session.active", path, _countof(path))) {
			return 0;
		}
		HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			return 0;
		}
		char buffer[256] = { 0 };
		DWORD read = 0;
		ReadFile(file, buffer, sizeof(buffer) - 1, &read, NULL);
		CloseHandle(file);
		buffer[read < sizeof(buffer) ? read : sizeof(buffer) - 1] = 0;
		const char* marker = strstr(buffer, "startBucket=");
		if (!marker) {
			return 0;
		}
		return _strtoui64(marker + strlen("startBucket="), NULL, 10);
	}

	void createSessionMarker() {
		wchar_t path[MAX_PATH] = { 0 };
		if (!buildPathInLogDirectory(L"session.active", path, _countof(path))) {
			return;
		}
		HANDLE file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
		wchar_t line[160] = { 0 };
		_snwprintf_s(line, _countof(line), _TRUNCATE, L"pid=%lu\r\nstartBucket=%llu\r\n",
			GetCurrentProcessId(), getCurrentBucket());
		writeWideAsUtf8(file, line);
		FlushFileBuffers(file);
		CloseHandle(file);
	}

	void removeSessionMarker() {
		wchar_t path[MAX_PATH] = { 0 };
		if (buildPathInLogDirectory(L"session.active", path, _countof(path))) {
			DeleteFile(path);
		}
	}

	bool archivePreviousAbnormalSession() {
		if (!sessionMarkerExists()) {
			return true;
		}
		const ULONGLONG sessionStartBucket = readSessionStartBucket();
		std::vector<SegmentInfo> segments = findLastRetainedSegmentsFromDisk(sessionStartBucket);
		if (segments.empty()) {
			return true;
		}

		wchar_t archivePath[MAX_PATH] = { 0 };
		if (!buildPathInLogDirectory(L"last_abnormal_session.log", archivePath, _countof(archivePath))) {
			return false;
		}
		HANDLE destination = CreateFile(archivePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (destination == INVALID_HANDLE_VALUE) {
			return false;
		}
		bool ok = writeWideAsUtf8(destination,
			L"Previous OpenKey session did not shut down cleanly. The final retained diagnostic window follows.\r\n\r\n");
		for (size_t i = 0; i < segments.size(); ++i) {
			if (!copySegmentToFile(segments[i], destination)) {
				ok = false;
				break;
			}
		}
		if (!FlushFileBuffers(destination)) {
			ok = false;
		}
		CloseHandle(destination);
		if (!ok) {
			DeleteFile(archivePath);
		}
		return ok;
	}

	std::wstring makeLogLine(const wchar_t* category, const wchar_t* message) {
		SYSTEMTIME now;
		GetLocalTime(&now);
		wchar_t line[1280] = { 0 };
		_snwprintf_s(line, _countof(line), _TRUNCATE,
			L"%04d-%02d-%02d %02d:%02d:%02d.%03d [tid=%lu] [%s] %s\r\n",
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
			GetCurrentThreadId(), category ? category : L"GENERAL", message ? message : L"");
		return line;
	}

	bool writeUtf8Text(HANDLE file, const wchar_t* text) {
		if (!text) {
			return false;
		}
		return writeWideAsUtf8(file, text);
	}
}

bool DiagnosticLog::initialize() {
	if (initialized) {
		return true;
	}
	if (!buildLogDirectory()) {
		return false;
	}
	// Preserve an unclean previous session before clearing rolling files.
	// Each process gets its own fresh segment set, preventing cross-session mixing
	// (including restarts that happen within the same 10-second bucket).
	if (!archivePreviousAbnormalSession() || !purgeSegmentFiles()) {
		return false;
	}

	InitializeCriticalSection(&queueLock);
	pendingLines.reserve(256);
	wakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	flushDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!wakeEvent || !flushDoneEvent) {
		if (wakeEvent) CloseHandle(wakeEvent);
		if (flushDoneEvent) CloseHandle(flushDoneEvent);
		wakeEvent = NULL;
		flushDoneEvent = NULL;
		DeleteCriticalSection(&queueLock);
		return false;
	}

	stopRequested = false;
	flushRequested = false;
	lastFlushTick = GetTickCount64();
	lastCleanupBucket = getCurrentBucket();
	currentFileDirty = false;
	writerThread = CreateThread(NULL, 0, writerThreadProc, NULL, 0, NULL);
	if (!writerThread) {
		CloseHandle(wakeEvent);
		CloseHandle(flushDoneEvent);
		wakeEvent = NULL;
		flushDoneEvent = NULL;
		DeleteCriticalSection(&queueLock);
		return false;
	}

	CrashHandler::initialize(logDirectory);
	initialized = true;
	createSessionMarker();
	logf(L"APP", L"diagnostics initialized retention=%llu seconds", RETENTION_SECONDS);
	return true;
}

void DiagnosticLog::shutdown() {
	if (!initialized) {
		return;
	}
	log(L"APP", L"diagnostics shutdown");

	EnterCriticalSection(&queueLock);
	stopRequested = true;
	LeaveCriticalSection(&queueLock);
	SetEvent(wakeEvent);
	WaitForSingleObject(writerThread, INFINITE);

	CrashHandler::shutdown();
	CloseHandle(writerThread);
	CloseHandle(wakeEvent);
	CloseHandle(flushDoneEvent);
	writerThread = NULL;
	wakeEvent = NULL;
	flushDoneEvent = NULL;
	initialized = false;
	removeSessionMarker();
	DeleteCriticalSection(&queueLock);
}

void DiagnosticLog::log(const wchar_t* category, const wchar_t* message) {
	if (!initialized) {
		return;
	}
	std::wstring line = makeLogLine(category, message);
	EnterCriticalSection(&queueLock);
	if (pendingLines.size() < MAX_PENDING_LINES) {
		pendingLines.push_back(line);
	} else {
		InterlockedIncrement(&droppedLines);
	}
	LeaveCriticalSection(&queueLock);
	SetEvent(wakeEvent);
}

void DiagnosticLog::logf(const wchar_t* category, const wchar_t* format, ...) {
	if (!initialized || !format) {
		return;
	}
	wchar_t message[768] = { 0 };
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
	va_end(args);
	log(category, message);
}

bool DiagnosticLog::exportRecentTo(const std::wstring& path) {
	if (!initialized || path.empty()) {
		return false;
	}
	if (!flushWriter()) {
		return false;
	}

	HANDLE destination = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (destination == INVALID_HANDLE_VALUE) {
		return false;
	}

	const BYTE utf8Bom[] = { 0xEF, 0xBB, 0xBF };
	bool ok = writeAll(destination, utf8Bom, sizeof(utf8Bom));
	wchar_t header[1024] = { 0 };
	_snwprintf_s(header, _countof(header), _TRUNCATE,
		L"OpenKey diagnostic log\r\nVersion: %s\r\nCurrent-session retention: at least the last 5 minutes (one partial 10-second segment may be extra). The latest abnormal-session snapshot is included when available.\r\nText-log privacy: raw keystrokes, typed text and macro contents are not recorded; runtime state is recorded. Crash .dmp files are separate and may contain process-memory fragments, so treat them as sensitive.\r\nDiagnostics folder: %s\r\n\r\n",
		OpenKeyHelper::getVersionString().c_str(), logDirectory);
	ok = writeUtf8Text(destination, header) && ok;

	wchar_t abnormalPath[MAX_PATH] = { 0 };
	if (buildPathInLogDirectory(L"last_abnormal_session.log", abnormalPath, _countof(abnormalPath)) &&
		GetFileAttributes(abnormalPath) != INVALID_FILE_ATTRIBUTES) {
		ok = writeUtf8Text(destination, L"--- LAST ABNORMAL SESSION (if detected) ---\r\n") && ok;
		ok = copyPathToFile(abnormalPath, destination, MAX_ABNORMAL_ARCHIVE_BYTES) && ok;
		ok = writeUtf8Text(destination, L"\r\n--- CURRENT SESSION: LAST 5 MINUTES ---\r\n") && ok;
	}

	std::vector<SegmentInfo> segments = findRecentSegments();
	for (size_t i = 0; i < segments.size(); ++i) {
		if (!copySegmentToFile(segments[i], destination)) {
			ok = false;
		}
	}
	if (!FlushFileBuffers(destination)) {
		ok = false;
	}
	CloseHandle(destination);
	return ok;
}
