// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "main.h"
#include "string_utils.h"
#include "ManagedContext.h"
#include "logger.h"

LPCSTR CONFIG_FILE_PATH = "CLRInjector_config.txt";
HMODULE hModSelf;
InjectionConfig config;

VOID Eject(DWORD exitCode = 0, LPCTSTR reason = NULL, UINT icon = MB_ICONINFORMATION)
{
	if (reason)
	{
		MessageBox(NULL, reason, TEXT("CLRInjector"), MB_OK | icon | MB_TOPMOST);
	}
	if (!DeleteFileA(CONFIG_FILE_PATH))
	{
		kotae::string_utils::PrintErrorMessage(GetLastError());
	}
	FreeLibraryAndExitThread(hModSelf, exitCode);
}

VOID RegisterConsole()
{
	if (AllocConsole() == FALSE)
	{
		// process already has a console, so we need to manually hook up our stdin/stdout/stderr to the console (well, we're only interested in stdout for now)
		MessageBox(NULL, TEXT("Process already has console, hooking up stdin/out/err"), TEXT("CLRInjector"), MB_OK | MB_ICONWARNING | MB_TOPMOST);
	}
	// if AllocConsole returns true, then
	// the process did NOT have a console, and so the function call did two things: 
	// 1. created a new console associated with the process
	// 2. initialized standard i/o/e streams
	// BUT! we need to re-open our standard streams to point to the new console streams:
	// ah, i finally figured out what the first arg does: if the function succeeds, it's identical to the fourth arg. if it fails, it'll be null or something else.
	// i was confused for so long because it's weird to have an OUT parameter as the first parameter
	FILE* fDummy = nullptr;
	freopen_s(&fDummy, "CONOUT$", "w", stderr);
	freopen_s(&fDummy, "CONOUT$", "w", stdout);
}

VOID Inject()
{
	RegisterConsole();
	LOG_INFO("Injected, loading mods now...");
	config = InjectionConfig();
	if (config.ReadFile(CONFIG_FILE_PATH) == false)
	{
		Eject(0, TEXT("Could not load settings from config file"));
		return;
	}
	LOG_INFO("Loaded config. Trying to inject mods now...");

	ManagedContext context{};
	if (context.TryInject(config) == false)
	{
		LOG_ERROR("Could not load all mods into managed environment!");
		Eject(0);
		return;
	}

	LOG_INFO("Successfully loaded all mods, exiting now.");
	Eject(1);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Inject, 0, 0, 0);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

