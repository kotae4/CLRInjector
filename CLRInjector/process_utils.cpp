#include "pch.h"
#include "process_utils.h"
#include "string_utils.h"
#include <TlHelp32.h>
#include <cstring>

namespace kotae
{
	namespace process_utils
	{
		bool IsCurrentProcess64bit()
		{
			HMODULE target = GetModuleHandle(NULL);
			IMAGE_DOS_HEADER* dosHd = (IMAGE_DOS_HEADER*)target;
			IMAGE_NT_HEADERS* ntHd = (IMAGE_NT_HEADERS*)((std::uintptr_t)target + (std::uintptr_t)dosHd->e_lfanew);

			if (ntHd->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
				return false;
			HANDLE curProcess = GetCurrentProcess();
			BOOL isWoW64 = FALSE;
			if (!IsWow64Process(curProcess, &isWoW64))
			{
				kotae::string_utils::PrintLastError();
			}
			return (isWoW64 == FALSE);
		}

		bool TryFindModuleByName(LPCTSTR moduleName, HMODULE* outModule, LPCTSTR& errMsg)
		{
			HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
			MODULEENTRY32 me32;

			DWORD snapshotFlags = TH32CS_SNAPMODULE;
			bool is64bitProcess = IsCurrentProcess64bit();
			if (is64bitProcess) snapshotFlags |= TH32CS_SNAPMODULE32;
			//  Take a snapshot of all modules in the specified process. 
			hModuleSnap = CreateToolhelp32Snapshot(snapshotFlags, GetCurrentProcessId());
			if (hModuleSnap == INVALID_HANDLE_VALUE)
			{
				errMsg = TEXT("Could not get snapshot for process");
				return false;
			}

			me32.dwSize = sizeof(MODULEENTRY32);
			if (!Module32First(hModuleSnap, &me32))
			{
				errMsg = TEXT("Could not get snapshot for process");
				CloseHandle(hModuleSnap);
				return false;
			}

			do
			{
				if (_tcsstr(me32.szModule, moduleName))
				{
					*outModule = me32.hModule;
					CloseHandle(hModuleSnap);
					return true;
				}
			} while (Module32Next(hModuleSnap, &me32));

			errMsg = TEXT("No loaded module matching name");
			return false;
		}
	}
}