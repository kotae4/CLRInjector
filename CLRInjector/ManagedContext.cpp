#include "pch.h"
#include "ManagedContext.h"
#include "logger.h"


bool ManagedContext::TryInject(InjectionConfig& config)
{
	bool success = true;
	std::unique_ptr<BaseCLRInjector> injector = nullptr;
	std::string entryMethodSignature{};
	if (config.CLRType == ECLRType::NET)
	{
		LOG_INFO("Instantiating NETInjector_COMBloat for CLR injection");
		injector = std::make_unique<NETFrameworkInjector_COMBloat>();
	}
	else if (config.CLRType == ECLRType::Mono)
	{
		LOG_INFO("Instantiating MonoInjector for CLR injection");
		injector = std::make_unique<MonoInjector>();
	}
	else
	{
		LOG_ERROR("Config did not specify which CLR to inject into ({})", static_cast<int>(config.CLRType));
		return false;
	}

	if (injector->TryPrepareState(config.RuntimeVersion) == false)
	{
		LOG_ERROR("Could not prepare CLR state for injection");
		return false;
	}

	LOG_INFO("Prepared CLR state");
	std::vector<CLRMethodInvocationData> onFinishedLoadingAllModsMethodCollection{};
	for (std::vector<InjectionAssemblyInfo>::iterator it = config.Assemblies.begin(); it != config.Assemblies.end(); ++it)
	{
		InjectionAssemblyInfo curAssembly = *it;

		LOG_INFO("Attempting to load mod '{}'", curAssembly.AssemblyFullName);

		std::string entryMethodSignature{};
		if (config.CLRType == ECLRType::NET)
		{
			entryMethodSignature = curAssembly.EntryMethodSignature.GetNETSignature();
		}
		else
		{
			entryMethodSignature = curAssembly.EntryMethodSignature.GetMonoSignature();
		}
		if (entryMethodSignature.compare("ERROR") == 0)
		{
			LOG_ERROR("Could not construct signature from {}", curAssembly.EntryMethodSignature.FullMethodSignature);
			success = false;
			continue;
		}
		LOG_INFO("\tGot entry method signature for mod", entryMethodSignature);

		ManagedResourceHandle assemblyHandle;
		if (injector->TryLoadAssembly(curAssembly.AssemblyPath, curAssembly.AssemblyFullName, &assemblyHandle) == false)
		{
			LOG_ERROR("Could not load mod '{}'", curAssembly.AssemblyFullName);
			success = false;
			continue;
		}
		LOG_INFO("\tLoaded mod assembly into CLR");

		ManagedResourceHandle typeHandle;
		if (injector->TryFindTypeByName(assemblyHandle, curAssembly.EntryTypeName, &typeHandle) == false)
		{
			LOG_ERROR("Could not find type '{}' in mod '{}'", curAssembly.EntryTypeName, curAssembly.AssemblyFullName);
			success = false;
			continue;
		}
		LOG_INFO("\tGot entry type handle: {}", typeHandle);

		ManagedResourceHandle methodHandle, retValHandle;
		if (curAssembly.IsEntryClassConstructor == true)
		{
			if (injector->TryFindConstructorBySignature(typeHandle, entryMethodSignature, &methodHandle) == false)
			{
				LOG_ERROR("Could not find constructor matching signature '{}' in mod '{}'", entryMethodSignature, curAssembly.AssemblyFullName);
				success = false;
				continue;
			}
			LOG_INFO("\tGot constructor handle: {}", methodHandle);

			if (injector->TryInvokeConstructor(methodHandle, curAssembly.EntryStringArgument, &retValHandle) == false)
			{
				LOG_ERROR("Could not invoke constructor '{}' in mod '{}'", curAssembly.EntryMethodSignature.FullMethodSignature, curAssembly.AssemblyFullName);
				success = false;
				continue;
			}
			LOG_INFO("\tInvoked entry constructor!");
		}
		else
		{
			if (injector->TryFindMethodBySignature(typeHandle, entryMethodSignature, &methodHandle) == false)
			{
				LOG_ERROR("Could not find method matching signature '{}' in mod '{}'", entryMethodSignature, curAssembly.AssemblyFullName);
				success = false;
				continue;
			}
			LOG_INFO("\tGot method handle: {}", methodHandle);

			if (injector->TryInvokeMethod(methodHandle, -1, curAssembly.EntryStringArgument, &retValHandle) == false)
			{
				LOG_ERROR("Could not invoke method '{}' in mod '{}'", curAssembly.EntryMethodSignature.FullMethodSignature, curAssembly.AssemblyFullName);
				success = false;
				continue;
			}
			LOG_INFO("\tInvoked entry method!");
		}

		if ((curAssembly.IsOnAllModsLoadedMethodValid == true) && (curAssembly.OnAllModsLoadedMethodSignature.FullMethodSignature != "NULL"))
		{
			LOG_INFO("\tSearching for OnFinishedLoadingAllMods...");
			if (injector->TryFindMethodBySignature(typeHandle, entryMethodSignature, &methodHandle) == false)
			{
				LOG_ERROR("Could not find OnFinishedLoadingAllMods method in mod '{}' despite it claiming to have one", curAssembly.AssemblyFullName);
				success = false;
				continue;
			}
			LOG_INFO("\tGot OnFinishedLoadingAllMods method handle: {}", methodHandle);

			CLRMethodInvocationData newMethodInvocationData{};
			newMethodInvocationData.owningModName = curAssembly.AssemblyFullName;
			newMethodInvocationData.methodHandle = methodHandle;
			if (curAssembly.IsOnAllModsLoadedMethodStatic)
				newMethodInvocationData.objInstanceHandle = -1;
			else
				newMethodInvocationData.objInstanceHandle = retValHandle;
			onFinishedLoadingAllModsMethodCollection.push_back(newMethodInvocationData);
		}
		LOG_INFO("\tFinished loading!");
	}
	LOG_INFO("Finished loading all mods, invoking all OnFinishedLoadingAllMods methods now...");
	// now that all mods have been loaded, loop through onFinishedLoadingAllModsMethodCollection and invoke each one
	for (CLRMethodInvocationData& onFinishedLoadingAllModsMethod : onFinishedLoadingAllModsMethodCollection)
	{
		LOG_INFO("Invoking OnFinishedLoadingAllMods for mod '{}'", onFinishedLoadingAllModsMethod.owningModName);
		ManagedResourceHandle retValHandle;
		std::string throwawayStr = std::string("helloworld");
		if (injector->TryInvokeMethod(onFinishedLoadingAllModsMethod.methodHandle, onFinishedLoadingAllModsMethod.objInstanceHandle, throwawayStr, &retValHandle) == false)
		{
			LOG_ERROR("Could not invoke OnFinishedLoadingAllMods method in mod '{}'", onFinishedLoadingAllModsMethod.owningModName);
			success = false;
		}
		LOG_INFO("\tSuccessfully invoked!");
	}

cleanup:
	LOG_INFO("Restoring CLR state and cleaning up");
	if (injector->TryRestoreState() == false)
	{
		LOG_ERROR("Could not restore CLR state after injection. Things may be leaking.");
		success = false;
	}
	return success;
}