#pragma once
#include "BaseCLRInjector.h"
#include "InjectionConfig.h"

class NETInjector :
    public BaseCLRInjector
{
private:
    // .NET definitions (framework 4.x, probably core and beyond too)
    typedef HRESULT(__stdcall* CLRCreateInstance_t)(REFCLSID clsid, REFIID riid, LPVOID* ppInterface);
    typedef HRESULT(__stdcall* ICLRMetaHost_GetRuntime_t)(ICLRMetaHost* _this, LPCWSTR pwzVersion, REFIID riid, LPVOID* ppRuntime);
    typedef HRESULT(__stdcall* ICLRRuntimeInfo_GetInterface_t)(ICLRRuntimeInfo* _this, REFCLSID rclsid, REFIID riid, LPVOID* ppUnk);
    typedef HRESULT(__stdcall* ICLRRuntimeHost_ExecuteInDefaultAppDomain_t)(ICLRRuntimeHost* _this, LPCWSTR pwzAssemblyPath, LPCWSTR pwzTypeName, LPCWSTR pwzMethodName, LPCWSTR pwzArgument, DWORD* pReturnValue);

    // func ptrs that we'll be using, loaded via Prepare() method using GetProcAddress winapi func.
    CLRCreateInstance_t oCLRCreateInstance;
    ICLRMetaHost_GetRuntime_t oMetaHost_GetRuntime;
    ICLRRuntimeInfo_GetInterface_t oRuntimeInfo_GetInterface;
    ICLRRuntimeHost_ExecuteInDefaultAppDomain_t oRuntimeHost_ExecuteInDefaultAppDomain;

private:
    bool Prepare();
public:
    bool Inject(InjectionConfig& config, LPCTSTR& errMsg);
};