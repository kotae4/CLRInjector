#pragma once
#include "BaseCLRInjector.h"
#include "InjectionConfig.h"

#include <mscoree.h>
#include <oleauto.h>
#include <comdef.h>

#include <vector>

#import <mscorlib.tlb> raw_interfaces_only auto_rename

class NETFrameworkInjector_COMBloat :
	public BaseCLRInjector
{
private:
	ICLRMetaHost* m_pMetaHost = NULL;
	ICLRRuntimeInfo* m_pRuntimeInfo = NULL;
	ICorRuntimeHost* m_pCorRuntimeHost = NULL;
	IUnknownPtr m_AppDomainThunk = NULL;
	mscorlib::_AppDomainPtr m_AppDomain = NULL;

	bool m_IsStateValid = false;

	// NOTE:
	// since c++11, containers now correctly handle cases where the type overloads the address-of operator.
	// so this is fine. and the smart pointer will correctly release the COM resource when the vector (or its elements) goes out of scope.
	std::vector<mscorlib::_AssemblyPtr> m_LoadedAssemblies;

	std::vector<mscorlib::_TypePtr> m_Types;

	std::vector<mscorlib::_MethodInfoPtr> m_Methods;

	std::vector<mscorlib::_ConstructorInfoPtr> m_Constructors;

	std::vector<_variant_t> m_Variants;

public:
	bool TryPrepareState(std::string runtimeVersionStr);
	bool TryRestoreState();


	bool TryLoadAssembly(std::string assemblyFilePath, std::string fullyQualifiedAssemblyName, OUT ManagedResourceHandle* assembly);

	bool TryFindTypeByName(ManagedResourceHandle assemblyHandle, std::string typeName, OUT ManagedResourceHandle* type);


	bool TryBuildMethodSignature(ManagedResourceHandle methodHandle, OUT std::string& signature);
	bool TryFindMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* methodHandle);
	bool TryInvokeMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle);
	bool TryInvokeMethod(ManagedResourceHandle methodHandle, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle);


	bool TryBuildConstructorSignature(ManagedResourceHandle constructorHandle, OUT std::string& signature);
	bool TryFindConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* constructorHandle);
	bool TryInvokeConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle);
	bool TryInvokeConstructor(ManagedResourceHandle constructorHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle);


	bool Inject(InjectionConfig& config, LPCTSTR& errMsg);
};

struct COMMethodInvocationData
{
	mscorlib::_MethodInfoPtr method;
	VARIANT objInstance;
};