#pragma once

#include "pch.h"
#include "InjectionConfig.h"

VOID Inject();
VOID Eject(DWORD exitCode, LPCTSTR reason, UINT icon);
VOID RegisterConsole();

