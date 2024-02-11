#include "pch.h"
#include "NETInjector.h"
#include "string_utils.h"
#include "process_utils.h"
#include "InjectionConfig.h"

bool NETInjector::Prepare()
{
	// TO-DO:
	// sig scan for the CLR functions we need
	// this'll free us from our compile-time dependency (not a big deal at all)
	return true;
}

bool NETInjector::Inject(InjectionConfig& config, LPCTSTR& errMsg)
{
	if (Prepare() == false)
	{
		errMsg = TEXT("Could not find fptr's for .NET injection");
		return false;
	}
	ICLRMetaHost* pMetaHost = NULL;
	ICLRRuntimeInfo* pRuntimeInfo = NULL;
	ICLRRuntimeHost* pRuntimeHost = NULL;
	HRESULT hr;
	DWORD InjectedLibraryResult = -1;

	std::wstring configRuntimeVersionW;
	if (kotae::string_utils::TryConvertUtf8ToUtf16(config.RuntimeVersion, configRuntimeVersionW) == false)
	{
		_tprintf(TEXT("Could not convert config RuntimeVersion to wide string for .NET injection\n"));
		return false;
	}

	// TO-DO:
	// eventually move all this over to the sig-scanned function ptrs
	// but linking CLR libs is fine for now
	hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost, (LPVOID*)&pMetaHost);
	if (hr != S_OK)
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get/create CLR metahost instance");
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got MetaHost [%tx]\n"), pMetaHost);

	hr = pMetaHost->GetRuntime(configRuntimeVersionW.c_str(), IID_ICLRRuntimeInfo, (LPVOID*)&pRuntimeInfo);
	if (hr != S_OK)
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get runtime instance");
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got RuntimeInfo [%tx]\n"), pRuntimeInfo);

	hr = pRuntimeInfo->GetInterface(CLSID_CLRRuntimeHost, IID_ICLRRuntimeHost, (LPVOID*)&pRuntimeHost);
	if (hr != S_OK)
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get runtime interface");
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got RuntimeHost [%tx]\n"), pRuntimeHost);

	_tprintf(TEXT("[Bootstrapper] Prepared the CLR, executing managed DLL(s) now\n"));
	std::wstring curAssemblyPathW, curInjectEntryTypeNameW, curInjectEntryMethodNameW, curInjectEntryStringArgumentW;
	for (std::vector<InjectionAssemblyInfo>::iterator it = config.Assemblies.begin(); it != config.Assemblies.end(); ++it)
	{
		InjectionAssemblyInfo curAssembly = *it;
		if ((kotae::string_utils::TryConvertUtf8ToUtf16(curAssembly.AssemblyPath, curAssemblyPathW) == false) ||
			(kotae::string_utils::TryConvertUtf8ToUtf16(curAssembly.EntryTypeName, curInjectEntryTypeNameW) == false) ||
			(kotae::string_utils::TryConvertUtf8ToUtf16(curAssembly.EntryMethodName, curInjectEntryMethodNameW) == false) ||
			(kotae::string_utils::TryConvertUtf8ToUtf16(curAssembly.EntryStringArgument, curInjectEntryStringArgumentW) == false))
		{
			_tprintf(TEXT("Could not convert config entries to wide strings for .NET injection\n"));
		}
		// TO-DO:
		// if the target method is a class constructor then do something else instead... 
		// or maybe not? maybe we can execute the constructor just like this and just store the return value to pass as the 'this' ptr to the "OnFinishedLoadingAllMods" method below
		hr = pRuntimeHost->ExecuteInDefaultAppDomain(curAssemblyPathW.c_str(), curInjectEntryTypeNameW.c_str(), curInjectEntryMethodNameW.c_str(), curInjectEntryStringArgumentW.c_str(), &InjectedLibraryResult);
		_tprintf(TEXT("[Bootstrapper] executed\n"));
		if (hr != S_OK)
		{
			if (hr == HOST_E_CLRNOTAVAILABLE)
			{
				_tprintf(TEXT("[Bootstrapper] CLR was in bad state, could not execute (%lx)\n"), hr);
			}
			else if (hr == HOST_E_TIMEOUT)
			{
				_tprintf(TEXT("[Bootstrapper] execution timed out (%lx)\n"), hr);
			}
			else if (hr == HOST_E_NOT_OWNER)
			{
				_tprintf(TEXT("[Bootstrapper] CLR owned by someone else, could not execute (%lx)\n"), hr);
			}
			else if (hr == HOST_E_ABANDONED)
			{
				_tprintf(TEXT("[Bootstrapper] An event was canceled while a blocked thread or fiber was waiting on it. (%lx)\n"), hr);
			}
			else if (hr == E_FAIL)
			{
				_tprintf(TEXT("[Bootstrapper][CRITICAL] An unknown catastrophic failure occurred. (%lx)\n"), hr);
			}
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not execute injected DLL's entrypoint");
			return false;
		}
		// TO-DO:
		// before moving to next mod, check to see if it implements "OnFinishedLoadingAllMods" method and add it to a collection
	}

	// TO-DO:
	// iterate "OnFinishedLoadingAllMods" collection and invoke each one now that all mods are loaded

	// TO-DO:
	// it definitely wouldn't make sense to do this, right?
	// but i wonder if there's another way of retrieving the RuntimeHost without calling CLRCreateInstance?
	// or maybe calling CLRCreateInstance isn't so bad, maybe it doesn't actually create anything if there's already an instance in the process?
	/*
	if (pRuntimeHost != NULL)
	{
		pRuntimeHost->Stop();
		pRuntimeHost->Release();
	}
	// TO-DO:
	// releasing these *might* actually be a good idea...
	// leaving them commented for now and accepting that there may be a tiny leak
	// definitely need to figure out if new resources are actually allocated or if we're just grabbing references to the process' existing resources (and thus releasing them would be bad, unless they're ref counted)
	if (pRuntimeInfo != NULL)
		pRuntimeInfo->Release();
	if (pMetaHost != NULL)
		pMetaHost->Release();
	*/
	return true;
}