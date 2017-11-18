#include "ppsspp_config.h"

#ifdef _MSC_VER

#include <cstdint>
#include "OSVersion.h"
#include "Common/CommonWindows.h"

bool DoesVersionMatchWindows(uint32_t major, uint32_t minor, uint32_t spMajor, uint32_t spMinor, bool greater) {
#if PPSSPP_PLATFORM(UWP)
	if (greater)
		return true;
	else
		return major >= 7;
#else
	uint64_t conditionMask = 0;
	OSVERSIONINFOEX osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));

	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = major;
	osvi.dwMinorVersion = minor;
	osvi.wServicePackMajor = spMajor;
	osvi.wServicePackMinor = spMinor;
	uint32_t op = greater ? VER_GREATER_EQUAL : VER_EQUAL;

	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, op);
	VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMAJOR, op);
	VER_SET_CONDITION(conditionMask, VER_SERVICEPACKMINOR, op);

	const uint32_t typeMask = VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR;

	return VerifyVersionInfo(&osvi, typeMask, conditionMask) != FALSE;
#endif
}

bool IsVistaOrHigher() {
#if PPSSPP_PLATFORM(UWP)
	return true;
#else
	OSVERSIONINFOEX osvi;
	DWORDLONG dwlConditionMask = 0;
	int op = VER_GREATER_EQUAL;
	ZeroMemory(&osvi, sizeof(osvi));
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = 6;  // Vista is 6.0
	osvi.dwMinorVersion = 0;

	VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, op);
	VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, op);

	return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask) != FALSE;
#endif
}

std::string GetWindowsVersion() {
	const bool IsWindowsXPSP2 = DoesVersionMatchWindows(5, 1, 2, 0, false);
	const bool IsWindowsXPSP3 = DoesVersionMatchWindows(5, 1, 3, 0, false);
	const bool IsWindowsVista = DoesVersionMatchWindows(6, 0, 0, 0, false);
	const bool IsWindowsVistaSP1 = DoesVersionMatchWindows(6, 0, 1, 0, false);
	const bool IsWindowsVistaSP2 = DoesVersionMatchWindows(6, 0, 2, 0, false);
	const bool IsWindows7 = DoesVersionMatchWindows(6, 1, 0, 0, false);
	const bool IsWindows7SP1 = DoesVersionMatchWindows(6, 1, 1, 0, false);
	const bool IsWindows8 = DoesVersionMatchWindows(6, 2, 0, 0, false);
	const bool IsWindows8_1 = DoesVersionMatchWindows(6, 3, 0, 0, false);

	if (IsWindowsXPSP2) return "Microsoft Windows XP, Service Pack 2";
	if (IsWindowsXPSP3) return "Microsoft Windows XP, Service Pack 3";
	if (IsWindowsVista) return "Microsoft Windows Vista";
	if (IsWindowsVistaSP1) return "Microsoft Windows Vista, Service Pack 1";
	if (IsWindowsVistaSP2) return "Microsoft Windows Vista, Service Pack 2";
	if (IsWindows7) return "Microsoft Windows 7";
	if (IsWindows7SP1) return "Microsoft Windows 7, Service Pack 1";
	if (IsWindows8) return "Microsoft Windows 8 or greater"; // "Applications not manifested for Windows 10 will return the Windows 8 OS version value (6.2)."
	if (IsWindows8_1) return "Microsoft Windows 8.1";
	return "Unsupported version of Microsoft Windows.";
}

std::string GetWindowsSystemArchitecture() {
	SYSTEM_INFO sysinfo;
	ZeroMemory(&sysinfo, sizeof(SYSTEM_INFO));
	GetNativeSystemInfo(&sysinfo);

	if (sysinfo.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_AMD64)
		return "(x64)";
	// Need to check for equality here, since ANDing with 0 is always 0.
	else if (sysinfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
		return "(x86)";
	else if (sysinfo.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_ARM)
		return "(ARM)";
	else
		return "(Unknown)";
}

#endif