#pragma once

#include "pch.h"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

#include "logger.h"

#define ON_FINISHED_LOADING_ADDONS_METHOD_NAME "OnFinishedLoadingAllMods"

struct InjectionAssemblyInfo;

enum class ECLRType : int
{
	NET,
	Mono,
	Invalid = -1
};

NLOHMANN_JSON_SERIALIZE_ENUM(ECLRType, {
	{ECLRType::Invalid, nullptr},
	{ECLRType::NET, "NET"},
	{ECLRType::Mono, "Mono"},
	})

class InjectionConfig
{
public:
	ECLRType CLRType;
	std::string RuntimeVersion;
	std::vector<InjectionAssemblyInfo> Assemblies;

	bool ReadFile(const std::string& filename);
};

struct InjectionMethodSignature
{
	// example input (from mono.cecil):
	// System.Void kotae4.UnityModdingFramework.Bootstrap.Loader::.ctor()
	// System.Void kotae4.UnityModdingFramework.Bootstrap.Loader::.ctor(System.String)
	// System.Boolean kotae4.UnityModdingFramework.Bootstrap.Loader::Derp(System.Object)
	// System.Void kotae4.UnityModdingFramework.Bootstrap.Loader::Load(System.String)
	// System.Void InjectedLibrary.InjectedClass::.ctor(System.String)
	// System.Void InjectedLibrary.InjectedClass::.ctor()

	// NOTE:
	// in all cases, the accessibility modifier is omitted. only the return type, namespace.typename, method name, and parameters are relevant.

	std::string FullMethodSignature;

	InjectionMethodSignature(std::string _FullMethodSignature)
	{
		LOG_INFO("Parametered constructor called");
		FullMethodSignature = _FullMethodSignature;
	}

	InjectionMethodSignature()
	{
		LOG_INFO("Default constructor called");
		FullMethodSignature = "ERROR";
	}

	std::string GetNETSignature();
	std::string GetMonoSignature();
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InjectionMethodSignature, FullMethodSignature)

struct InjectionAssemblyInfo
{
	std::string AssemblyPath;
	std::string AssemblyFullName;
	std::string EntryTypeName;
	std::string EntryMethodName;
	InjectionMethodSignature EntryMethodSignature;
	std::string EntryStringArgument;
	bool IsEntryClassConstructor;

	InjectionMethodSignature OnAllModsLoadedMethodSignature;
	bool IsOnAllModsLoadedMethodStatic;
	bool IsOnAllModsLoadedMethodValid;


	InjectionAssemblyInfo(std::string _AssemblyPath, std::string _AssemblyFullName, 
		std::string _EntryTypeName, std::string _EntryMethodName, InjectionMethodSignature _EntryMethodSignature, std::string _EntryStringArgument, bool _IsEntryClassConstructor,
		InjectionMethodSignature _OnAllModsLoadedMethodSignature, bool _IsOnAllModsLoadedMethodStatic, bool _IsOnAllModsLoadedMethodValid)
	{
		LOG_INFO("Parametered constructor called");
		AssemblyPath = _AssemblyPath;
		AssemblyFullName = _AssemblyFullName;
		EntryTypeName = _EntryTypeName;
		EntryMethodName = _EntryMethodName;
		EntryMethodSignature = _EntryMethodSignature;
		EntryStringArgument = _EntryStringArgument;
		IsEntryClassConstructor = _IsEntryClassConstructor;

		OnAllModsLoadedMethodSignature = _OnAllModsLoadedMethodSignature;
		IsOnAllModsLoadedMethodStatic = _IsOnAllModsLoadedMethodStatic;
		IsOnAllModsLoadedMethodValid = _IsOnAllModsLoadedMethodValid;
	}

	InjectionAssemblyInfo()
	{
		LOG_INFO("Default constructor called");
		AssemblyPath = "ERROR";
		AssemblyFullName = "ERROR";
		EntryTypeName = "ERROR";
		EntryMethodName = "ERROR";
		EntryMethodSignature = InjectionMethodSignature("ERROR");
		EntryStringArgument = "ERROR";
		IsEntryClassConstructor = false;
		OnAllModsLoadedMethodSignature = InjectionMethodSignature("ERROR");
		IsOnAllModsLoadedMethodStatic = false;
		IsOnAllModsLoadedMethodValid = false;
	}
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InjectionAssemblyInfo, AssemblyPath, AssemblyFullName, EntryTypeName, EntryMethodName, EntryMethodSignature, EntryStringArgument, IsEntryClassConstructor, OnAllModsLoadedMethodSignature, IsOnAllModsLoadedMethodStatic, IsOnAllModsLoadedMethodValid)