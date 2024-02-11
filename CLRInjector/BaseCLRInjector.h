#pragma once

#include "InjectionConfig.h"

// TO-DO:
// write wrappers for resources (types, methodinfos, etc).
// wrapper can be a simple int that indexes into an array of all the resources this class manages.
// this array can be instantiated and pre-populated when TryPrepareState() is called,
// and then iterated and have all its resources released when TryRestoreState() is called.
// a separate counter variable is necessary for each array to ensure indexes are always up to date.
typedef size_t ManagedResourceHandle;

class BaseCLRInjector
{
protected:
	FARPROC LoadFPointer(HMODULE lib, LPCSTR funcName, LPCTSTR errorMsg);
public:
	/// <summary>
	/// Opens the CLR and grabs the relevant COM interfaces. Also initializes some internal data structures.
	/// </summary>
	/// <returns></returns>
	virtual bool TryPrepareState(std::string runtimeVersionStr) = 0;
	/// <summary>
	/// Releases the COM interfaces.
	/// </summary>
	/// <returns></returns>
	virtual bool TryRestoreState() = 0;

	/// <summary>
	/// Prepares the CLR to load the assembly into the managed environment from the file
	/// </summary>
	/// <param name="assemblyFilePath"></param>
	/// <param name="fullyQualifiedAssemblyName"></param>
	/// <returns></returns>
	virtual bool TryLoadAssembly(std::string assemblyFilePath, std::string fullyQualifiedAssemblyName, OUT ManagedResourceHandle* assembly) = 0;

	/// <summary>
	/// Attempts to find the managed type by its full type name (including namespace) in the assembly.
	/// </summary>
	/// <param name="assembly"></param>
	/// <param name="typeName"></param>
	/// <returns></returns>
	virtual bool TryFindTypeByName(ManagedResourceHandle assemblyHandle, std::string typeName, OUT ManagedResourceHandle* type) = 0;


	virtual bool TryBuildMethodSignature(ManagedResourceHandle methodHandle, OUT std::string& signature) = 0;
	virtual bool TryFindMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* methodHandle) = 0;
	virtual bool TryInvokeMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle) = 0;
	virtual bool TryInvokeMethod(ManagedResourceHandle methodHandle, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle) = 0;


	virtual bool TryBuildConstructorSignature(ManagedResourceHandle constructorHandle, OUT std::string& signature) = 0;
	virtual bool TryFindConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* constructorHandle) = 0;
	virtual bool TryInvokeConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle) = 0;
	virtual bool TryInvokeConstructor(ManagedResourceHandle constructorHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle) = 0;

	// debug
	virtual bool Inject(InjectionConfig& config, LPCTSTR& errMsg) = 0;
};

struct CLRMethodInvocationData
{
	ManagedResourceHandle methodHandle;
	ManagedResourceHandle objInstanceHandle;
	std::string owningModName;
};