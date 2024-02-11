#pragma once

#include <string>

namespace kotae
{
	namespace string_utils
	{
		// credits: someone on stackoverflow, probably (sorry didn't bookmark)
		void PrintErrorMessage(HRESULT error);
		VOID PrintLastError();

		std::string GetErrorMessage(HRESULT error);

		// credit: https://docs.microsoft.com/en-us/archive/msdn-magazine/2016/september/c-unicode-encoding-conversions-with-stl-strings-and-win32-apis
		BOOL TryConvertUtf8ToUtf16(const std::string& utf8, OUT std::wstring& outWStr);
		BOOL TryConvertUtf16ToUtf8(const std::wstring& utf16, OUT std::string& outStr);

		// credit: https://stackoverflow.com/a/29752943
		void replaceAll(std::string& source, const std::string& from, const std::string& to);
	}
}