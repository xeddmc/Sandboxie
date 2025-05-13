/*
 * Copyright 2004-2020 Sandboxie Holdings, LLC 
 * Copyright 2020-2024 David Xanatos, xanasoft.com
 *
 * This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//---------------------------------------------------------------------------
// Service Control Manager
//---------------------------------------------------------------------------

#include "dll.h"

#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include "dll.h"
#include "common/win32_ntddk.h"
#include "core/svc/ServiceWire.h"
#include "common/my_version.h"
#include "../../apps/com/header.h" //SC_HANDLE_...


//---------------------------------------------------------------------------
// Defines
//---------------------------------------------------------------------------


#define HANDLE_SERVICE_MANAGER          ((SC_HANDLE)0x12340001)
#define HANDLE_SERVICE_STATUS           ((SERVICE_STATUS_HANDLE)0x12340003)
#define HANDLE_SERVICE_LOCK             ((SC_LOCK)0x12340005)
#define HANDLE_EVENT_LOG                ((SC_LOCK)0x12340007)


//---------------------------------------------------------------------------
// Functions
//---------------------------------------------------------------------------


static BOOLEAN Scm_HookRegisterServiceCtrlHandler(HMODULE module);

//---------------------------------------------------------------------------

static SC_HANDLE Scm_OpenSCManagerA(
    UCHAR *lpMachineName,
    UCHAR *lpDatabaseName,
    DWORD dwDesiredAccess);

static SC_HANDLE Scm_OpenSCManagerW(
    WCHAR *lpMachineName,
    WCHAR *lpDatabaseName,
    DWORD dwDesiredAccess);

static SC_HANDLE Scm_OpenServiceA(
    SC_HANDLE hSCManager,
    const UCHAR *lpServiceName,
    DWORD dwDesiredAccess);

static SC_HANDLE Scm_OpenServiceWImpl(
    SC_HANDLE hSCManager,
    const WCHAR *lpServiceName,
    DWORD dwDesiredAccess);

static SC_HANDLE Scm_OpenServiceW(
    SC_HANDLE hSCManager,
    const WCHAR *lpServiceName,
    DWORD dwDesiredAccess);

static BOOL Scm_CloseServiceHandleImpl(SC_HANDLE hSCObject);

static BOOL Scm_CloseServiceHandle(SC_HANDLE hSCObject);


//---------------------------------------------------------------------------

static ULONG_PTR Scm_SubscribeServiceChangeNotifications(
    ULONG_PTR Unknown1, ULONG_PTR Unknown2, ULONG_PTR Unknown3,
    ULONG_PTR Unknown4, ULONG_PTR Unknown5);
	

//---------------------------------------------------------------------------


static WCHAR *Scm_GetHandleName(SC_HANDLE hService);

static BOOLEAN Scm_IsBoxedService(const WCHAR *ServiceName);

static WCHAR *Scm_GetBoxedServices(void);

static WCHAR *Scm_GetAllServices(void);

static void Scm_DiscardKeyCache(const WCHAR *ServiceName);

static BOOLEAN Scm_DllHack(HMODULE module, const WCHAR *svcname);


//---------------------------------------------------------------------------
// Prototypes
//---------------------------------------------------------------------------


typedef SC_HANDLE (*P_OpenSCManager)(
    void *lpMachineName, const void *lpDatabaseName, DWORD dwDesiredAccess);

typedef SC_HANDLE (*P_OpenService)(
    SC_HANDLE hSCManager, const void *lpServiceName, DWORD dwDesiredAccess);

typedef BOOL (*P_CloseServiceHandle)(
    SC_HANDLE hSCObject);

typedef BOOL (*P_QueryServiceStatus)(
    SC_HANDLE hSCObject, SERVICE_STATUS *lpServiceStatus);

typedef BOOL (*P_QueryServiceStatusEx)(
    SC_HANDLE hService, SC_STATUS_TYPE InfoLevel,
    LPBYTE lpBuffer, DWORD cbBufSize, LPDWORD pcbBytesNeeded);

typedef BOOL (*P_QueryServiceConfig)(
    SC_HANDLE hService, void *lpServiceConfig,
    DWORD cbBufSize, LPDWORD pcbBytesNeeded);

typedef BOOL (*P_QueryServiceConfig2)(
    SC_HANDLE hService, DWORD InfoLevel, LPBYTE lpBuffer,
    DWORD cbBufSize, LPDWORD pcbBytesNeeded);

typedef BOOL (*P_EnumServicesStatus)(
    SC_HANDLE hSCManager, DWORD dwServiceType, DWORD dwServiceState,
    void *lpServices, DWORD cbBufSize, LPDWORD pcbBytesNeeded,
    LPDWORD lpServicesReturned, LPDWORD lpResumeHandle);

typedef BOOL (*P_EnumServicesStatusEx)(
    SC_HANDLE hSCManager, SC_ENUM_TYPE InfoLevel,
    DWORD dwServiceType, DWORD dwServiceState,
    void *lpServices, DWORD cbBufSize, LPDWORD pcbBytesNeeded,
    LPDWORD lpServicesReturned, LPDWORD lpResumeHandle, void *GroupName);

typedef BOOL (*P_QueryServiceLockStatus)(
    SC_HANDLE hService, void *lpLockStatus,
    DWORD cbBufSize, LPDWORD pcbBytesNeeded);

typedef BOOL (*P_GetServiceDisplayName)(
    SC_HANDLE hSCManager, WCHAR *lpServiceName,
    WCHAR *lpDisplayName, LPDWORD lpcchBuffer);

typedef BOOL (*P_GetServiceKeyName)(
    SC_HANDLE hSCManager, WCHAR *lpDisplayName,
    WCHAR *lpServiceName, LPDWORD lpcchBuffer);

typedef BOOL (*P_EnumDependentServices)(
    SC_HANDLE hService, DWORD dwServiceState,
    LPENUM_SERVICE_STATUS lpServices, DWORD cbBufSize,
    LPDWORD pcbBytesNeeded, LPDWORD lpServicesReturned);

typedef BOOL (*P_QueryServiceObjectSecurity)(
    SC_HANDLE hService,
    SECURITY_INFORMATION dwSecurityInformation,
    PSECURITY_DESCRIPTOR lpSecurityDescriptor,
    DWORD cbBufSize, LPDWORD pcbBytesNeeded);

typedef BOOL (*P_SetServiceObjectSecurity)(
    SC_HANDLE hService,
    SECURITY_INFORMATION dwSecurityInformation,
    PSECURITY_DESCRIPTOR lpSecurityDescriptor);


//---------------------------------------------------------------------------

typedef ULONG_PTR (*P_SubscribeServiceChangeNotifications)(
    ULONG_PTR Unknown1, ULONG_PTR Unknown2, ULONG_PTR Unknown3,
    ULONG_PTR Unknown4, ULONG_PTR Unknown5); // ret 14h


//---------------------------------------------------------------------------


typedef SC_LOCK (*P_LockServiceDatabase)(SC_HANDLE hSCManager);

typedef BOOL (*P_UnlockServiceDatabase)(SC_LOCK ScLock);

typedef BOOL (*P_CreateService)(
    SC_HANDLE hSCManager,
    void *lpServiceName, void *lpDisplayName, ULONG dwDesiredAccess,
    ULONG dwServiceType, ULONG dwStartType, ULONG dwErrorControl,
    void *lpBinaryPathName, void *lpLoadOrderGroup, void *lpdwTagId,
    void *lpDependencies, void *lpServiceStartName, void *lpPassword);

typedef BOOL (*P_ChangeServiceConfig)(
    SC_HANDLE hService,
    ULONG dwServiceType, ULONG dwStartType, ULONG dwErrorControl,
    void *lpBinaryPathName, void *lpLoadOrderGroup,
    void *lpdwTagId, void *lpDependencies,
    void *lpServiceStartName, void *lpPassword, void *lpDisplayName);

typedef BOOL (*P_ChangeServiceConfig2)(
    SC_HANDLE hService, ULONG dwInfoLevel, void *lpInfo);

typedef BOOL (*P_DeleteService)(SC_HANDLE hService);

typedef BOOL (*P_StartService)(
    SC_HANDLE hService, DWORD dwNumServiceArgs, void *lpServiceArgVector);

typedef BOOL (*P_StartServiceCtrlDispatcher)(const void *ServiceTable);

typedef SERVICE_STATUS_HANDLE (*P_RegisterServiceCtrlHandler)(
    void *ServiceName, void *HandlerProc);

typedef SERVICE_STATUS_HANDLE (*P_RegisterServiceCtrlHandlerEx)(
    void *ServiceName, void *HandlerProc, void *Context);

typedef BOOL (*P_SetServiceStatus)(
    SERVICE_STATUS_HANDLE hServiceStatus, SERVICE_STATUS *ServiceStatus);

typedef BOOL (*P_ControlService)(
    SC_HANDLE hService, DWORD dwControl, SERVICE_STATUS *lpServiceStatus);

typedef BOOL (*P_ControlServiceEx)(
    SC_HANDLE hService, DWORD dwControl,
    ULONG dwInfoLevel, void *pControlParams);


//---------------------------------------------------------------------------


typedef HANDLE (*P_RegisterEventSource)(void *ServerName, void *SourceName);

typedef BOOL (*P_DeregisterEventSource)(HANDLE hEventLog);

typedef BOOL (*P_ReportEvent)(
    HANDLE hEventLog, WORD wType, WORD wCategory, DWORD dwEventID,
    PSID lpUserSid, WORD wNumStrings, DWORD dwDataSize,
    void *Strings, void *RawData);

typedef BOOL (*P_CloseEventLog)(
    HANDLE hEventLog);

//---------------------------------------------------------------------------
// Pointers
//---------------------------------------------------------------------------


static P_OpenSCManager          __sys_OpenSCManagerW            = NULL;
static P_OpenSCManager          __sys_OpenSCManagerA            = NULL;

static P_OpenService            __sys_OpenServiceW              = NULL;
static P_OpenService            __my_OpenServiceW               = NULL;
static P_OpenService            __sys_OpenServiceA              = NULL;

static P_CloseServiceHandle     __sys_CloseServiceHandle        = NULL;
static P_CloseServiceHandle     __my_CloseServiceHandle			= NULL;

static P_QueryServiceStatus     __sys_QueryServiceStatus        = NULL;
static P_QueryServiceStatus     __my_QueryServiceStatus         = NULL;

static P_QueryServiceStatusEx   __sys_QueryServiceStatusEx      = NULL;
static P_QueryServiceStatusEx   __my_QueryServiceStatusEx       = NULL;

static P_QueryServiceConfig     __sys_QueryServiceConfigW       = NULL;
static P_QueryServiceConfig     __sys_QueryServiceConfigA       = NULL;

static P_QueryServiceConfig2    __sys_QueryServiceConfig2W      = NULL;
static P_QueryServiceConfig2    __sys_QueryServiceConfig2A      = NULL;

static P_EnumServicesStatus     __sys_EnumServicesStatusW       = NULL;
static P_EnumServicesStatus     __sys_EnumServicesStatusA       = NULL;

static P_EnumServicesStatusEx   __sys_EnumServicesStatusExW     = NULL;
static P_EnumServicesStatusEx   __sys_EnumServicesStatusExA     = NULL;

static P_QueryServiceLockStatus __sys_QueryServiceLockStatusW   = NULL;
static P_QueryServiceLockStatus __sys_QueryServiceLockStatusA   = NULL;

static P_GetServiceDisplayName  __sys_GetServiceDisplayNameW    = NULL;
static P_GetServiceDisplayName  __sys_GetServiceDisplayNameA    = NULL;

static P_GetServiceKeyName      __sys_GetServiceKeyNameW        = NULL;
static P_GetServiceKeyName      __sys_GetServiceKeyNameA        = NULL;

static P_EnumDependentServices  __sys_EnumDependentServicesW    = NULL;
static P_EnumDependentServices  __sys_EnumDependentServicesA    = NULL;

static P_QueryServiceObjectSecurity
                                __sys_QueryServiceObjectSecurity= NULL;

static P_SetServiceObjectSecurity
                                __sys_SetServiceObjectSecurity  = NULL;


//---------------------------------------------------------------------------


static P_SubscribeServiceChangeNotifications
                            __sys_SubscribeServiceChangeNotifications = NULL;

//---------------------------------------------------------------------------


static P_LockServiceDatabase    __sys_LockServiceDatabase       = NULL;

static P_UnlockServiceDatabase  __sys_UnlockServiceDatabase     = NULL;

static P_CreateService          __sys_CreateServiceW            = NULL;
static P_CreateService          __sys_CreateServiceA            = NULL;

static P_ChangeServiceConfig    __sys_ChangeServiceConfigW      = NULL;
static P_ChangeServiceConfig    __sys_ChangeServiceConfigA      = NULL;

static P_ChangeServiceConfig2   __sys_ChangeServiceConfig2W     = NULL;
static P_ChangeServiceConfig2   __sys_ChangeServiceConfig2A     = NULL;

static P_DeleteService          __sys_DeleteService             = NULL;

static P_StartService           __sys_StartServiceW             = NULL;
static P_StartService           __my_StartServiceW              = NULL;
static P_StartService           __sys_StartServiceA             = NULL;

static P_StartServiceCtrlDispatcher
                              __sys_StartServiceCtrlDispatcherW = NULL;
static P_StartServiceCtrlDispatcher
							  __my_StartServiceCtrlDispatcherW	= NULL;
static P_StartServiceCtrlDispatcher
                              __sys_StartServiceCtrlDispatcherA = NULL;

static P_RegisterServiceCtrlHandler
                              __sys_RegisterServiceCtrlHandlerW = NULL;
static P_RegisterServiceCtrlHandler
                              __sys_RegisterServiceCtrlHandlerA = NULL;

static P_RegisterServiceCtrlHandlerEx
                            __sys_RegisterServiceCtrlHandlerExW = NULL;
static P_RegisterServiceCtrlHandlerEx
                            __sys_RegisterServiceCtrlHandlerExA = NULL;

static P_SetServiceStatus       __sys_SetServiceStatus          = NULL;
static P_SetServiceStatus       __my_SetServiceStatus           = NULL;

static P_ControlService         __sys_ControlService            = NULL;
static P_ControlService         __my_ControlService             = NULL;

static P_ControlServiceEx       __sys_ControlServiceExW         = NULL;
static P_ControlServiceEx       __sys_ControlServiceExA         = NULL;


//---------------------------------------------------------------------------


static P_RegisterEventSource    __sys_RegisterEventSourceW      = NULL;
static P_RegisterEventSource    __sys_RegisterEventSourceA      = NULL;

static P_DeregisterEventSource  __sys_DeregisterEventSource     = NULL;

static P_ReportEvent            __sys_ReportEventW              = NULL;
static P_ReportEvent            __sys_ReportEventA              = NULL;

static P_CloseEventLog          __sys_CloseEventLog             = NULL;

//---------------------------------------------------------------------------
// Variables
//---------------------------------------------------------------------------


static const WCHAR *Scm_ServicesKeyPath =
    L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Services\\";

static const WCHAR *Scm_MsiServer     = L"MSIServer";
       const WCHAR *Scm_CryptSvc      = L"cryptsvc";

static const WCHAR *SandboxieBITS     = SANDBOXIE L"BITS.exe";
static const WCHAR *SandboxieWUAU     = SANDBOXIE L"WUAU.exe";
static const WCHAR *SandboxieCrypto   = SANDBOXIE L"Crypto.exe";

static const WCHAR *_bits             = L"bits";
static const WCHAR *_wuauserv         = L"wuauserv";
static const WCHAR *_TrustedInstaller = L"TrustedInstaller";


//---------------------------------------------------------------------------
// SCM_IMPORT
//---------------------------------------------------------------------------


#define SCM_IMPORT_XX(base,suffix) {                                    \
    __sys_##base##suffix =                                              \
    (P_##base)Ldr_GetProcAddrNew(DllName_advapi32, L#base L#suffix,#base #suffix); \
    }

#define SCM_IMPORT_(m,base,suffix) {                                    \
    const UCHAR *ProcName = #base#suffix;                               \
    __sys_##base##suffix =                                              \
    (P_##base)Ldr_GetProcAddrNew(m, L#base L#suffix,#base #suffix);     \
    if (! __sys_##base##suffix) {                                       \
        SbieApi_Log(2303, L"%s (ADV)", ProcName);                       \
        return FALSE;                                                   \
    }                                                                   \
    }

#define SCM_IMPORT___(base) SCM_IMPORT_(DllName_advapi32,base,)
#define SCM_IMPORT_W8___(base) SCM_IMPORT_(DllName_sechost,base,)
#define SCM_IMPORT_AW(base) SCM_IMPORT_(DllName_advapi32,base,A) SCM_IMPORT_(DllName_advapi32,base,W)


//---------------------------------------------------------------------------
// SBIEDLL_HOOK_SCM
//---------------------------------------------------------------------------


#define SBIEDLL_HOOK_SCM(proc)                                      \
    *(ULONG_PTR *)&__sys_##proc = (ULONG_PTR)                       \
        SbieDll_Hook(#proc, __sys_##proc, Scm_##proc, module);      \
    if (! __sys_##proc) return FALSE;

#define SBIEDLL_HOOK_SCM_EX(base,suffix)                            \
    if (__sys_##base##suffix == NULL) {                             \
        __sys_##base##suffix =                                      \
            (P_##base)GetProcAddress(module, #base #suffix);        \
        if (__sys_##base##suffix) {                                 \
            *(ULONG_PTR *)&__sys_##base##suffix = (ULONG_PTR)       \
                SbieDll_Hook(#base #suffix, __sys_##base##suffix, Scm_##base##suffix, module); \
            if (! __sys_##base##suffix) return FALSE;               \
        }                                                           \
    }

//---------------------------------------------------------------------------
// Scm (other modules)
//---------------------------------------------------------------------------


#include "scm_query.c"
#include "scm_msi.c"
#include "scm_create.c"
#include "scm_event.c"
#include "scm_notify.c"
#include "scm_misc.c"


//---------------------------------------------------------------------------
// Undo Unicode defines from windows.h
//---------------------------------------------------------------------------


#ifdef OpenSCManager
#undef OpenSCManager
#endif

#ifdef OpenService
#undef OpenService
#endif

#ifdef QueryServiceConfig
#undef QueryServiceConfig
#endif

#ifdef QueryServiceConfig2
#undef QueryServiceConfig2
#endif

#ifdef EnumServicesStatus
#undef EnumServicesStatus
#endif

#ifdef EnumServicesStatusEx
#undef EnumServicesStatusEx
#endif

#ifdef QueryServiceLockStatus
#undef QueryServiceLockStatus
#endif

#ifdef GetServiceDisplayName
#undef GetServiceDisplayName
#endif

#ifdef GetServiceKeyName
#undef GetServiceKeyName
#endif

#ifdef EnumDependentServices
#undef EnumDependentServices
#endif

#ifdef CreateService
#undef CreateService
#endif

#ifdef ChangeServiceConfig
#undef ChangeServiceConfig
#endif

#ifdef ChangeServiceConfig2
#undef ChangeServiceConfig2
#endif

#ifdef StartService
#undef StartService
#endif

#ifdef StartServiceCtrlDispatcher
#undef StartServiceCtrlDispatcher
#endif

#ifdef RegisterServiceCtrlHandler
#undef RegisterServiceCtrlHandler
#endif

#ifdef RegisterServiceCtrlHandlerEx
#undef RegisterServiceCtrlHandlerEx
#endif

#ifdef RegisterEventSource
#undef RegisterEventSource
#endif

#ifdef ReportEvent
#undef ReportEvent
#endif


//---------------------------------------------------------------------------
// Scm_Init
//---------------------------------------------------------------------------


_FX BOOLEAN Scm_Init(HMODULE module)
{
    //
    // As Windows got older, a lot of service-related functions have been subsequently moved
    // from advapi32.dll to the sechost.dll. This happened continuously over many releases.
    // To solve this more elegantly, we invoke the below code twice once when the sechost.dll
    // is loaded and once when advapi32.dll is loaded. The SBIEDLL_HOOK_SCM_EX template was
    // designed such that it will only hook the first encounter of any given function.
    // 
    // To work properly this mechanism necessitates the sechost.dll handler being called before
    // the one for advapi32.dll
    //

    SBIEDLL_HOOK_SCM_EX(OpenSCManager,A);
    SBIEDLL_HOOK_SCM_EX(OpenSCManager,W);

    SBIEDLL_HOOK_SCM_EX(OpenService,A);
    SBIEDLL_HOOK_SCM_EX(OpenService,W);

    SBIEDLL_HOOK_SCM_EX(CloseServiceHandle,);

    SBIEDLL_HOOK_SCM_EX(QueryServiceStatus,);
    SBIEDLL_HOOK_SCM_EX(QueryServiceStatusEx,);

    SBIEDLL_HOOK_SCM_EX(QueryServiceConfig,A);
    SBIEDLL_HOOK_SCM_EX(QueryServiceConfig,W);
    SBIEDLL_HOOK_SCM_EX(QueryServiceConfig2,A);
    SBIEDLL_HOOK_SCM_EX(QueryServiceConfig2,W);

    SBIEDLL_HOOK_SCM_EX(EnumServicesStatus,A);
    SBIEDLL_HOOK_SCM_EX(EnumServicesStatus,W);
    SBIEDLL_HOOK_SCM_EX(EnumServicesStatusEx,A);
    SBIEDLL_HOOK_SCM_EX(EnumServicesStatusEx,W);

    SBIEDLL_HOOK_SCM_EX(QueryServiceLockStatus,A);
    SBIEDLL_HOOK_SCM_EX(QueryServiceLockStatus,W);

    SBIEDLL_HOOK_SCM_EX(GetServiceDisplayName,A);
    SBIEDLL_HOOK_SCM_EX(GetServiceDisplayName,W);

    SBIEDLL_HOOK_SCM_EX(GetServiceKeyName,A);
    SBIEDLL_HOOK_SCM_EX(GetServiceKeyName,W);

    SBIEDLL_HOOK_SCM_EX(EnumDependentServices,A);
    SBIEDLL_HOOK_SCM_EX(EnumDependentServices,W);

    SBIEDLL_HOOK_SCM_EX(QueryServiceObjectSecurity,);
    SBIEDLL_HOOK_SCM_EX(SetServiceObjectSecurity,);

    SBIEDLL_HOOK_SCM_EX(LockServiceDatabase,);
    SBIEDLL_HOOK_SCM_EX(UnlockServiceDatabase,);

    SBIEDLL_HOOK_SCM_EX(CreateService,A);
    SBIEDLL_HOOK_SCM_EX(CreateService,W);

    SBIEDLL_HOOK_SCM_EX(ChangeServiceConfig,A);
    SBIEDLL_HOOK_SCM_EX(ChangeServiceConfig,W);

    SBIEDLL_HOOK_SCM_EX(ChangeServiceConfig2,A);
    SBIEDLL_HOOK_SCM_EX(ChangeServiceConfig2,W);

    SBIEDLL_HOOK_SCM_EX(DeleteService,);

    SBIEDLL_HOOK_SCM_EX(StartService,A);
    SBIEDLL_HOOK_SCM_EX(StartService,W);

    SBIEDLL_HOOK_SCM_EX(StartServiceCtrlDispatcher,A);
    SBIEDLL_HOOK_SCM_EX(StartServiceCtrlDispatcher,W);

    if (__sys_RegisterServiceCtrlHandlerW == NULL) {
        __sys_RegisterServiceCtrlHandlerA = (P_RegisterServiceCtrlHandler)GetProcAddress(module, "RegisterServiceCtrlHandlerA");
        __sys_RegisterServiceCtrlHandlerW = (P_RegisterServiceCtrlHandler)GetProcAddress(module, "RegisterServiceCtrlHandlerW");
        __sys_RegisterServiceCtrlHandlerExA = (P_RegisterServiceCtrlHandlerEx)GetProcAddress(module, "RegisterServiceCtrlHandlerExA");
        __sys_RegisterServiceCtrlHandlerExW = (P_RegisterServiceCtrlHandlerEx)GetProcAddress(module, "RegisterServiceCtrlHandlerExW");
        if (!Scm_HookRegisterServiceCtrlHandler(module))
            return FALSE;
    }

    SBIEDLL_HOOK_SCM_EX(SetServiceStatus,);

    SBIEDLL_HOOK_SCM_EX(ControlService,);
    SBIEDLL_HOOK_SCM_EX(ControlServiceEx,A);
    SBIEDLL_HOOK_SCM_EX(ControlServiceEx,W);

    //
    // NotifyServiceStatusChange is available on Windows Vista and later
    //

    if (Dll_OsBuild < 6000)
        return TRUE;

    //
    // initialize critical section
    //

    if (Scm_Notify_CritSec == NULL) {
        Scm_Notify_CritSec = Dll_Alloc(sizeof(CRITICAL_SECTION));
        InitializeCriticalSectionAndSpinCount(Scm_Notify_CritSec, 1000);
    }

    //
    // hook the API
    //

    SBIEDLL_HOOK_SCM_EX(NotifyServiceStatusChange,A);
    SBIEDLL_HOOK_SCM_EX(NotifyServiceStatusChange,W);

    return TRUE;
}


//---------------------------------------------------------------------------
// Scm_Init_AdvApi
//---------------------------------------------------------------------------


_FX BOOLEAN Scm_Init_AdvApi(HMODULE module)
{
    //
    // hook event log functions
    //

    SCM_IMPORT_AW(RegisterEventSource);
    SBIEDLL_HOOK_SCM(RegisterEventSourceA);
    SBIEDLL_HOOK_SCM(RegisterEventSourceW);

    SCM_IMPORT___(DeregisterEventSource);
    SBIEDLL_HOOK_SCM(DeregisterEventSource);

    SCM_IMPORT_AW(ReportEvent);
    SBIEDLL_HOOK_SCM(ReportEventA);
    SBIEDLL_HOOK_SCM(ReportEventW);

    SCM_IMPORT___(CloseEventLog);
    SBIEDLL_HOOK_SCM(CloseEventLog);

    //
    // hook SCM functions
    //

    // ensure we first try to hook the sechost.dll
    HMODULE sechost = GetModuleHandle(DllName_sechost);
    if (sechost && !Scm_Init(sechost))
        return FALSE;

    if (!Scm_Init(module))
        return FALSE;

    return TRUE;
}


//---------------------------------------------------------------------------
// SecHost_Init
//---------------------------------------------------------------------------


BOOLEAN SecHost_Init(HMODULE module)
{
    //
    // hook SCM functions
    //

    if (!Scm_Init(module))
        return FALSE;

    if (Dll_OsBuild >= 8400) {

        //
        // on Windows 8, hook sechost!SubscribeServiceChangeNotifications
        //

        SCM_IMPORT_W8___(SubscribeServiceChangeNotifications);
        SBIEDLL_HOOK_SCM(SubscribeServiceChangeNotifications);

        //
        // on Windows 8, the cred functions have been moved from advapi32.dll to sechost.dll
        //

        if (!Cred_Init(module))
            return FALSE;
    }

    return TRUE;
}


//---------------------------------------------------------------------------
// Scm_HookRegisterServiceCtrlHandler
//---------------------------------------------------------------------------


BOOLEAN Scm_HookRegisterServiceCtrlHandler(HMODULE module)
{

#ifndef _M_ARM64
    // Note: with the last SCM rework, the below comment is no longer true !!!
    // $HookHack$ - Custom, not automated, Hook no longer applies to later Windows 10 builds
#ifdef _WIN64
    static const UCHAR PrologW[] = {
        0x45, 0x33, 0xC9,                       // xor r9d,r9d
        0x45, 0x33, 0xC0,                       // xor r8d,r8d
        0xE9                                    // jmp ...
    };
    static const UCHAR PrologExW[] = {
        0x41, 0xB9, 0x02, 0x00, 0x00, 0x00,     // mov r9d,2
        0xE9                                    // jmp ...
    };
    BOOLEAN HookedRegisterServiceCtrlHandler = FALSE;

    //
    // on 64-bit Windows, ADVAPI32!RegisterServiceCtrlHandlerW is an 11-byte
    // function embedded in the code space of another function, so to prevent
    // overwriting the other function, we instead hook the internal function
    // ADVAPI32!RegisterServiceCtrlHandlerHelp
    //

    if (memcmp(__sys_RegisterServiceCtrlHandlerW, PrologW, 7) == 0 &&
        memcmp(__sys_RegisterServiceCtrlHandlerExW, PrologExW, 7) == 0) {

        ULONG64 AddrW =
            (ULONG64)(ULONG_PTR)__sys_RegisterServiceCtrlHandlerW
            + 3 + 3 + 5 +       // xor, xor, jmp
            *(LONG *)((UCHAR *)__sys_RegisterServiceCtrlHandlerW + 7);
        ULONG64 AddrExW =
            (ULONG64)(ULONG_PTR)__sys_RegisterServiceCtrlHandlerExW
            + 6 + 5 +           // mov, jmp
            *(LONG *)((UCHAR *)__sys_RegisterServiceCtrlHandlerExW + 7);

        if (AddrW == AddrExW) {

            void *__sys_RegisterServiceCtrlHandlerHelp = (void *)AddrW;

            SBIEDLL_HOOK_SCM(RegisterServiceCtrlHandlerHelp);

            HookedRegisterServiceCtrlHandler = TRUE;
        }
    }

    if (HookedRegisterServiceCtrlHandler)
        return TRUE;
#endif _WIN64
#endif

    //
    // otherwise hook the four functions normally
    //

    SBIEDLL_HOOK_SCM(RegisterServiceCtrlHandlerA);
    SBIEDLL_HOOK_SCM(RegisterServiceCtrlHandlerW);
    SBIEDLL_HOOK_SCM(RegisterServiceCtrlHandlerExA);
    SBIEDLL_HOOK_SCM(RegisterServiceCtrlHandlerExW);

    return TRUE;
}


//---------------------------------------------------------------------------
// Scm_OpenSCManagerW
//---------------------------------------------------------------------------


_FX SC_HANDLE Scm_OpenSCManagerW(
    WCHAR *lpMachineName,
    WCHAR *lpDatabaseName,
    DWORD dwDesiredAccess)
{
    if (Secure_IsRestrictedToken(TRUE)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }

    return HANDLE_SERVICE_MANAGER;
}


//---------------------------------------------------------------------------
// Scm_OpenSCManagerA
//---------------------------------------------------------------------------


_FX SC_HANDLE Scm_OpenSCManagerA(
    UCHAR *lpMachineName,
    UCHAR *lpDatabaseName,
    DWORD dwDesiredAccess)
{
    return Scm_OpenSCManagerW(NULL, NULL, dwDesiredAccess);
}


//---------------------------------------------------------------------------
// Scm_OpenServiceWImpl
//---------------------------------------------------------------------------


_FX SC_HANDLE Scm_OpenServiceWImpl(
    SC_HANDLE hSCManager,
    const WCHAR *lpServiceName,
    DWORD dwDesiredAccess)
{
    WCHAR *name;
    BOOLEAN ok = FALSE;

    if (hSCManager != HANDLE_SERVICE_MANAGER) {
        SetLastError(ERROR_INVALID_HANDLE);
        return (SC_HANDLE)0;
    }

    if ((! lpServiceName) || (! *lpServiceName)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return (SC_HANDLE)0;
    }

    //
    // open the service if we know its name, first check in the sandbox,
    // and if not found, outside the sandbox
    //

    Scm_DiscardKeyCache(lpServiceName);

    if (Scm_IsBoxedService(lpServiceName)) {

        HANDLE hkey = Scm_OpenKeyForService(lpServiceName, FALSE);
        if (hkey) {
            NtClose(hkey);
            ok = TRUE;
        }

    } else {

        SERVICE_QUERY_RPL *rpl = (SERVICE_QUERY_RPL *)
            Scm_QueryServiceByName(lpServiceName, FALSE, FALSE);
        if (rpl) {
            Dll_Free(rpl);
            ok = TRUE;
        }
    }

    if (! ok) {
        // either Scm_OpenKeyForService or Scm_QueryServiceByName
        // has already called SetLastError
        return (SC_HANDLE)0;
    }

    //
    // allocate a 'handle' that points to the service name
    //

    name = Dll_Alloc(
        sizeof(ULONG) + (wcslen(lpServiceName) + 1) * sizeof(WCHAR));
    *(ULONG *)name = tzuk;
    wcscpy((WCHAR *)(((ULONG *)name) + 1), lpServiceName);
    _wcslwr(name);

    SetLastError(0);
    return (SC_HANDLE)name;
}


//---------------------------------------------------------------------------
// Scm_HookOpenServiceW
//---------------------------------------------------------------------------


_FX ULONG_PTR Scm_HookOpenServiceW(VOID* hook)
{
	__my_OpenServiceW = hook;
	return (ULONG_PTR)Scm_OpenServiceWImpl;
}


//---------------------------------------------------------------------------
// Scm_OpenServiceW
//---------------------------------------------------------------------------


_FX SC_HANDLE Scm_OpenServiceW(
    SC_HANDLE hSCManager,
    const WCHAR *lpServiceName,
    DWORD dwDesiredAccess)
{
	if (__my_OpenServiceW)
		return __my_OpenServiceW(hSCManager, lpServiceName, dwDesiredAccess);
	return Scm_OpenServiceWImpl(hSCManager, lpServiceName, dwDesiredAccess);
}


//---------------------------------------------------------------------------
// Scm_OpenServiceA
//---------------------------------------------------------------------------


_FX SC_HANDLE Scm_OpenServiceA(
    SC_HANDLE hSCManager,
    const UCHAR *lpServiceName,
    DWORD dwDesiredAccess)
{
    SC_HANDLE h;
    ANSI_STRING ansi;
    UNICODE_STRING uni;
    DWORD err;

    uni.Buffer = NULL;
    if (lpServiceName) {
        RtlInitString(&ansi, lpServiceName);
        RtlAnsiStringToUnicodeString(&uni, &ansi, TRUE);
    }

    h = Scm_OpenServiceWImpl(hSCManager, uni.Buffer, dwDesiredAccess);
    err = GetLastError();

    if (uni.Buffer)
        RtlFreeUnicodeString(&uni);

    SetLastError(err);
    return h;
}


//---------------------------------------------------------------------------
// Scm_CloseServiceHandleImpl
//---------------------------------------------------------------------------


_FX BOOL Scm_CloseServiceHandleImpl(SC_HANDLE hSCObject)
{
    BOOL ok = FALSE;

    if (hSCObject == HANDLE_SERVICE_MANAGER)
        ok = TRUE;
    else if (Scm_GetHandleName(hSCObject)) {
        Scm_Notify_CloseHandle(hSCObject);
        Dll_Free(hSCObject);
        ok = TRUE;
    }

    if (ok)
        SetLastError(0);
    else
        SetLastError(ERROR_INVALID_HANDLE);
    return ok;
}


//---------------------------------------------------------------------------
// Scm_HookCloseServiceHandle
//---------------------------------------------------------------------------


_FX ULONG_PTR Scm_HookCloseServiceHandle(VOID* hook)
{
	__my_CloseServiceHandle = hook;
	return (ULONG_PTR)Scm_CloseServiceHandleImpl;
}


//---------------------------------------------------------------------------
// Scm_CloseServiceHandle
//---------------------------------------------------------------------------


_FX BOOL Scm_CloseServiceHandle(SC_HANDLE hSCObject)
{
	if (__my_CloseServiceHandle)
		return __my_CloseServiceHandle(hSCObject);
	return Scm_CloseServiceHandleImpl(hSCObject);
}


//---------------------------------------------------------------------------
// Scm_SubscribeServiceChangeNotifications
//---------------------------------------------------------------------------


_FX ULONG_PTR Scm_SubscribeServiceChangeNotifications(
    ULONG_PTR Unknown1, ULONG_PTR Unknown2, ULONG_PTR Unknown3,
    ULONG_PTR Unknown4, ULONG_PTR Unknown5)
{
    //
    // fake success for new unknown function in Windows 8,
    // SubscribeServiceChangeNotifications
    //

    return 0;
}


//---------------------------------------------------------------------------
// Scm_GetHandleName
//---------------------------------------------------------------------------


_FX WCHAR *Scm_GetHandleName(SC_HANDLE hService)
{
    WCHAR *name = NULL;
	if (hService == SC_HANDLE_RPCSS)
		return L"RpcSs";
	if (hService == SC_HANDLE_MSISERVER)
		return L"MSIServer";
	if (hService == SC_HANDLE_EVENTSYSTEM)
		return L"EventSystem";
    __try {
        if (hService && *(ULONG *)hService == tzuk)
            name = (WCHAR *)(((ULONG *)hService) + 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (! name)
        SetLastError(ERROR_INVALID_HANDLE);
    return name;
}


//---------------------------------------------------------------------------
// Scm_IsBoxedService
//---------------------------------------------------------------------------


_FX BOOLEAN Scm_IsBoxedService(const WCHAR *ServiceName)
{
    WCHAR *name, *names;

    BOOLEAN found = FALSE;

    names = Scm_GetBoxedServices();

    for (name = names; *name; name += wcslen(name) + 1) {
        if (_wcsicmp(name, ServiceName) == 0) {
            found = TRUE;
            break;
        }
    }

    Dll_Free(names);

    if (SbieDll_CheckStringInList(ServiceName, NULL, L"SandboxService"))
        found = TRUE;

    /*
    if (_wcsicmp(ServiceName, _eventsystem) == 0) {
        //
        // SandboxieEventSys does not run well on Windows Vista,
        //  so make it available only on earlier systems
        //
        if (! __sys_ControlServiceExW)
            return TRUE;

        return TRUE;
    }*/

    if (! found) {

        if (_wcsicmp(ServiceName, Scm_MsiServer) == 0           ||
            _wcsicmp(ServiceName, _TrustedInstaller) == 0       ||
            _wcsicmp(ServiceName, _bits) == 0                   ||
            _wcsicmp(ServiceName, _wuauserv) == 0               ||
            _wcsicmp(ServiceName, Scm_CryptSvc) == 0) {

            found = TRUE;
        }
    }

    return found;
}


//---------------------------------------------------------------------------
// Scm_GetBoxedServices
//---------------------------------------------------------------------------


_FX WCHAR *Scm_GetBoxedServices(void)
{
    WCHAR *names = NULL;
    NTSTATUS status;
    HANDLE hkey;
    UNICODE_STRING uni;
    union {
        KEY_VALUE_PARTIAL_INFORMATION info;
        WCHAR info_space[256];
    } u;
    ULONG len;

    //
    // otherwise get the MULTI_SZ list of sandboxed services from
    // the SandboxedServices value of the SbieSvc key
    //

    hkey = Scm_OpenKeyForService(SBIESVC, FALSE);
    if (! hkey)
        goto finish;

    RtlInitUnicodeString(&uni, L"SandboxedServices");
    status = NtQueryValueKey(
        hkey, &uni, KeyValuePartialInformation, &u.info, sizeof(u), &len);

    NtClose(hkey);

    if ((! NT_SUCCESS(status)) ||
                u.info.Type != REG_MULTI_SZ || u.info.DataLength == 0)
        goto finish;

    names = Dll_AllocTemp(u.info.DataLength + 8);
    memzero(names, u.info.DataLength + 8);
    memcpy(names, u.info.Data, u.info.DataLength);

finish:

    if (! names) {

        names = Dll_AllocTemp(sizeof(WCHAR) * 2);
        memzero(names, sizeof(WCHAR) * 2);
    }

    return names;
}


//---------------------------------------------------------------------------
// Scm_GetAllServices
//---------------------------------------------------------------------------


_FX WCHAR *Scm_GetAllServices(void)
{
    SERVICE_LIST_REQ req;
    SERVICE_LIST_RPL *rpl;
    WCHAR *true_names, *copy_names, *out_names;
    ULONG out_max_len, out_cur_len;
    WCHAR *small_ptr, *work_ptr, *last_ptr, last_ptr0;
    BOOLEAN free_true_names;

    //
    // get list of services inside and outside sandbox
    //

    req.h.length = sizeof(SERVICE_LIST_REQ);
    req.h.msgid = MSGID_SERVICE_LIST;
    req.type_filter = SERVICE_TYPE_ALL;
    req.state_filter = SERVICE_STATE_ALL;

    rpl = (SERVICE_LIST_RPL *)SbieDll_CallServer(&req.h);
    if (rpl && rpl->h.status == 0) {
        true_names = rpl->names;
        free_true_names = FALSE;
    } else {
        true_names = Dll_AllocTemp(sizeof(WCHAR) * 2);
        memzero(true_names, sizeof(WCHAR) * 2);
        free_true_names = TRUE;
    }

    copy_names = Scm_GetBoxedServices();

    //
    //
    //

    out_max_len = 1000;
    out_names = Dll_AllocTemp(out_max_len * sizeof(WCHAR));
    out_cur_len = 0;

    last_ptr = NULL;

    while (1) {

        //
        // find smallest service alphanumerically.
        // scan both real services and sandboxed services
        //

        small_ptr = NULL;

        for (work_ptr = true_names;
             *work_ptr;
             work_ptr += wcslen(work_ptr) + 1)
        {
            if (*work_ptr == L'*')
                continue;
            if (! small_ptr)
                small_ptr = work_ptr;
            else if (_wcsicmp(work_ptr, small_ptr) < 0)
                small_ptr = work_ptr;
        }

        if (copy_names) {

            for (work_ptr = copy_names;
                 *work_ptr;
                 work_ptr += wcslen(work_ptr) + 1)
            {
                if (*work_ptr == L'*')
                    continue;
                if (! small_ptr)
                    small_ptr = work_ptr;
                else if (_wcsicmp(work_ptr, small_ptr) < 0)
                    small_ptr = work_ptr;
            }
        }

        if (! small_ptr)
            break;

        //
        // we might hit a duplicate service name as the result of use
        // of the StartService setting, so skip the duplicates
        //

        if (last_ptr && *small_ptr == last_ptr0 &&
                _wcsicmp(small_ptr + 1, last_ptr + 1) == 0) {

            *small_ptr = L'*';
            continue;
        }

        last_ptr = small_ptr;
        last_ptr0 = *last_ptr;

        //
        // add the service to the output buffer
        //

        if (wcslen(small_ptr) + 1 + out_cur_len >= out_max_len) {

            WCHAR *new_out;
            out_max_len += 1000;
            new_out = Dll_AllocTemp(out_max_len * sizeof(WCHAR));
            memcpy(new_out, out_names, out_cur_len * sizeof(WCHAR));
            Dll_Free(out_names);
            out_names = new_out;
        }

        wcscpy(&out_names[out_cur_len], small_ptr);
        out_cur_len += wcslen(small_ptr) + 1;

        *small_ptr = L'*';
    }

    //
    // finish
    //

    out_names[out_cur_len] = L'\0';

    if (copy_names)
        Dll_Free(copy_names);

    if (free_true_names)
        Dll_Free(true_names);

    if (rpl)
        Dll_Free(rpl);

    return out_names;
}


//---------------------------------------------------------------------------
// Scm_OpenKeyForService
//---------------------------------------------------------------------------


_FX HANDLE Scm_OpenKeyForService(const WCHAR *ServiceName, BOOLEAN ForWrite)
{
    NTSTATUS status;
    WCHAR keyname[128];
    OBJECT_ATTRIBUTES objattrs;
    UNICODE_STRING objname;
    HANDLE handle;
    ULONG error;

    wcscpy(keyname, Scm_ServicesKeyPath);
    wcscat(keyname, ServiceName);
    RtlInitUnicodeString(&objname, keyname);

    InitializeObjectAttributes(
        &objattrs, &objname, OBJ_CASE_INSENSITIVE, NULL, NULL);

    if (ForWrite) {

        ULONG disp;
        status = NtCreateKey(
            &handle, KEY_ALL_ACCESS, &objattrs, 0, NULL, 0, &disp);

    } else {

        status = NtOpenKey(&handle, KEY_QUERY_VALUE, &objattrs);
    }

    if (NT_SUCCESS(status))
        error = 0;
    else {

        handle = NULL;
        if (status == STATUS_OBJECT_NAME_NOT_FOUND ||
            status == STATUS_OBJECT_PATH_NOT_FOUND) {

            error = ERROR_SERVICE_DOES_NOT_EXIST;
        } else
            error = RtlNtStatusToDosError(status);
    }
    SetLastError(error);

    return handle;
}


//---------------------------------------------------------------------------
// SbieDll_IsBoxedService
//---------------------------------------------------------------------------


_FX BOOLEAN SbieDll_IsBoxedService(HANDLE hService)
{
    WCHAR *ServiceName = Scm_GetHandleName(hService);
    if (! ServiceName)
        return FALSE;
    return Scm_IsBoxedService(ServiceName);
}


//---------------------------------------------------------------------------
// Scm_DiscardKeyCache
//---------------------------------------------------------------------------


_FX void Scm_DiscardKeyCache(const WCHAR *ServiceName)
{
    WCHAR *keyname = Dll_AllocTemp(sizeof(WCHAR) * 256);
    wcscpy(keyname, Scm_ServicesKeyPath);
    wcscat(keyname, ServiceName);
    Key_DiscardMergeByPath(keyname, TRUE);
    Dll_Free(keyname);
}


//---------------------------------------------------------------------------
// SbieDll_CheckProcessLocalSystem
//---------------------------------------------------------------------------


_FX BOOL SbieDll_CheckProcessLocalSystem(HANDLE ProcessHandle)
{
    BOOL IsLocalSystem = FALSE;

    HANDLE TokenHandle;
    if (NtOpenProcessToken(ProcessHandle, TOKEN_QUERY, &TokenHandle)) {

        extern BOOL Secure_IsTokenLocalSystem(HANDLE hToken);
        IsLocalSystem = Secure_IsTokenLocalSystem(TokenHandle);

        NtClose(TokenHandle);
    }

    return IsLocalSystem;
}
