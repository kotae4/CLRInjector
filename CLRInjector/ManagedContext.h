#pragma once

#include "InjectionConfig.h"
#include "NETFrameworkInjector_COMBloat.h"
#include "MonoInjector.h"

class ManagedContext
{
public:
	bool TryInject(InjectionConfig& config);
	// WISHLIST:
	/*
	// this would eliminate the need to pass the CLR type via the config file.
	// ultimately, we might be able to get rid of the config file entirely.
	bool TryGetProcessCLRType(OUT ECLRType* type);
	// installing a CLR would give mod authors the ease of C# even when the game is entirely native (like unreal engine games, or countless others)
	bool TryInstallCLR(ECLRType& type);
	*/
};