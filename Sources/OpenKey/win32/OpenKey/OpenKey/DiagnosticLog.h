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
#pragma once

#include <string>

namespace DiagnosticLog {
	bool initialize();
	void shutdown();
	void log(const wchar_t* category, const wchar_t* message);
	void logf(const wchar_t* category, const wchar_t* format, ...);
	bool exportRecentTo(const std::wstring& path);
}
