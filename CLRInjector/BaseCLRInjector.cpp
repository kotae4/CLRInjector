#include "pch.h"
#include "BaseCLRInjector.h"

FARPROC BaseCLRInjector::LoadFPointer(HMODULE lib, LPCSTR funcName, LPCTSTR errorMsg)
{
	FARPROC retVal = GetProcAddress(lib, funcName);
	if (retVal == NULL)
		MessageBox(NULL, errorMsg, TEXT("CLRInjector"), MB_OK | MB_ICONERROR | MB_TOPMOST);
	return retVal;
}