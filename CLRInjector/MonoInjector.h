#pragma once
#include "BaseCLRInjector.h"
#include "InjectionConfig.h"

#include <vector>

class MonoInjector :
	public BaseCLRInjector
{
private:
	// mono definitions
	typedef VOID(*mono_security_set_mode_t)(DWORD mode);
	typedef PVOID(*mono_get_root_domain_t)();
	typedef PVOID(*mono_thread_attach_t)(PVOID domain);
	typedef VOID(*mono_thread_detach_t)(PVOID thread);
	typedef PVOID(*mono_domain_get_t)();
	typedef PVOID(*mono_domain_assembly_open_t)(PVOID domain, const char* targFile);
	typedef PVOID(*mono_assembly_get_image_t)(PVOID assembly);
	typedef PVOID(*mono_class_from_name_t)(PVOID image, const char* targNamespace, const char* targClass);
	typedef PVOID(*mono_class_get_method_from_name_t)(PVOID targClass, const char* targMethod, DWORD param_count);
	// NOTE:
	// this does see constructors too!!!
	typedef PVOID(*mono_class_get_methods_t)(PVOID targClass, PVOID* iter);
	typedef PVOID(*mono_string_new_t)(PVOID domain, const char* text);
	typedef PVOID(*mono_object_new_t)(PVOID domain, PVOID targClass);
	typedef VOID(*mono_runtime_object_init_t)(PVOID objInstance);
	typedef PVOID(*mono_runtime_invoke_t)(PVOID method, PVOID instance, PVOID* params, PVOID exc);
	// classes
	// NOTE:
	// should not be freed
	typedef const char* (*mono_class_get_name_t)(PVOID monoClass);
	// methods
	// NOTE:
	// example output (passing true for the signature arg):
	// kotae4.UnityModdingFramework.Bootstrap.Loader:Load (string)
	// kotae4.UnityModdingFramework.Bootstrap.Loader:.ctor ()
	typedef char* (*mono_method_full_name_t)(PVOID method, bool signature);
	typedef PVOID(*mono_method_get_class_t)(PVOID method);
	// for double-checking method signatures...
	typedef PVOID(*mono_method_signature_t)(PVOID method);
	typedef bool (*mono_signature_is_instance_t)(PVOID methodSignature);
	typedef UINT32(*mono_signature_get_param_count_t)(PVOID methodSignature);
	// NOTE:
	// signature doesn't contain the typename or namespace. it's just the return type and parameter types.
	// example output: void(string)
	typedef char* (*mono_signature_full_name_t)(PVOID methodSignature);
	// images
	typedef const char* (*mono_image_get_name_t)(PVOID image);
	// general
	typedef void(*g_free_t)(PVOID data);

	

	mono_security_set_mode_t mono_security_set_mode;
	mono_get_root_domain_t mono_get_root_domain;
	mono_thread_attach_t mono_thread_attach;
	mono_thread_detach_t mono_thread_detach;
	mono_domain_get_t mono_domain_get;
	mono_domain_assembly_open_t mono_domain_assembly_open;
	mono_assembly_get_image_t mono_assembly_get_image;
	mono_class_from_name_t mono_class_from_name;
	mono_class_get_method_from_name_t mono_class_get_method_from_name;
	mono_class_get_methods_t mono_class_get_methods;
	mono_string_new_t mono_string_new;
	mono_object_new_t mono_object_new;
	mono_runtime_object_init_t mono_runtime_object_init;
	mono_runtime_invoke_t mono_runtime_invoke;
	// classes
	mono_class_get_name_t mono_class_get_name;
	// methods
	mono_method_full_name_t mono_method_full_name;
	mono_method_get_class_t mono_method_get_class;
	// signatures
	mono_method_signature_t mono_method_signature;
	mono_signature_is_instance_t mono_signature_is_instance;
	mono_signature_get_param_count_t mono_signature_get_param_count;
	mono_signature_full_name_t mono_signature_full_name;
	// images
	mono_image_get_name_t mono_image_get_name;
	// general
	g_free_t g_free;

	// TO-DO:
	// mono_assembly_getrootdir
	// mono_assembly_setrootdir

	bool m_AreFunctionsLoaded = false;
	bool m_IsStateValid = false;

	PVOID m_MonoMainThread = NULL;
	PVOID m_RootDomain = NULL;


	std::vector<PVOID> m_Images;
	std::vector<PVOID> m_Classes;
	std::vector<PVOID> m_Methods;
	std::vector<PVOID> m_Objects;


private:
	bool Prepare();
	void ExtractNamespaceAndTypeFromFullTypename(std::string fullTypename, OUT std::string& namespaceStr, OUT std::string& typeStr);
	void PrintMethods(PVOID monoClass);
	void PrintConstructors(PVOID monoClass);
public:

	bool TryPrepareState(std::string runtimeVersionStr);
	bool TryRestoreState();


	bool TryLoadAssembly(std::string assemblyFilePath, std::string fullyQualifiedAssemblyName, OUT ManagedResourceHandle* imageHandle);

	bool TryFindTypeByName(ManagedResourceHandle assemblyHandle, std::string fullTypeName, OUT ManagedResourceHandle* classHandle);


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

struct MethodInvocationData
{
	PVOID method;
	PVOID objInstance;
};