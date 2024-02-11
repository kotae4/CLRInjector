#include "pch.h"

#include "InjectionConfig.h"
#include "string_utils.h"
#include "logger.h"

bool InjectionConfig::ReadFile(const std::string& filename)
{
	std::ifstream myfile(filename);
	if (myfile.is_open())
	{
		// [unsafe] assumptions:
		// line 1: specifying whether mono or net injection is desired ["mono" or "net"]
		// line 2: runtime version (only really used for net CLR, but expected regardless) ["v4.0.30319"]
		// line 3: number of assemblies present in the config file
		// from this point on, each assembly that is to be injected provides its information on 4 separate lines:
		// line x % 4 == 0: "path/to/mod-assembly.dll"
		// line x % 5 == 0: full type name in mod-assembly.dll ["MyCompany.MyProject.InjectedLibrary.InjectedClass"]
		// line x % 6 == 0: method to act as entrypoint ["MyModEntrypoint"]
		// line x % 7 == 0: a string argument to pass to the entrypoint ["HelloFromInjector"]
		// OR
		// we can just use json :)
		nlohmann::json config = nlohmann::json::parse(myfile, nullptr, false);
		myfile.close();
		if (config.is_discarded())
		{
			return false;
		}

		this->CLRType = config.at("CLRType").get<ECLRType>();
		this->RuntimeVersion = config.at("RuntimeVersion").get<std::string>();
		this->Assemblies = config.at("Assemblies").get<std::vector<InjectionAssemblyInfo>>();

		return true;
	}
	else
	{
		return false;
	}
}

std::string InjectionMethodSignature::GetNETSignature()
{
	// desired output:
	// Void InjectedEntryPoint(System.String)
	// Boolean Equals(System.Object)
	// System.String ToString()
	// Void .ctor(System.String)
	// Void .ctor()

	// TO-DO:
	// 1. separate the return type
	size_t firstSpaceIndex = FullMethodSignature.find_first_of(' ');
	size_t lastColonIndex = FullMethodSignature.find_last_of(':');
	size_t firstParenthesisIndex = FullMethodSignature.find_first_of('(');
	if ((firstSpaceIndex == std::string::npos) || (lastColonIndex == std::string::npos) || (firstParenthesisIndex == std::string::npos))
	{
		LOG_ERROR("Input does not match expected format (got: {})", FullMethodSignature);
		return "ERROR";
	}
	std::string retType = FullMethodSignature.substr(0, firstSpaceIndex);
	// 2. separate the method name (discarding the namespace.typename entirely)
	if (lastColonIndex > firstParenthesisIndex)
	{
		LOG_ERROR("Input does not match expected format (got: {})", FullMethodSignature);
		return "ERROR";
	}
	std::string methodName = FullMethodSignature.substr(lastColonIndex + 1, (((firstParenthesisIndex - 1) - lastColonIndex + 1) - 1));
	// 3. separate the parameter list
	// NOTE:
	// we'll copy the entire parameter list, *including* the parenthesis
	std::string parameterList = FullMethodSignature.substr(firstParenthesisIndex, FullMethodSignature.size() - firstParenthesisIndex);
	// 4. replace 'System.Void' with just 'Void', 'System.Boolean' with just 'Boolean'.
	kotae::string_utils::replaceAll(retType, "System.Void", "Void");
	kotae::string_utils::replaceAll(parameterList, "System.Void", "Void");
	// 5. construct signature by piecing together the separated parts according to desired output format
	std::string retSignature = retType + " " + methodName + parameterList;
	LOG_INFO("Constructed .NET method signature: '{}' (was: '{}')", retSignature, FullMethodSignature);
	return retSignature;
}

std::string InjectionMethodSignature::GetMonoSignature()
{
	// desired output:
	// kotae4.UnityModdingFramework.Bootstrap.Loader:Load (string)
	// kotae4.UnityModdingFramework.Bootstrap.Loader:.ctor ()

	// TO-DO:
	// 1. separate the namespace.typename (discard the return type entirely)
	size_t firstSpaceIndex = FullMethodSignature.find_first_of(' ');
	size_t lastColonIndex = FullMethodSignature.find_last_of(':');
	size_t firstParenthesisIndex = FullMethodSignature.find_first_of('(');
	if ((firstSpaceIndex == std::string::npos) || (lastColonIndex == std::string::npos) || (firstParenthesisIndex == std::string::npos))
	{
		LOG_ERROR("Input does not match expected format (got: {})", FullMethodSignature);
		return "ERROR";
	}
	std::string fullTypeName = FullMethodSignature.substr(firstSpaceIndex + 1, (lastColonIndex - 2) - (firstSpaceIndex + 1));
	// 2. separate the method name
	std::string methodName = FullMethodSignature.substr(lastColonIndex + 1, (((firstParenthesisIndex - 1) - lastColonIndex + 1) - 1));
	// 3. separate the parameter list
	std::string parameterList = FullMethodSignature.substr(firstParenthesisIndex, FullMethodSignature.size() - firstParenthesisIndex);
	// 4. replace 'System.String' with just 'string'
	kotae::string_utils::replaceAll(parameterList, "System.String", "string");
	// 5. construct signature by piecing together the separated parts according to desired output format
	std::string retSignature = fullTypeName + ":" + methodName + " " + parameterList;
	LOG_INFO("Constructed mono method signature: '{}' (was: '{}')", retSignature, FullMethodSignature);
	return retSignature;
}