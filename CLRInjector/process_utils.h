#pragma once
namespace kotae
{
	namespace process_utils
	{
		bool IsCurrentProcess64bit();
		bool TryFindModuleByName(LPCTSTR moduleName, HMODULE* outModule, LPCTSTR& errMsg);
	}
}

