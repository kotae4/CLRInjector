#include "pch.h"
#include "NETFrameworkInjector_COMBloat.h"
#include "string_utils.h"
#include "process_utils.h"
#include "InjectionConfig.h"

#include <mscoree.h>
#include <oleauto.h>
#include <comdef.h>

#import <mscorlib.tlb> raw_interfaces_only auto_rename

#include "logger.h"

bool NETFrameworkInjector_COMBloat::TryPrepareState(std::string runtimeVersionStr)
{
	if (m_IsStateValid)
	{
		LOG_WARN("Tried to prepare state while state is already valid. Restoring then preparing.");
		if (TryRestoreState() == false)
			return false;
	}
	LOG_INFO("Preparing NETFramework state.");

	// convert runtimeVersionStr to a wide string for winapi
	std::wstring runtimeVersionWStr;
	if (kotae::string_utils::TryConvertUtf8ToUtf16(runtimeVersionStr, runtimeVersionWStr) == false)
	{
		LOG_ERROR("Could not convert runtimeVersionStr to wide string (str: {})", runtimeVersionStr);
		return false;
	}

	m_pMetaHost = NULL;
	m_pRuntimeInfo = NULL;
	m_pCorRuntimeHost = NULL;
	m_AppDomainThunk = NULL;
	m_AppDomain = NULL;
	HRESULT hr;

	hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&m_pMetaHost));
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get/create CLR metahost instance (error: {} [{}])", errMsg, hr);
		return false;
	}

	LOG_INFO("Got CLR metahost (addr: {})", reinterpret_cast<void*>(m_pMetaHost));

	hr = m_pMetaHost->GetRuntime(runtimeVersionWStr.c_str(), IID_PPV_ARGS(&m_pRuntimeInfo));
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get runtime instance (error: {} [{}])", errMsg, hr);
		return false;
	}

	LOG_INFO("Got RuntimeInfo (addr: {})", reinterpret_cast<void*>(m_pRuntimeInfo));

	hr = m_pRuntimeInfo->GetInterface(CLSID_CorRuntimeHost, IID_PPV_ARGS(&m_pCorRuntimeHost));
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get CorRuntimeHost (error: {} [{}])", errMsg, hr);
		return false;
	}

	LOG_INFO("Got CorRuntimeHost (addr: {})", reinterpret_cast<void*>(m_pCorRuntimeHost));

	IUnknownPtr pAppDomainThunk = NULL;
	mscorlib::_AppDomainPtr pAppDomain = NULL;
	// Get a pointer to the default AppDomain in the CLR.
	hr = m_pCorRuntimeHost->GetDefaultDomain(&m_AppDomainThunk);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get default appdomain COM object (error: {} [{}])", errMsg, hr);
		return false;
	}

	hr = m_AppDomainThunk->QueryInterface(IID_PPV_ARGS(&m_AppDomain));
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get appdomain interface from COM object (error: {} [{}])", errMsg, hr);
		return false;
	}

	LOG_INFO("Got default AppDomain (addr: {})", (void*)m_AppDomain);
	LOG_INFO("Done preparing NETFramework state.");
	m_IsStateValid = true;
	return true;
}

bool NETFrameworkInjector_COMBloat::TryRestoreState()
{
	m_IsStateValid = false;

	m_LoadedAssemblies.clear();
	m_Types.clear();
	m_Methods.clear();
	m_Constructors.clear();
	m_Variants.clear();

	if (m_AppDomainThunk != NULL)
		m_AppDomainThunk->Release();
	if (m_pCorRuntimeHost != NULL)
		m_pCorRuntimeHost->Release();
	if (m_pRuntimeInfo != NULL)
		m_pRuntimeInfo->Release();
	if (m_pMetaHost != NULL)
		m_pMetaHost->Release();

	// everything else is a smart pointer, so no worries.
	// NOTE:
	// there are some worries with m_AppDomain. it's a smart pointer, but it's unclear when it'll go out of scope and that might be bad
	// it'd also be great if i could get rid of m_AppDomainThunk and somehow just use m_AppDomain

	return true;
}

bool NETFrameworkInjector_COMBloat::TryLoadAssembly(std::string assemblyFilePath, std::string fullyQualifiedAssemblyName, OUT ManagedResourceHandle* assembly)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to load an assembly without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	mscorlib::_AssemblyPtr modAssembly = NULL;
	HRESULT hr = m_AppDomain->Load_2(_bstr_t(fullyQualifiedAssemblyName.c_str()), &modAssembly);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not load assembly into appdomain (error: {} [{}])(assembly: {})", errMsg, hr, fullyQualifiedAssemblyName);
		return false;
	}

	m_LoadedAssemblies.push_back(modAssembly);
	*assembly = m_LoadedAssemblies.size() - 1;
	LOG_INFO("Loaded assembly into appdomain (assembly: {}, index: {})", fullyQualifiedAssemblyName, (m_LoadedAssemblies.size() - 1));

	return true;
}

bool NETFrameworkInjector_COMBloat::TryFindTypeByName(ManagedResourceHandle assemblyHandle, std::string typeName, OUT ManagedResourceHandle* type)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find type by name without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}
	// 1. retrieve the AssemblyPtr from the supplied handle
	if ((assemblyHandle < 0) || (assemblyHandle >= m_LoadedAssemblies.size()))
	{
		LOG_ERROR("Assembly handle is out of bounds (handle: {}, max: {})", assemblyHandle, m_LoadedAssemblies.size());
		return false;
	}
	mscorlib::_AssemblyPtr& assembly = m_LoadedAssemblies[assemblyHandle];
	_bstr_t assemblyName;
	assembly->get_FullName(assemblyName.GetAddress());

	// 2. try to find the type with typeName parameter
	mscorlib::_TypePtr modEntryType = NULL;
	HRESULT hr = assembly->GetType_3(_bstr_t(typeName.c_str()), VARIANT_FALSE, &modEntryType);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		
		LOG_ERROR("Could not find type in assembly (error: {} [{}])(assembly: {})(type: {})", errMsg, hr, (const char*)assemblyName, typeName);
		return false;
	}

	// 3. save the type and set the handle OUT parameter
	m_Types.push_back(modEntryType);
	*type = m_Types.size() - 1;
	LOG_INFO("Found type (assembly: {}, type: {}, index: {})", (const char*)assemblyName, typeName, (m_LoadedAssemblies.size() - 1));

	return true;
}


bool NETFrameworkInjector_COMBloat::TryBuildMethodSignature(ManagedResourceHandle methodHandle, std::string& signature)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to build method signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MethodInfoPtr from the supplied handle
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	mscorlib::_MethodInfoPtr& method = m_Methods[methodHandle];

	// 2. call ToString() on the MethodInfo and convert to std::string
	_bstr_t methodSignatureBSTR;
	HRESULT hr = method->get_ToString(methodSignatureBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);

		LOG_ERROR("Could not get method signature (error: {} [{}])(methodHandle: {})", errMsg, hr, methodHandle);
		return false;
	}
	signature = std::string((char*)methodSignatureBSTR);

	return true;
}

bool NETFrameworkInjector_COMBloat::TryFindMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* methodHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find method by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the TypePtr from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Types.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Types.size());
		return false;
	}
	mscorlib::_TypePtr& type = m_Types[typeHandle];
	_bstr_t typeNameBSTR;
	HRESULT hr = type->get_FullName(typeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get typename from type while searching for methods by signature (error: {} [{}])(typeHandle: {})", errMsg, hr, typeHandle);
		return false;
	}

	// 2. iterate every method on the Type
	bool hasFoundMatch = false;
	SAFEARRAY* methods = NULL;
	hr = type->GetMethods_2(&methods);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get methods on type (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
		return false;
	}
	mscorlib::_MethodInfoPtr* pVals;
	long lowerBound, upperBound, cnt_elements;  // get array bounds
	hr = SafeArrayAccessData(methods, (void**)&pVals);
	if (FAILED(hr)) goto error;
	hr = SafeArrayGetLBound(methods, 1, &lowerBound);
	if (FAILED(hr)) goto error;
	hr = SafeArrayGetUBound(methods, 1, &upperBound);
	if (FAILED(hr)) goto error;

	cnt_elements = upperBound - lowerBound + 1;
	for (int i = 0; i < cnt_elements; ++i)
	{
		mscorlib::_MethodInfoPtr lVal = pVals[i];
		_bstr_t methodName = {};
		hr = lVal->get_ToString(methodName.GetAddress());
		if (FAILED(hr))
		{
			std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
			LOG_WARN("Could not get a method name (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
			continue;
		}
		// 3. compare name to signature parameter
		std::string methodNameStr = std::string((char*)methodName);
		if (methodNameStr.compare(signature) == 0)
		{
			// 4. save method and set methodHandle OUT parameter
			m_Methods.push_back(lVal);
			*methodHandle = m_Methods.size() - 1;
			hasFoundMatch = true;
			LOG_INFO("Found method on type '{}' corresponding to signature '{}' (index: {})", (const char*)typeNameBSTR, signature, m_Methods.size() - 1);
			break;
		}
	}
	// 5. clean up and exit
	hr = SafeArrayUnaccessData(methods);
	if (FAILED(hr)) goto error;
	// NOTE:
	// every example i've found online also does this, so i'm assuming it's safe to do.
	hr = SafeArrayDestroy(methods);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up SAFEARRAY of methods (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
		return false;
	}
	return hasFoundMatch;

error:
	std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
	LOG_ERROR("SAFEARRAY error occurred while searching all methods on type (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
	hr = SafeArrayDestroy(methods);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up SAFEARRAY of methods (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
	}
	return false;
}

bool NETFrameworkInjector_COMBloat::TryInvokeMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke method by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the TypePtr from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Types.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Types.size());
		return false;
	}
	mscorlib::_TypePtr& type = m_Types[typeHandle];
	_bstr_t typeNameBSTR;
	HRESULT hr = type->get_FullName(typeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get typename from type while invoking method by signature (error: {} [{}])(typeHandle: {})", errMsg, hr, typeHandle);
		return false;
	}

	// 2. retrieve the _variant_t from the objInstanceHandle
	_variant_t objInstance;
	if (objInstanceHandle == -1)
	{
		objInstance = {0};
	}
	else
	{
		if ((objInstanceHandle < 0) || (objInstanceHandle >= m_Variants.size()))
		{
			LOG_ERROR("objInstance handle is out of bounds (handle: {}, max: {})", objInstanceHandle, m_Variants.size());
			return false;
		}
		objInstance = m_Variants[objInstanceHandle];
	}
	

	// 3. try to find the method according to the signature parameter
	ManagedResourceHandle methodHandle = NULL;
	if (TryFindMethodBySignature(typeHandle, signature, &methodHandle) == false)
	{
		LOG_ERROR("Could not find method for invocation (type: {}, signature: {})", (const char*)typeNameBSTR, signature);
		return false;
	}

	// 4. retrieve the MethodInfoPtr from the handle
	// NOTE: we could probably make an overload of the TryFindMethodBySignature that has the MethodInfoPtr as an OUT parameter directly
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	mscorlib::_MethodInfoPtr& method = m_Methods[methodHandle];

	// 5. insert the string parameter into a SAFEARRAY
	SAFEARRAY* arguments = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	_variant_t argument(stringArgument.c_str());
	LONG argumentIndex = 0;
	hr = SafeArrayPutElement(arguments, &argumentIndex, &argument);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not insert string argument into SafeArray (error: {} [{}])(type: {}, signature: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR.GetBSTR(), signature, objInstanceHandle, stringArgument);
		return false;
	}

	// 6. invoke it
	_variant_t retValObject = {};
	hr = method->Invoke_3(objInstance, arguments, &retValObject);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not invoke method (error: {} [{}])(type: {}, signature: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR, signature, objInstanceHandle, stringArgument);
		SafeArrayDestroy(arguments);
		return false;
	}

	// 7. clean up
	hr = SafeArrayDestroy(arguments);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up method parameters SAFEARRAY (error: {} [{}])(type: {}, signature: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR, signature, objInstanceHandle, stringArgument);
		return false;
	}

	// 8. save & set the retValObject handle
	m_Variants.push_back(retValObject);
	*retValObjectHandle = m_Variants.size() - 1;
	LOG_INFO("Invoked method successfully (type: {}, signature: {}) (retVal index: {})", (const char*)typeNameBSTR, signature, m_Variants.size() - 1);

	return true;
}

bool NETFrameworkInjector_COMBloat::TryInvokeMethod(ManagedResourceHandle methodHandle, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke method without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the _variant_t from the objInstanceHandle
	_variant_t objInstance;
	if (objInstanceHandle == -1)
	{
		objInstance = {0};
	}
	else
	{
		if ((objInstanceHandle < 0) || (objInstanceHandle >= m_Variants.size()))
		{
			LOG_ERROR("objInstance handle is out of bounds (handle: {}, max: {})", objInstanceHandle, m_Variants.size());
			return false;
		}
		objInstance = m_Variants[objInstanceHandle];
	}

	// 2. retrieve the MethodInfoPtr from the handle
	// NOTE: we could probably make an overload of the TryFindMethodBySignature that has the MethodInfoPtr as an OUT parameter directly
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	mscorlib::_MethodInfoPtr& method = m_Methods[methodHandle];
	_bstr_t methodNameBSTR;
	HRESULT hr = method->get_ToString(methodNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get method name from method while invoking method (error: {} [{}])(methodHandle: {})", errMsg, hr, methodHandle);
		return false;
	}

	// 3. insert the string parameter into a SAFEARRAY
	SAFEARRAY* arguments = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	_variant_t argument(stringArgument.c_str());
	LONG argumentIndex = 0;
	hr = SafeArrayPutElement(arguments, &argumentIndex, &argument);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not insert string argument into SafeArray (error: {} [{}])(method: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)methodNameBSTR, objInstanceHandle, stringArgument);
		return false;
	}

	// 4. invoke it
	_variant_t retValObject = {};
	hr = method->Invoke_3(objInstance, arguments, &retValObject);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not invoke method (error: {} [{}])(method: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)methodNameBSTR, objInstanceHandle, stringArgument);
		SafeArrayDestroy(arguments);
		return false;
	}

	// 5. clean up
	hr = SafeArrayDestroy(arguments);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up method parameters SAFEARRAY (error: {} [{}])(method: {}, objInstanceHandle: {}, stringArgument: {})", errMsg, hr, (const char*)methodNameBSTR, objInstanceHandle, stringArgument);
		return false;
	}

	// 6. save & set the retValObject handle
	m_Variants.push_back(retValObject);
	*retValObjectHandle = m_Variants.size() - 1;
	LOG_INFO("Invoked method successfully (method: {}) (retVal index: {})", (const char*)methodNameBSTR, m_Variants.size() - 1);

	return true;
}


bool NETFrameworkInjector_COMBloat::TryBuildConstructorSignature(ManagedResourceHandle constructorHandle, OUT std::string& signature)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to build constructor signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the ConstructorInfoPtr from the supplied handle
	if ((constructorHandle < 0) || (constructorHandle >= m_Constructors.size()))
	{
		LOG_ERROR("Constructor handle is out of bounds (handle: {}, max: {})", constructorHandle, m_Constructors.size());
		return false;
	}
	mscorlib::_ConstructorInfoPtr& constructor = m_Constructors[constructorHandle];

	// 2. call ToString() on the ConstructorInfo and convert to std::string
	_bstr_t constructorSignatureBSTR;
	HRESULT hr = constructor->get_ToString(constructorSignatureBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);

		LOG_ERROR("Could not get constructor signature (error: {} [{}])(constructorHandle: {})", errMsg, hr, constructorHandle);
		return false;
	}
	signature = std::string((char*)constructorSignatureBSTR);

	return true;
}

bool NETFrameworkInjector_COMBloat::TryFindConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* constructorHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find constructor by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the TypePtr from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Types.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Types.size());
		return false;
	}
	mscorlib::_TypePtr& type = m_Types[typeHandle];
	_bstr_t typeNameBSTR;
	HRESULT hr = type->get_FullName(typeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get typename from type while searching for constructors by signature (error: {} [{}])(typeHandle: {})", errMsg, hr, typeHandle);
		return false;
	}

	// 2. iterate every constructor on the Type
	bool hasFoundMatch = false;
	SAFEARRAY* constructors = NULL;
	hr = type->GetConstructors_2(&constructors);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructors on type (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
		return false;
	}
	mscorlib::_ConstructorInfoPtr* pVals;
	long lowerBound, upperBound, cnt_elements;  // get array bounds
	hr = SafeArrayAccessData(constructors, (void**)&pVals);
	if (FAILED(hr)) goto error;
	hr = SafeArrayGetLBound(constructors, 1, &lowerBound);
	if (FAILED(hr)) goto error;
	hr = SafeArrayGetUBound(constructors, 1, &upperBound);
	if (FAILED(hr)) goto error;

	cnt_elements = upperBound - lowerBound + 1;
	for (int i = 0; i < cnt_elements; ++i)
	{
		mscorlib::_ConstructorInfoPtr lVal = pVals[i];
		_bstr_t constructorName = {};
		hr = lVal->get_ToString(constructorName.GetAddress());
		if (FAILED(hr))
		{
			std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
			LOG_WARN("Could not get a constructor name (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
			continue;
		}
		// 3. compare name to signature parameter
		std::string constructorNameStr = std::string((char*)constructorName);
		if (constructorNameStr.compare(signature) == 0)
		{
			// 4. save constructor and set constructorHandle OUT parameter
			m_Constructors.push_back(lVal);
			*constructorHandle = m_Constructors.size() - 1;
			hasFoundMatch = true;
			LOG_INFO("Found constructor on type '{}' corresponding to signature '{}' (index: {})", (const char*)typeNameBSTR, signature, m_Constructors.size() - 1);
			break;
		}
	}
	// 5. clean up and exit
	hr = SafeArrayUnaccessData(constructors);
	if (FAILED(hr)) goto error;
	// NOTE:
	// every example i've found online also does this, so i'm assuming it's safe to do.
	hr = SafeArrayDestroy(constructors);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up SAFEARRAY of constructors (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
		return false;
	}
	return hasFoundMatch;

error:
	std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
	LOG_ERROR("SAFEARRAY error occurred while searching all constructors on type (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
	hr = SafeArrayDestroy(constructors);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up SAFEARRAY of constructors (error: {} [{}])(type: {})", errMsg, hr, (const char*)typeNameBSTR);
	}
	return false;
}

bool NETFrameworkInjector_COMBloat::TryInvokeConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke constructor by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the TypePtr from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Types.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Types.size());
		return false;
	}
	mscorlib::_TypePtr& type = m_Types[typeHandle];
	_bstr_t typeNameBSTR;
	HRESULT hr = type->get_FullName(typeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get typename from type while invoking constructor by signature (error: {} [{}])(typeHandle: {})", errMsg, hr, typeHandle);
		return false;
	}

	// 2. set up default objInstance
	_variant_t objInstance = {0};

	// 3. try to find the method according to the signature parameter
	ManagedResourceHandle constructorHandle = NULL;
	if (TryFindConstructorBySignature(typeHandle, signature, &constructorHandle) == false)
	{
		LOG_ERROR("Could not find constructor for invocation (type: {}, signature: {})", (const char*)typeNameBSTR, signature);
		return false;
	}

	// 4. retrieve the ConstructorInfoPtr from the handle
	// NOTE: we could probably make an overload of the TryFindConstructorBySignature that has the ConstructorInfoPtr as an OUT parameter directly
	if ((constructorHandle < 0) || (constructorHandle >= m_Constructors.size()))
	{
		LOG_ERROR("Constructor handle is out of bounds (handle: {}, max: {})", constructorHandle, m_Constructors.size());
		return false;
	}
	mscorlib::_ConstructorInfoPtr& constructor = m_Constructors[constructorHandle];
	_bstr_t constructorNameBSTR;
	hr = constructor->get_ToString(constructorNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor name from constructor while invoking constructor (error: {} [{}])(methodHandle: {})", errMsg, hr, constructorHandle);
		return false;
	}

	// 5. get the assembly that this method comes from
	// NOTE:
	// i know this is already in m_LoadedAssemblies, but i don't want to add an assemblyHandle arg when .NET is the only CLR that needs this
	mscorlib::_TypePtr constructorType = NULL;
	hr = constructor->get_DeclaringType(&constructorType);
	if ((FAILED(hr)) || (constructorType == NULL))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor type from constructor while invoking constructor (error: {} [{}])(constructor: {})", errMsg, hr, (const char*)constructorNameBSTR);
		return false;
	}
	_bstr_t constructorTypeNameBSTR;
	hr = constructorType->get_FullName(constructorTypeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor type fullname from constructor while invoking constructor (error: {} [{}])(constructor: {})", errMsg, hr, (const char*)constructorNameBSTR);
		return false;
	}
	mscorlib::_AssemblyPtr constructorAssembly = NULL;
	hr = constructorType->get_Assembly(&constructorAssembly);
	if ((FAILED(hr)) || (constructorAssembly == NULL))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor assembly from constructor while invoking constructor (error: {} [{}])(constructor: {}, type: {})", errMsg, hr, (const char*)constructorNameBSTR, (const char*)constructorTypeNameBSTR);
		return false;
	}
	LOG_INFO("Got constructor typename: {}", (const char*)constructorTypeNameBSTR);

	// 6. insert the string parameter into a SAFEARRAY
	SAFEARRAY* arguments = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	_variant_t argument(stringArgument.c_str());
	LONG argumentIndex = 0;
	hr = SafeArrayPutElement(arguments, &argumentIndex, &argument);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not insert string argument into SafeArray (error: {} [{}])(type: {}, signature: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR, signature, stringArgument);
		return false;
	}

	// 7. invoke it
	_variant_t retValObject = {};
	//hr = constructorAssembly->CreateInstance_3(constructorTypeNameBSTR, VARIANT_FALSE, (mscorlib::BindingFlags)0, NULL, NULL, NULL, NULL, &retValObject);
	hr = constructorAssembly->CreateInstance_3(constructorTypeNameBSTR, VARIANT_FALSE, (mscorlib::BindingFlags)(static_cast<int>(mscorlib::BindingFlags::BindingFlags_CreateInstance) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_Instance) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_Public) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_NonPublic)), NULL, arguments, NULL, NULL, &retValObject);
	//hr = constructor->Invoke_3(objInstance, arguments, &retValObject);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not invoke constructor (error: {} [{}])(type: {}, signature: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR, signature, stringArgument);
		SafeArrayDestroy(arguments);
		return false;
	}

	// 8. clean up
	hr = SafeArrayDestroy(arguments);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up constructor parameters SAFEARRAY (error: {} [{}])(type: {}, signature: {}, stringArgument: {})", errMsg, hr, (const char*)typeNameBSTR, signature, stringArgument);
		return false;
	}

	// 9. save & set the retValObject handle
	m_Variants.push_back(retValObject);
	*retValObjectHandle = m_Variants.size() - 1;
	LOG_INFO("Invoked constructor successfully (type: {}, signature: {}) (retVal index: {})", (const char*)typeNameBSTR, signature, m_Variants.size() - 1);

	return true;
}

bool NETFrameworkInjector_COMBloat::TryInvokeConstructor(ManagedResourceHandle constructorHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke constructor without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. set up default objInstance
	_variant_t objInstance = {0};

	// 2. retrieve the MethodInfoPtr from the handle
	// NOTE: we could probably make an overload of the TryFindMethodBySignature that has the MethodInfoPtr as an OUT parameter directly
	if ((constructorHandle < 0) || (constructorHandle >= m_Constructors.size()))
	{
		LOG_ERROR("Constructor handle is out of bounds (handle: {}, max: {})", constructorHandle, m_Constructors.size());
		return false;
	}
	mscorlib::_ConstructorInfoPtr& constructor = m_Constructors[constructorHandle];
	_bstr_t constructorNameBSTR;
	HRESULT hr = constructor->get_ToString(constructorNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor name from constructor while invoking constructor (error: {} [{}])(methodHandle: {})", errMsg, hr, constructorHandle);
		return false;
	}

	// 3. get the assembly that this method comes from
	// NOTE:
	// i know this is already in m_LoadedAssemblies, but i don't want to add an assemblyHandle arg when .NET is the only CLR that needs this
	mscorlib::_TypePtr constructorType = NULL;
	hr = constructor->get_DeclaringType(&constructorType);
	if ((FAILED(hr)) || (constructorType == NULL))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor type from constructor while invoking constructor (error: {} [{}])(constructor: {})", errMsg, hr, (const char*)constructorNameBSTR);
		return false;
	}
	_bstr_t constructorTypeNameBSTR;
	hr = constructorType->get_FullName(constructorTypeNameBSTR.GetAddress());
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor type fullname from constructor while invoking constructor (error: {} [{}])(constructor: {})", errMsg, hr, (const char*)constructorNameBSTR);
		return false;
	}
	mscorlib::_AssemblyPtr constructorAssembly = NULL;
	hr = constructorType->get_Assembly(&constructorAssembly);
	if ((FAILED(hr)) || (constructorAssembly == NULL))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not get constructor assembly from constructor while invoking constructor (error: {} [{}])(constructor: {}, type: {})", errMsg, hr, (const char*)constructorNameBSTR, (const char*)constructorTypeNameBSTR);
		return false;
	}
	LOG_INFO("Got constructor typename: {}", (const char*)constructorTypeNameBSTR);

	// 4. insert the string parameter into a SAFEARRAY
	SAFEARRAY* arguments = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	_variant_t argument(stringArgument.c_str());
	LONG argumentIndex = 0;
	hr = SafeArrayPutElement(arguments, &argumentIndex, &argument);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not insert string argument into SafeArray (error: {} [{}])(constructor: {}, stringArgument: {})", errMsg, hr, (const char*)constructorNameBSTR, stringArgument);
		return false;
	}

	// 5. invoke it
	_variant_t retValObject = {};
	//hr = constructorAssembly->CreateInstance_3(constructorTypeNameBSTR, VARIANT_FALSE, (mscorlib::BindingFlags)0, NULL, NULL, NULL, NULL, &retValObject);
	hr = constructorAssembly->CreateInstance_3(constructorTypeNameBSTR, VARIANT_FALSE, (mscorlib::BindingFlags)(static_cast<int>(mscorlib::BindingFlags::BindingFlags_CreateInstance) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_Instance) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_Public) | static_cast<int>(mscorlib::BindingFlags::BindingFlags_NonPublic)), NULL, arguments, NULL, NULL, &retValObject);
	//hr = constructor->Invoke_3(objInstance, arguments, &retValObject);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not invoke constructor (error: {} [{}])(constructor: {}, stringArgument: {})", errMsg, hr, (const char*)constructorNameBSTR, stringArgument);
		SafeArrayDestroy(arguments);
		return false;
	}

	// 6. clean up
	hr = SafeArrayDestroy(arguments);
	if (FAILED(hr))
	{
		std::string errMsg = kotae::string_utils::GetErrorMessage(hr);
		LOG_ERROR("Could not clean up constructor parameters SAFEARRAY (error: {} [{}])(constructor: {}, stringArgument: {})", errMsg, hr, (const char*)constructorNameBSTR, stringArgument);
		return false;
	}

	// 7. save & set the retValObject handle
	m_Variants.push_back(retValObject);
	*retValObjectHandle = m_Variants.size() - 1;
	LOG_INFO("Invoked constructor successfully (constructor: {}) (retVal index: {})", (const char*)constructorNameBSTR, m_Variants.size() - 1);

	return true;
}

void PrintMethods(InjectionAssemblyInfo& curAssembly, mscorlib::_TypePtr& modEntryType)
{
	SAFEARRAY* methods = NULL;
	modEntryType->GetMethods_2(&methods);
	mscorlib::_MethodInfoPtr* pVals;
	HRESULT saHR = SafeArrayAccessData(methods, (void**)&pVals);
	if (FAILED(saHR))
	{
		kotae::string_utils::PrintErrorMessage(saHR);
		LOG_ERROR("Could not iterate methods of entry type (assembly: {}, type: {})", curAssembly.AssemblyFullName, curAssembly.EntryTypeName);
		return;
	}
	long lowerBound, upperBound;  // get array bounds
	SafeArrayGetLBound(methods, 1, &lowerBound);
	SafeArrayGetUBound(methods, 1, &upperBound);

	long cnt_elements = upperBound - lowerBound + 1;
	for (int i = 0; i < cnt_elements; ++i)  // iterate through returned values
	{
		mscorlib::_MethodInfoPtr lVal = pVals[i];
		_bstr_t methodName = {};
		lVal->get_ToString(methodName.GetAddress());
		std::string signature = std::string((char*)methodName);
		// NOTE:
		// example output: 
		// Void InjectedEntryPoint(System.String)
		// Boolean Equals(System.Object)
		// System.String ToString()
		LOG_INFO("Saw method '{}' (assembly: {})", signature, curAssembly.AssemblyFullName);
	}
	SafeArrayUnaccessData(methods);
	// NOTE:
	// every example i've found online also does this, so i'm assuming it's safe to do.
	SafeArrayDestroy(methods);
}

void PrintConstructors(InjectionAssemblyInfo& curAssembly, mscorlib::_TypePtr& modEntryType)
{
	SAFEARRAY* constructors = NULL;
	HRESULT hr = modEntryType->GetConstructors_2(&constructors);
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		_tprintf(TEXT("Could not get constructors of entry type (assembly: %S, type: %S)\n"), curAssembly.AssemblyFullName.c_str(), curAssembly.EntryTypeName.c_str());
		return;
	}
	mscorlib::_ConstructorInfoPtr* pVals;
	hr = SafeArrayAccessData(constructors, (void**)&pVals);
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		_tprintf(TEXT("Could not iterate constructors of entry type (assembly: %S, type: %S)\n"), curAssembly.AssemblyFullName.c_str(), curAssembly.EntryTypeName.c_str());
		return;
	}
	long lowerBound, upperBound;  // get array bounds
	SafeArrayGetLBound(constructors, 1, &lowerBound);
	SafeArrayGetUBound(constructors, 1, &upperBound);

	long cnt_elements = upperBound - lowerBound + 1;
	for (int i = 0; i < cnt_elements; ++i)  // iterate through returned values
	{
		mscorlib::_ConstructorInfoPtr lVal = pVals[i];
		_bstr_t constructorName = {};
		lVal->get_ToString(constructorName.GetAddress());
		// NOTE:
		// example output:
		// Void .ctor(System.String)
		// Void .ctor()
		_tprintf(TEXT("Saw constructor '%s' (assembly: %S)\n"), constructorName.GetBSTR(), curAssembly.AssemblyFullName.c_str());
	}
	SafeArrayUnaccessData(constructors);
	SafeArrayDestroy(constructors);
}

bool NETFrameworkInjector_COMBloat::Inject(InjectionConfig& config, LPCTSTR& errMsg)
{
	ICLRMetaHost* pMetaHost = NULL;
	ICLRRuntimeInfo* pRuntimeInfo = NULL;
	ICorRuntimeHost* pCorRuntimeHost = NULL;
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
	hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&pMetaHost));
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get/create CLR metahost instance");
		_tprintf(TEXT("Could not get/create CLR metahost instance"));
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got MetaHost [%tx]\n"), pMetaHost);

	hr = pMetaHost->GetRuntime(configRuntimeVersionW.c_str(), IID_PPV_ARGS(&pRuntimeInfo));
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get runtime instance");
		_tprintf(TEXT("Could not get runtime instance"));
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got RuntimeInfo [%tx]\n"), pRuntimeInfo);

	hr = pRuntimeInfo->GetInterface(CLSID_CorRuntimeHost, IID_PPV_ARGS(&pCorRuntimeHost));
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get runtime interface");
		_tprintf(TEXT("Could not get runtime interface"));
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Got CorRuntimeHost [%tx]\n"), pCorRuntimeHost);

	IUnknownPtr pAppDomainThunk = NULL;
	mscorlib::_AppDomainPtr pAppDomain = NULL;
	// Get a pointer to the default AppDomain in the CLR.
	hr = pCorRuntimeHost->GetDefaultDomain(&pAppDomainThunk);
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get default appdomain COM object");
		_tprintf(TEXT("Could not get default appdomain COM object"));
		return false;
	}
	hr = pAppDomainThunk->QueryInterface(IID_PPV_ARGS(&pAppDomain));
	if (FAILED(hr))
	{
		kotae::string_utils::PrintErrorMessage(hr);
		errMsg = TEXT("Could not get appdomain interface from COM object");
		_tprintf(TEXT("Could not get appdomain interface from COM object"));
		return false;
	}

	_tprintf(TEXT("[Bootstrapper] Prepared the CLR, executing managed DLL(s) now\n"));
	// this collection will keep all of the "OnFinishedLoadingAllMods" methods present in mods. once we finish loading all mods, we'll iterate this collection and invoke each one.
	std::vector<COMMethodInvocationData> onFinishedLoadingAllModsMethodCollection{};
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
		_tprintf(TEXT("Loaded assembly config:\n\tAssemblyPath=%S\n\tAssemblyFullName=%S\n\tInjectEntryTypeName=%S\n\tInjectEntryMethodName=%S\n\tInjectEntryStringArgument=%S\n\tInjectEntryIsClassConstructor=%d\n"),
			curAssembly.AssemblyPath.c_str(), curAssembly.AssemblyFullName.c_str(), curAssembly.EntryTypeName.c_str(), curAssembly.EntryMethodName.c_str(), curAssembly.EntryStringArgument.c_str(), curAssembly.IsEntryClassConstructor);

		// NOTE:
		// EITHER
		// read the assembly from disk into memory
		// copy the in-memory assembly into a SAFEARRAY
		// call pAppDomain->Load_3 passing in the SAFEARRAY
		// OR
		// pAppDomain->Load_2 probably won't work without passing in the fully qualified assembly name, which would require more json parsing
		// adding in the json parsing shouldn't be too difficult, and may actually be better if mono doesn't support loading from memory... more research to do.
		// TO-DO:
		// release all COM stuff... have to research which ones need to be freed / released
		mscorlib::_AssemblyPtr modAssembly = NULL;
		hr = pAppDomain->Load_2(_bstr_t(curAssembly.AssemblyFullName.c_str()), &modAssembly);
		//hr = pRuntimeHost->ExecuteInDefaultAppDomain(curAssemblyPathW.c_str(), curInjectEntryTypeNameW.c_str(), curInjectEntryMethodNameW.c_str(), curInjectEntryStringArgumentW.c_str(), &InjectedLibraryResult);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			_tprintf(TEXT("Could not load mod assembly into appdomain (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			errMsg = TEXT("Could not load mod assembly into appdomain");
			return false;
		}
		_tprintf(TEXT("[Bootstrapper] loaded mod assembly, searching for entrypoint now...\n"));
		mscorlib::_TypePtr modEntryType = NULL;
		hr = modAssembly->GetType_3(_bstr_t(curAssembly.EntryTypeName.c_str()), VARIANT_FALSE, &modEntryType);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not find entry type in mod assembly");
			_tprintf(TEXT("Could not find entry type in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		_variant_t modEntryObjInstance = {};
		_variant_t modEntryRetVal = {};
		if (curAssembly.IsEntryClassConstructor)
		{
			PrintConstructors(curAssembly, modEntryType);
			errMsg = TEXT("Could not find matching constructor in mod assembly");
			_tprintf(TEXT("Could not find matching constructor in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		else
		{
			PrintMethods(curAssembly, modEntryType);
			mscorlib::_MethodInfoPtr modEntryMethod = NULL;
			hr = modEntryType->GetMethod_6(_bstr_t(curAssembly.EntryMethodName.c_str()), &modEntryMethod);
			if (FAILED(hr))
			{
				kotae::string_utils::PrintErrorMessage(hr);
				errMsg = TEXT("Could not find entry method in mod assembly");
				_tprintf(TEXT("Could not find entry method in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
				return false;
			}
			if (modEntryMethod == NULL)
			{
				errMsg = TEXT("Supplied entry method does not exist in mod assembly");
				_tprintf(TEXT("Supplied entry method does not exist in mod assembly (assembly: %S, method: %S)\n"), curAssembly.AssemblyFullName.c_str(), curAssembly.EntryMethodName.c_str());
				return false;
			}
			SAFEARRAY* modEntryArguments = SafeArrayCreateVector(VT_VARIANT, 0, 1);
			_variant_t argument(curAssembly.EntryStringArgument.c_str());
			LONG argumentIndex = 0;
			hr = SafeArrayPutElement(modEntryArguments, &argumentIndex, &argument);
			if (FAILED(hr))
			{
				kotae::string_utils::PrintErrorMessage(hr);
				errMsg = TEXT("Could not insert string argument into SafeArray");
				_tprintf(TEXT("Could not insert string argument into SafeArray (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
				return false;
			}
			hr = modEntryMethod->Invoke_3(modEntryObjInstance, modEntryArguments, &modEntryRetVal);
			if (FAILED(hr))
			{
				kotae::string_utils::PrintErrorMessage(hr);
				errMsg = TEXT("Could not invoke entry method in mod assembly");
				_tprintf(TEXT("Could not invoke entry method in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
				return false;
			}

			if (curAssembly.IsEntryClassConstructor)
				modEntryObjInstance = modEntryRetVal;

			hr = SafeArrayDestroy(modEntryArguments);
			if (FAILED(hr))
			{
				kotae::string_utils::PrintErrorMessage(hr);
				errMsg = TEXT("Could not clean up SAFEARRAY parameters");
				_tprintf(TEXT("Could not clean up SAFEARRAY parameters (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
				return false;
			}
		}

		_tprintf(TEXT("[Bootstrapper] executed (hr: %tx)\n"), hr);
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
			_tprintf(TEXT("Could not execute injected DLL's entrypoint (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}

		// before moving to next mod, check to see if it implements "OnFinishedLoadingAllMods" method and add it to a collection
		_tprintf(TEXT("[Bootstrapper] Checking if mod implements an OnFinishedLoadingAllMods method...\n"));
		// TO-DO:
		// crashes some point after this. figure out where.
		mscorlib::_MethodInfoPtr modOnFinishedLoadingModsMethod = NULL;
		hr = modEntryType->GetMethod_6(_bstr_t(ON_FINISHED_LOADING_ADDONS_METHOD_NAME), &modOnFinishedLoadingModsMethod);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not find OnFinishedLoadingAllMods method in mod assembly");
			_tprintf(TEXT("Could not find OnFinishedLoadingAllMods method in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		if (modOnFinishedLoadingModsMethod == NULL)
		{
			_tprintf(TEXT("There was no OnFinishedLoadingAllMods method in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			continue;
		}
		_tprintf(TEXT("[Bootstrapper] Saw OnFinishedLoadingAllMods method, now checking if it's of the proper signature...\n"));
		SAFEARRAY* onFinishedLoadingModsParameters = NULL;
		hr = modOnFinishedLoadingModsMethod->GetParameters(&onFinishedLoadingModsParameters);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not get parameters of OnFinishedLoadingAllMods method in mod assembly");
			_tprintf(TEXT("Could not get parameters of OnFinishedLoadingAllMods method in mod assembly (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		_tprintf(TEXT("[Bootstrapper] Checking OnFinishedLoadingAllMods method, parameters OK...\n"));
		if ((onFinishedLoadingModsParameters->cDims > 1) || (onFinishedLoadingModsParameters->rgsabound[0].cElements > 0))
		{
			errMsg = TEXT("OnFinishedLoadingAllMods method has parameters. Expected zero parameters.");
			_tprintf(TEXT("OnFinishedLoadingAllMods method has parameters. Expected zero parameters. (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		// NOTE:
		// WARNING:
		// is this necessary? hopefully we aren't destroying internal runtime data. it's not ref counted so it really is just destroyed.
		SafeArrayDestroy(onFinishedLoadingModsParameters);

		VARIANT_BOOL isAbstract = NULL;
		hr = modOnFinishedLoadingModsMethod->get_IsAbstract(&isAbstract);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not determine whether OnFinishedLoadingAllMods method is abstract or concrete. Expected it to be non-abstract and have zero parameters.");
			_tprintf(TEXT("Could not determine whether OnFinishedLoadingAllMods method is abstract or concrete. Expected it to be non-abstract and have zero parameters. (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		_tprintf(TEXT("[Bootstrapper] Checking OnFinishedLoadingAllMods method, IsAbstract OK...\n"));
		if ((isAbstract == VARIANT_TRUE) || (isAbstract == TRUE))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("OnFinishedLoadingAllMods is abstract. Expected it to be non-abstract and have zero parameters.");
			_tprintf(TEXT("OnFinishedLoadingAllMods is abstract. Expected it to be non-abstract and have zero parameters. (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}

		VARIANT_BOOL isStatic = NULL;
		hr = modOnFinishedLoadingModsMethod->get_IsStatic(&isStatic);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not determine whether OnFinishedLoadingAllMods method is static or instance.");
			_tprintf(TEXT("Could not determine whether OnFinishedLoadingAllMods method is static or instance. (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
			return false;
		}
		_tprintf(TEXT("[Bootstrapper] Checking OnFinishedLoadingAllMods method, IsStatic OK...\n"));
		_tprintf(TEXT("[Bootstrapper] Creating invocation data for OnFinishedLoadingAllMods method and adding to collection...\n"));
		COMMethodInvocationData newMethodInvocationData = {};
		newMethodInvocationData.method = modOnFinishedLoadingModsMethod;
		if ((isStatic == VARIANT_TRUE) || (isStatic == TRUE))
		{
			_variant_t nullVariant = {};
			nullVariant.vt = VT_NULL;
			nullVariant.plVal = NULL;
			newMethodInvocationData.objInstance = nullVariant;
		}
		else
		{
			if (curAssembly.IsEntryClassConstructor == false)
			{
				errMsg = TEXT("OnFinishedLoadingAllMods method is an instance method but the mod entry method was static. Cannot invoke OnFinishedLoadingAllMods because we have no class object instance.");
				_tprintf(TEXT("OnFinishedLoadingAllMods method is an instance method but the mod entry method was static. Cannot invoke OnFinishedLoadingAllMods because we have no class object instance. (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
				return false;
			}
			else
			{
				newMethodInvocationData.objInstance = modEntryRetVal;
			}
		}

		onFinishedLoadingAllModsMethodCollection.push_back(newMethodInvocationData);
		_tprintf(TEXT("[Bootstrapper] Done loading mod (assembly: %S)\n"), curAssembly.AssemblyFullName.c_str());
	}
	_tprintf(TEXT("[INFO]: Done loading mod(s)! Invoking OnFinishedLoadingAllMods for each mod that has implemented it\n"));
	// now that all mods are loaded, iterate our collection of "OnFinishedLoadingAllMods" methods and invoke each one
	_variant_t onFinishedLoadingAllModsRetVal = {0};
	for (COMMethodInvocationData& onFinishedLoadingAllModsMethod : onFinishedLoadingAllModsMethodCollection)
	{
		hr = onFinishedLoadingAllModsMethod.method->Invoke_3(onFinishedLoadingAllModsMethod.objInstance, NULL, &onFinishedLoadingAllModsRetVal);
		if (FAILED(hr))
		{
			kotae::string_utils::PrintErrorMessage(hr);
			errMsg = TEXT("Could not invoke OnFinishedLoadingAllMods method.");
			_tprintf(TEXT("Could not invoke OnFinishedLoadingAllMods method.\n"));
			return false;
		}
		_tprintf(TEXT("[INFO]: Invoked OnFinishedLoadingAllMods (%p, %p)\n"), onFinishedLoadingAllModsMethod.method, onFinishedLoadingAllModsMethod.objInstance);
	}

	// everything else is a smart pointer, so no worries.
	if (pAppDomainThunk != NULL)
		pAppDomainThunk->Release();
	if (pCorRuntimeHost != NULL)
		pCorRuntimeHost->Release();
	if (pRuntimeInfo != NULL)
		pRuntimeInfo->Release();
	if (pMetaHost != NULL)
		pMetaHost->Release();

	return true;
}