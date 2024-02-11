#include "pch.h"
#include "MonoInjector.h"
#include "process_utils.h"
// for counting
#include <algorithm>

#include "logger.h"

bool MonoInjector::Prepare()
{
	HMODULE hMono = NULL;
	LPCTSTR errMsg;
	int numRetries = 0, maxRetries = 30000;
	while (hMono == NULL)
	{
		if (kotae::process_utils::TryFindModuleByName(TEXT("mono"), &hMono, errMsg) == true)
			break;

		numRetries++;
		//_tprintf(TEXT("Could not get handle to mono library: %s (attempt %d/%d)\n"), errMsg, numRetries, maxRetries);
		if (numRetries > maxRetries)
		{
			_tprintf(TEXT("Failed to get handle to mono library within maximum retries. Is the process using mono?"));
			return false;
		}
		Sleep(10);
	}

	mono_security_set_mode = (mono_security_set_mode_t)LoadFPointer(hMono, "mono_security_set_mode", TEXT("[ERROR]: mono_security_set_mode is NULL"));
	if (mono_security_set_mode == NULL)
		return false;
	mono_get_root_domain = (mono_get_root_domain_t)LoadFPointer(hMono, "mono_get_root_domain", TEXT("[ERROR]: mono_get_root_domain is NULL"));
	if (mono_get_root_domain == NULL)
		return false;
	mono_thread_attach = (mono_thread_attach_t)LoadFPointer(hMono, "mono_thread_attach", TEXT("[ERROR]: mono_thread_attach is NULL"));
	if (mono_thread_attach == NULL)
		return false;
	mono_thread_detach = (mono_thread_detach_t)LoadFPointer(hMono, "mono_thread_detach", TEXT("[ERROR]: mono_thread_detach is NULL"));
	if (mono_thread_detach == NULL)
		return false;
	mono_domain_get = (mono_domain_get_t)LoadFPointer(hMono, "mono_domain_get", TEXT("[ERROR]: mono_domain_get is NULL"));
	if (mono_domain_get == NULL)
		return false;
	mono_domain_assembly_open = (mono_domain_assembly_open_t)LoadFPointer(hMono, "mono_domain_assembly_open", TEXT("[ERROR]: mono_domain_assembly_open is NULL"));
	if (mono_domain_assembly_open == NULL)
		return false;
	mono_assembly_get_image = (mono_assembly_get_image_t)LoadFPointer(hMono, "mono_assembly_get_image", TEXT("[ERROR]: mono_assembly_get_image is NULL"));
	if (mono_assembly_get_image == NULL)
		return false;
	mono_class_from_name = (mono_class_from_name_t)LoadFPointer(hMono, "mono_class_from_name", TEXT("[ERROR]: mono_class_from_name is NULL"));
	if (mono_class_from_name == NULL)
		return false;
	mono_class_get_method_from_name = (mono_class_get_method_from_name_t)LoadFPointer(hMono, "mono_class_get_method_from_name", TEXT("[ERROR]: mono_class_get_method_from_name is NULL"));
	if (mono_class_get_method_from_name == NULL)
		return false;
	mono_class_get_methods = (mono_class_get_methods_t)LoadFPointer(hMono, "mono_class_get_methods", TEXT("[ERROR]: mono_class_get_methods is NULL"));
	if (mono_class_get_methods == NULL)
		return false;
	mono_string_new = (mono_string_new_t)LoadFPointer(hMono, "mono_string_new", TEXT("[ERROR]: mono_string_new is NULL"));
	if (mono_string_new == NULL)
		return false;
	mono_object_new = (mono_object_new_t)LoadFPointer(hMono, "mono_object_new", TEXT("[ERROR]: mono_object_new is NULL"));
	if (mono_object_new == NULL)
		return false;
	mono_runtime_object_init = (mono_runtime_object_init_t)LoadFPointer(hMono, "mono_runtime_object_init", TEXT("[ERROR]: mono_runtime_object_init is NULL"));
	if (mono_runtime_object_init == NULL)
		return false;
	mono_runtime_invoke = (mono_runtime_invoke_t)LoadFPointer(hMono, "mono_runtime_invoke", TEXT("[ERROR]: mono_runtime_invoke is NULL"));
	if (mono_runtime_invoke == NULL)
		return false;

	// classes
	mono_class_get_name = (mono_class_get_name_t)LoadFPointer(hMono, "mono_class_get_name", TEXT("[ERROR]: mono_class_get_name is NULL"));
	if (mono_class_get_name == NULL)
		return false;

	// methods
	mono_method_full_name = (mono_method_full_name_t)LoadFPointer(hMono, "mono_method_full_name", TEXT("[ERROR]: mono_method_full_name is NULL"));
	if (mono_method_full_name == NULL)
		return false;
	mono_method_get_class = (mono_method_get_class_t)LoadFPointer(hMono, "mono_method_get_class", TEXT("[ERROR]: mono_method_get_class is NULL"));
	if (mono_method_get_class == NULL)
		return false;

	// signatures
	mono_method_signature = (mono_method_signature_t)LoadFPointer(hMono, "mono_method_signature", TEXT("[ERROR]: mono_method_signature is NULL"));
	if (mono_method_signature == NULL)
		return false;
	mono_signature_is_instance = (mono_signature_is_instance_t)LoadFPointer(hMono, "mono_signature_is_instance", TEXT("[ERROR]: mono_signature_is_instance is NULL"));
	if (mono_signature_is_instance == NULL)
		return false;
	mono_signature_get_param_count = (mono_signature_get_param_count_t)LoadFPointer(hMono, "mono_signature_get_param_count", TEXT("[ERROR]: mono_signature_get_param_count is NULL"));
	if (mono_signature_get_param_count == NULL)
		return false;
	mono_signature_full_name = (mono_signature_full_name_t)LoadFPointer(hMono, "mono_signature_full_name", TEXT("[ERROR]: mono_signature_full_name is NULL"));
	if (mono_signature_full_name == NULL)
		return false;

	// images
	mono_image_get_name = (mono_image_get_name_t)LoadFPointer(hMono, "mono_image_get_name", TEXT("[ERROR]: mono_image_get_name is NULL"));
	if (mono_image_get_name == NULL)
		return false;

	// general
	g_free = (g_free_t)LoadFPointer(hMono, "g_free", TEXT("[WARNING]: g_free is NULL"));
	if (g_free == NULL)
	{
		g_free = (g_free_t)LoadFPointer(hMono, "mono_unity_g_free", TEXT("[ERROR]: mono_unity_g_free is NULL"));
		if (g_free == NULL)
			return false;
	}

	m_AreFunctionsLoaded = true;

	return true;
}

bool MonoInjector::TryPrepareState(std::string runtimeVersionStr)
{
	if (m_AreFunctionsLoaded == false)
	{
		if (Prepare() == false)
		{
			LOG_ERROR("Could not load mono function pointers");
			return false;
		}
	}

	if (m_IsStateValid)
	{
		LOG_WARN("Tried to prepare state while state is already valid. Restoring then preparing.");
		if (TryRestoreState() == false)
			return false;
	}

	LOG_INFO("Preparing mono state.");

	m_MonoMainThread = mono_thread_attach(mono_get_root_domain());
	mono_security_set_mode(0);
	m_RootDomain = mono_domain_get();

	m_IsStateValid = true;
	return true;
}

bool MonoInjector::TryRestoreState()
{
	m_IsStateValid = false;

	m_Images.clear();
	m_Classes.clear();
	m_Methods.clear();
	m_Objects.clear();

	mono_thread_detach(m_MonoMainThread);
	// can't reset security mode because that'd break the hooking API
	//mono_security_set_mode(1);

	return true;
}


bool MonoInjector::TryLoadAssembly(std::string assemblyFilePath, std::string fullyQualifiedAssemblyName, OUT ManagedResourceHandle* imageHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to load an assembly without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	PVOID monoAssembly = mono_domain_assembly_open(m_RootDomain, assemblyFilePath.c_str());
	PVOID monoImage = mono_assembly_get_image(monoAssembly);

	m_Images.push_back(monoImage);
	*imageHandle = m_Images.size() - 1;
	LOG_INFO("Loaded assembly into appdomain (assembly: {}, index: {})", fullyQualifiedAssemblyName, (m_Images.size() - 1));

	return true;
}

bool MonoInjector::TryFindTypeByName(ManagedResourceHandle assemblyHandle, std::string fullTypeName, OUT ManagedResourceHandle* classHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find type by name without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoImage from the supplied handle
	if ((assemblyHandle < 0) || (assemblyHandle >= m_Images.size()))
	{
		LOG_ERROR("Image handle is out of bounds (handle: {}, max: {})", assemblyHandle, m_Images.size());
		return false;
	}
	PVOID& monoImage = m_Images[assemblyHandle];
	const char* assemblyName = mono_image_get_name(monoImage);

	// 2. extract the parts from the fullTypeName
	std::string namespaceStr = "";
	std::string typeNameStr = fullTypeName;
	ExtractNamespaceAndTypeFromFullTypename(fullTypeName, namespaceStr, typeNameStr);

	// 3. try to get the type from within the image
	PVOID monoClass = mono_class_from_name(monoImage, namespaceStr.c_str(), typeNameStr.c_str());

	// 4. save the type and set the handle OUT parameter
	m_Classes.push_back(monoClass);
	*classHandle = m_Classes.size() - 1;
	LOG_INFO("Found type (assembly: {}, type: {}, index: {})", assemblyName, fullTypeName, (m_Classes.size() - 1));

	return true;
}


bool MonoInjector::TryBuildMethodSignature(ManagedResourceHandle methodHandle, OUT std::string& signature)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to build method signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoMethod from the supplied handle
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	PVOID& method = m_Methods[methodHandle];

	// 2. call ToString() on the MethodInfo and convert to std::string
	char* methodNameCStr = mono_method_full_name(method, true);
	signature = std::string(methodNameCStr);
	g_free(methodNameCStr);

	return true;
}

bool MonoInjector::TryFindMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* methodHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find method by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoClass from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Classes.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Classes.size());
		return false;
	}
	PVOID& monoClass = m_Classes[typeHandle];
	const char* className = mono_class_get_name(monoClass);

	// 2. iterate over every method
	bool hasFoundMatch = false;
	PVOID iter = NULL;
	PVOID method = mono_class_get_methods(monoClass, &iter);
	while (method != NULL)
	{
		char* methodNameCStr = mono_method_full_name(method, true);
		std::string methodName = std::string(methodNameCStr);
		g_free(methodNameCStr);

		// 3. compare name to signature parameter
		if (methodName.compare(signature) == 0)
		{
			// 4. save method and set methodHandle OUT parameter
			m_Methods.push_back(method);
			*methodHandle = m_Methods.size() - 1;
			hasFoundMatch = true;
			LOG_INFO("Found method on type '{}' corresponding to signature '{}' (index: {})", className, signature, m_Methods.size() - 1);
			break;
		}

		method = mono_class_get_methods(monoClass, &iter);
	}

	return hasFoundMatch;
}

bool MonoInjector::TryInvokeMethodBySignature(ManagedResourceHandle typeHandle, std::string& signature, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke method by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoClass from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Classes.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Classes.size());
		return false;
	}
	PVOID monoClass = m_Classes[typeHandle];
	const char* className = mono_class_get_name(monoClass);

	// 2. retrieve the MonoObject from the objInstanceHandle
	PVOID objInstance;
	if (objInstanceHandle == -1)
	{
		objInstance = NULL;
	}
	else
	{
		if ((objInstanceHandle < 0) || (objInstanceHandle >= m_Objects.size()))
		{
			LOG_ERROR("objInstance handle is out of bounds (handle: {}, max: {})", objInstanceHandle, m_Objects.size());
			return false;
		}
		objInstance = m_Objects[objInstanceHandle];
	}


	// 3. try to find the method according to the signature parameter
	ManagedResourceHandle methodHandle = NULL;
	if (TryFindMethodBySignature(typeHandle, signature, &methodHandle) == false)
	{
		LOG_ERROR("Could not find method for invocation (type: {}, signature: {})", className, signature);
		return false;
	}

	// 4. retrieve the MonoMethod from the handle
	// NOTE: we could probably make an overload of the TryFindMethodBySignature that has the MethodInfoPtr as an OUT parameter directly
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	PVOID& method = m_Methods[methodHandle];

	// 5. prepare the string argument
	PVOID monoStringArg = mono_string_new(m_RootDomain, stringArgument.c_str());
	void* args[1];
	args[0] = monoStringArg;

	// 6. invoke it
	PVOID retValObject = mono_runtime_invoke(method, objInstance, args, NULL);

	// 7. save & set the retValObject handle
	m_Objects.push_back(retValObject);
	*retValObjectHandle = m_Objects.size() - 1;
	LOG_INFO("Invoked method successfully (type: {}, signature: {}) (retVal index: {})", className, signature, m_Objects.size() - 1);

	return true;
}

bool MonoInjector::TryInvokeMethod(ManagedResourceHandle methodHandle, ManagedResourceHandle objInstanceHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke method without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoMethod from the supplied handle
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	PVOID& method = m_Methods[methodHandle];
	char* methodNameCStr = mono_method_full_name(method, true);
	std::string methodName = std::string(methodNameCStr);
	g_free(methodNameCStr);

	// 2. retrieve the MonoObject from the objInstanceHandle
	PVOID objInstance;
	if (objInstanceHandle == -1)
	{
		objInstance = NULL;
	}
	else
	{
		if ((objInstanceHandle < 0) || (objInstanceHandle >= m_Objects.size()))
		{
			LOG_ERROR("objInstance handle is out of bounds (handle: {}, max: {})", objInstanceHandle, m_Objects.size());
			return false;
		}
		objInstance = m_Objects[objInstanceHandle];
	}

	// 3. prepare the string argument
	PVOID monoStringArg = mono_string_new(m_RootDomain, stringArgument.c_str());
	void* args[1];
	args[0] = monoStringArg;

	// 4. invoke it
	PVOID retValObject = mono_runtime_invoke(method, objInstance, args, NULL);

	// 5. save & set the retValObject handle
	m_Objects.push_back(retValObject);
	*retValObjectHandle = m_Objects.size() - 1;
	LOG_INFO("Invoked method successfully (method: {}) (retVal index: {})", methodName, m_Objects.size() - 1);

	return true;
}


bool MonoInjector::TryBuildConstructorSignature(ManagedResourceHandle constructorHandle, OUT std::string& signature)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to build constructor signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	return TryBuildMethodSignature(constructorHandle, signature);
}

bool MonoInjector::TryFindConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, OUT ManagedResourceHandle* constructorHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to find constructor by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	return TryFindMethodBySignature(typeHandle, signature, constructorHandle);
}

bool MonoInjector::TryInvokeConstructorBySignature(ManagedResourceHandle typeHandle, std::string& signature, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke constructor by signature without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoClass from the supplied handle
	if ((typeHandle < 0) || (typeHandle >= m_Classes.size()))
	{
		LOG_ERROR("Type handle is out of bounds (handle: {}, max: {})", typeHandle, m_Classes.size());
		return false;
	}
	PVOID monoClass = m_Classes[typeHandle];
	const char* className = mono_class_get_name(monoClass);

	// 2. allocate memory for the object
	PVOID objInstance = mono_object_new(m_RootDomain, monoClass);

	// 3. try to find the method according to the signature parameter
	ManagedResourceHandle methodHandle = NULL;
	if (TryFindMethodBySignature(typeHandle, signature, &methodHandle) == false)
	{
		LOG_ERROR("Could not find constructor for invocation (type: {}, signature: {})", className, signature);
		return false;
	}

	// 4. retrieve the MonoMethod from the handle
	// NOTE: we could probably make an overload of the TryFindMethodBySignature that has the MethodInfoPtr as an OUT parameter directly
	if ((methodHandle < 0) || (methodHandle >= m_Methods.size()))
	{
		LOG_ERROR("Constructor handle is out of bounds (handle: {}, max: {})", methodHandle, m_Methods.size());
		return false;
	}
	PVOID& method = m_Methods[methodHandle];

	// 5. prepare the string argument
	PVOID monoStringArg = mono_string_new(m_RootDomain, stringArgument.c_str());
	void* args[1];
	args[0] = monoStringArg;

	// 6. invoke it
	// NOTE:
	// constructors return void, so retValObject should be NULL
	PVOID retValObject = mono_runtime_invoke(method, objInstance, args, NULL);

	// 7. save & set retValObjectHandle
	// NOTE:
	// the actual object instance that was constructed is objInstance, NOT retValObject
	m_Objects.push_back(objInstance);
	*retValObjectHandle = m_Objects.size() - 1;
	LOG_INFO("Invoked constructor successfully (type: {}, signature: {}) (retVal index: {})", className, signature, m_Objects.size() - 1);

	return true;
}

bool MonoInjector::TryInvokeConstructor(ManagedResourceHandle constructorHandle, std::string& stringArgument, OUT ManagedResourceHandle* retValObjectHandle)
{
	if (m_IsStateValid == false)
	{
		LOG_ERROR("Tried to invoke constructor without preparing CLR state. Must call TryPrepareState(...) and have it succeed before doing anything else.");
		return false;
	}

	// 1. retrieve the MonoMethod from the supplied handle
	if ((constructorHandle < 0) || (constructorHandle >= m_Methods.size()))
	{
		LOG_ERROR("Method handle is out of bounds (handle: {}, max: {})", constructorHandle, m_Methods.size());
		return false;
	}
	PVOID& method = m_Methods[constructorHandle];
	char* methodNameCStr = mono_method_full_name(method, true);
	std::string methodName = std::string(methodNameCStr);
	g_free(methodNameCStr);
	PVOID monoClass = mono_method_get_class(method);

	// 2. allocate memory for the object
	PVOID objInstance = mono_object_new(m_RootDomain, monoClass);

	// 3. prepare the string argument
	PVOID monoStringArg = mono_string_new(m_RootDomain, stringArgument.c_str());
	void* args[1];
	args[0] = monoStringArg;

	// 4. invoke it
	// NOTE:
	// constructors return void, so retValObject should be NULL
	PVOID retValObject = mono_runtime_invoke(method, objInstance, args, NULL);

	// 5. save & set the retValObject handle
	// NOTE:
	// the actual object instance that was constructed is objInstance, NOT retValObject
	m_Objects.push_back(objInstance);
	*retValObjectHandle = m_Objects.size() - 1;
	LOG_INFO("Invoked method successfully (method: {}) (retVal index: {})", methodName, m_Objects.size() - 1);

	return true;
}


void MonoInjector::ExtractNamespaceAndTypeFromFullTypename(std::string fullTypename, OUT std::string& namespaceStr, OUT std::string& typeStr)
{
	namespaceStr = "";
	typeStr = fullTypename;
	size_t numPeriodsInTypename = std::count(fullTypename.begin(), fullTypename.end(), '.');
	if (numPeriodsInTypename > 0)
	{
		size_t lastPeriodIndex = fullTypename.find_last_of('.');
		namespaceStr = fullTypename.substr(0, lastPeriodIndex);
		typeStr = fullTypename.substr(lastPeriodIndex + 1, fullTypename.length() - (lastPeriodIndex + 1));
		LOG_INFO("Saw {} periods in fully qualified typename, so split it at index {} to get '{}' and '{}'", numPeriodsInTypename, lastPeriodIndex, namespaceStr, typeStr);
	}
}

void MonoInjector::PrintMethods(PVOID monoClass)
{
	// mono_class_num_methods 
	// MonoMethod* mono_class_get_methods (MonoClass* klass, gpointer *iter)
	PVOID iter = NULL;
	PVOID method = mono_class_get_methods(monoClass, &iter);
	while (method != NULL)
	{
		char* methodNameCStr = mono_method_full_name(method, true);
		std::string methodName = std::string(methodNameCStr);
		g_free(methodNameCStr);

		PVOID monoMethodSignature = mono_method_signature(method);
		char* sigFullName = mono_signature_full_name(monoMethodSignature);
		std::string signature = std::string(sigFullName);
		g_free(sigFullName);

		LOG_INFO("Saw method '{}' with signature '{}'", methodName, signature);
		method = mono_class_get_methods(monoClass, &iter);
	}
}

bool MonoInjector::Inject(InjectionConfig& config, LPCTSTR& errMsg)
{
	if (Prepare() == false)
	{
		errMsg = TEXT("Could not find fptr's for mono injection");
		return false;
	}

	_tprintf(TEXT("[INFO]: All set, attaching to thread\n"));
	PVOID monoThread = mono_thread_attach(mono_get_root_domain());
	_tprintf(TEXT("[INFO]: attached to mono root thread\n"));
	// necessary for hooking API
	mono_security_set_mode(0);
	// load all the mods
	PVOID monoDomain = mono_domain_get();
	_tprintf(TEXT("[INFO]: Disabled mono security, loading mod(s) now...\n"));
	// this collection will keep all of the "OnFinishedLoadingAllMods" methods present in mods. once we finish loading all mods, we'll iterate this collection and invoke each one.
	std::vector<MethodInvocationData> onFinishedLoadingAllModsMethodCollection{};
	for (std::vector<InjectionAssemblyInfo>::iterator it = config.Assemblies.begin(); it != config.Assemblies.end(); ++it)
	{
		InjectionAssemblyInfo curAssembly = *it;

		// InjectEntryTypeName is supposed to be the fully qualified typename, so we want to split at the last period to separate the namespace and the typename
		std::string curNamespace = "";
		std::string curTypeName = curAssembly.EntryTypeName;
		ExtractNamespaceAndTypeFromFullTypename(curAssembly.EntryTypeName, curNamespace, curTypeName);

		_tprintf(TEXT("[INFO]: Loading mod ('%S.%S::%S' with arg %S)\n"), curNamespace.c_str(), curTypeName.c_str(), curAssembly.EntryMethodName.c_str(), curAssembly.EntryStringArgument.c_str());
		MessageBox(NULL, TEXT("Hello world!"), TEXT("MonoInjector"), 0);

		PVOID monoAssembly = mono_domain_assembly_open(monoDomain, curAssembly.AssemblyPath.c_str());
		PVOID monoImage = mono_assembly_get_image(monoAssembly);
		PVOID monoClass = mono_class_from_name(monoImage, curNamespace.c_str(), curTypeName.c_str());

		// debug
		PrintMethods(monoClass);

		PVOID monoMethod = mono_class_get_method_from_name(monoClass, curAssembly.EntryMethodName.c_str(), -1);
		// prepare the string argument
		PVOID monoStringArg = mono_string_new(monoDomain, curAssembly.EntryStringArgument.c_str());
		void* args[1];
		args[0] = monoStringArg;
		// prepare the object instance
		PVOID injectClassInstance = NULL;
		if (curAssembly.IsEntryClassConstructor)
		{
			// the target method is a class constructor, so create & initialize the object before invoking the instance method
			injectClassInstance = mono_object_new(monoDomain, monoClass);
			// invoke the method, but with the object instance this time since constructors are instance methods
			_tprintf(TEXT("[INFO]: Loaded mod and instantiated class instance, invoking constructor method '%S.%S::%S' with arg (%S [%p]) now\n"), curNamespace.c_str(), curTypeName.c_str(), curAssembly.EntryMethodName.c_str(), curAssembly.EntryStringArgument.c_str(), args[0]);
			mono_runtime_invoke(monoMethod, injectClassInstance, args, NULL);
		}
		else
		{
			_tprintf(TEXT("[INFO]: Loaded mod, invoking static method '%S.%S::%S' with arg (%S [%p]) now\n"), curNamespace.c_str(), curTypeName.c_str(), curAssembly.EntryMethodName.c_str(), curAssembly.EntryStringArgument.c_str(), args[0]);
			mono_runtime_invoke(monoMethod, NULL, args, NULL);
		}
		_tprintf(TEXT("[INFO]: Invoked mod\n"));
		// before moving to the next assembly, let's try to see if this mod implements an "OnFinishedLoadingAllMods" method
		// this isn't required, so the loader won't fail if it's not present, but i thought it might be useful for interop between mods.
		PVOID onFinishedLoadingAddonsMethod = mono_class_get_method_from_name(monoClass, ON_FINISHED_LOADING_ADDONS_METHOD_NAME, 0);
		if (onFinishedLoadingAddonsMethod != NULL)
		{
			PVOID methodSig = mono_method_signature(onFinishedLoadingAddonsMethod);
			UINT paramCount = mono_signature_get_param_count(methodSig);
			if (paramCount > 0)
			{
				_tprintf(TEXT("[WARNING]: Saw %d parameters in %S.%S::OnFinishedLoadingAllMods method, expected 0\n"), paramCount, curNamespace.c_str(), curTypeName.c_str());
				continue;
			}
			bool isInstanceMethod = mono_signature_is_instance(methodSig);
			MethodInvocationData newMethodInvocationData = {};
			newMethodInvocationData.method = onFinishedLoadingAddonsMethod;
			if (isInstanceMethod)
				newMethodInvocationData.objInstance = injectClassInstance;
			else
				newMethodInvocationData.objInstance = NULL;

			onFinishedLoadingAllModsMethodCollection.push_back(newMethodInvocationData);
		}
	}

	_tprintf(TEXT("[INFO]: Done loading mod(s)! Invoking OnFinishedLoadingAllMods for each mod that has implemented it\n"));
	// now that all mods are loaded, iterate our collection of "OnFinishedLoadingAllMods" methods and invoke each one
	for (MethodInvocationData& onFinishedLoadingAllModsMethod : onFinishedLoadingAllModsMethodCollection)
	{
		mono_runtime_invoke(onFinishedLoadingAllModsMethod.method, onFinishedLoadingAllModsMethod.objInstance, NULL, NULL);
		_tprintf(TEXT("[INFO]: Invoked OnFinishedLoadingAllMods (%p, %p)\n"), onFinishedLoadingAllModsMethod.method, onFinishedLoadingAllModsMethod.objInstance);
	}
	_tprintf(TEXT("[INFO]: Done! Detaching from mono root thread now.\n"));
	// can't re-enable security because that'd break the hooking API, but usually you'd do so here.
	//mono_security_set_mode(1);
	mono_thread_detach(monoThread);
	_tprintf(TEXT("[INFO]: Done pulling self up by bootstraps! Exiting now.\n"));
	return true;
}