// Copyright (c) 2015-2016, tandasat. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements DdiMon functions.

#include "ddi_mon.h"
#include <ntimage.h>
#define NTSTRSAFE_NO_CB_FUNCTIONS
#include <ntstrsafe.h>
#include <ntifs.h>
#include <WinDef.h>
#include "../HyperPlatform/HyperPlatform/common.h"
#include "../HyperPlatform/HyperPlatform/log.h"
#include "../HyperPlatform/HyperPlatform/util.h"
#include "../HyperPlatform/HyperPlatform/ept.h"
#include "../HyperPlatform/HyperPlatform/kernel_stl.h"
#include <array>
#include <vector>
#include <string>
#include "shadow_hook.h"
#include "ksocket.h"
#include "../../DdiMon/VMProtectDDK.h"

////////////////////////////////////////////////////////////////////////////////
//
// macro utilities
//

////////////////////////////////////////////////////////////////////////////////
//
// constants and macros
//

////////////////////////////////////////////////////////////////////////////////
//
// types
//

// A helper type for parsing a PoolTag value
union PoolTag {
  ULONG value;
  UCHAR chars[4];
};

//ö�ٵ������ŵĻص����ͣ���
using EnumExportedSymbolsCallbackType = bool (*)(
    ULONG index, ULONG_PTR base_address, PIMAGE_EXPORT_DIRECTORY directory,
    ULONG_PTR directory_base, ULONG_PTR directory_end, void* context, BOOLEAN ssdt);
//ö�ٽ���ģ��ص����ͣ���
using DdimonpEnumProcessModuleCallbackType = bool(*)(
	ULONG index, PVOID LdrDataEntry, BOOL Wow64);


// For SystemProcessInformation
enum SystemInformationClass {
  kSystemProcessInformation = 5,
};

// For NtQuerySystemInformation
struct SystemProcessInformation {
  ULONG next_entry_offset;
  ULONG number_of_threads;
  LARGE_INTEGER working_set_private_size;
  ULONG hard_fault_count;
  ULONG number_of_threads_high_watermark;
  ULONG64 cycle_time;
  LARGE_INTEGER create_time;
  LARGE_INTEGER user_time;
  LARGE_INTEGER kernel_time;
  UNICODE_STRING image_name;
  // omitted. see ole32!_SYSTEM_PROCESS_INFORMATION
};


typedef struct _RTL_USER_PROCESS_PARAMETERS {
	BYTE           Reserved1[16];
	PVOID          Reserved2[10];
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB_LDR_DATA {
	ULONG Length;
	UCHAR Initialized;
	PVOID SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
	PVOID EntryInProgress;
} PEB_LDR_DATA, *PPEB_LDR_DATA;
//רΪWoW64׼��;
typedef struct _PEB_LDR_DATA32 {
	ULONG Length;
	UCHAR Initialized;
	ULONG SsHandle;
	LIST_ENTRY32 InLoadOrderModuleList;
	LIST_ENTRY32 InMemoryOrderModuleList;
	LIST_ENTRY32 InInitializationOrderModuleList;
	ULONG EntryInProgress;
} PEB_LDR_DATA32, *PPEB_LDR_DATA32;

typedef struct _PEB {
	UCHAR InheritedAddressSpace;
	UCHAR ReadImageFileExecOptions;
	UCHAR BeingDebugged;
	UCHAR Spare;
	PVOID Mutant;
	PVOID ImageBaseAddress;
	PPEB_LDR_DATA Ldr;
	PRTL_USER_PROCESS_PARAMETERS  ProcessParameters;
	PVOID SubSystemData;
} PEB, *PPEB;
//רΪWoW64׼��;
typedef struct _PEB32 {
	UCHAR InheritedAddressSpace;
	UCHAR ReadImageFileExecOptions;
	UCHAR BeingDebugged;
	UCHAR Spare;
	ULONG Mutant;
	ULONG ImageBaseAddress;
	ULONG/*PPEB_LDR_DATA32*/ Ldr;
} PEB32, *PPEB32;

typedef struct _LDR_DATA_TABLE_ENTRY {
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	ULONG Flags;
	USHORT LoadCount;
	USHORT TlsIndex;
	LIST_ENTRY HashLinks;
	PVOID SectionPointer;
	ULONG CheckSum;
	ULONG TimeDateStamp;
	PVOID LoadedImports;
	PVOID EntryPointActivationContext;
	PVOID PatchInformation;
	LIST_ENTRY ForwarderLinks;
	LIST_ENTRY ServiceTagLinks;
	LIST_ENTRY StaticLinks;
	PVOID ContextInformation;
	PVOID OriginalBase;
	LARGE_INTEGER LoadTime;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;
//רΪWoW64׼��;
typedef struct _LDR_DATA_TABLE_ENTRY32 {
	LIST_ENTRY32 InLoadOrderLinks;
	LIST_ENTRY32 InMemoryOrderLinks;
	LIST_ENTRY32 InInitializationOrderLinks;
	ULONG DllBase;
	ULONG EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING32 FullDllName;
	UNICODE_STRING32 BaseDllName;
	ULONG Flags;
	USHORT LoadCount;
	USHORT TlsIndex;
	LIST_ENTRY32 HashLinks;
	ULONG SectionPointer;
	ULONG CheckSum;
	ULONG TimeDateStamp;
	ULONG LoadedImports;
	ULONG EntryPointActivationContext;
	ULONG PatchInformation;
	LIST_ENTRY32 ForwarderLinks;
	LIST_ENTRY32 ServiceTagLinks;
	LIST_ENTRY32 StaticLinks;
	ULONG ContextInformation;
	ULONG OriginalBase;
	LARGE_INTEGER LoadTime;
} LDR_DATA_TABLE_ENTRY32, *PLDR_DATA_TABLE_ENTRY32;


typedef struct _NT_PROC_THREAD_ATTRIBUTE_ENTRY {
	ULONG Attribute;    // PROC_THREAD_ATTRIBUTE_XXX���μ�MSDN��UpdateProcThreadAttribute��˵��
	SIZE_T Size;        // Value�Ĵ�С
	ULONG_PTR Value;    // ����4�ֽ����ݣ�����һ��Handle��������ָ��
	ULONG Unknown;      // ����0�������������������ݸ�������
} PROC_THREAD_ATTRIBUTE_ENTRY, *PPROC_THREAD_ATTRIBUTE_ENTRY;

typedef struct _NT_PROC_THREAD_ATTRIBUTE_LIST {
	ULONG Length;       // �ṹ�ܴ�С
	PROC_THREAD_ATTRIBUTE_ENTRY Entry[1];
} NT_PROC_THREAD_ATTRIBUTE_LIST, *PNT_PROC_THREAD_ATTRIBUTE_LIST;

//������
struct Protection {
	std::wstring wcProcessName;
	std::string cProcessName;
	DWORD dwProcessId;
};

//ģ�������
struct Module {
	std::wstring wcModuleName;
	std::string Info;
};

typedef struct _SYSTEM_SERVICE_TABLE {
	PVOID  		ServiceTableBase;
	PVOID  		ServiceCounterTableBase;
	ULONGLONG  	NumberOfServices;
	PVOID  		ParamTableBase;
} SYSTEM_SERVICE_TABLE, *PSYSTEM_SERVICE_TABLE;

typedef struct _SERVICE_DESCRIPTOR_TABLE {
	SYSTEM_SERVICE_TABLE ntoskrnl;  // ntoskrnl.exe (native api)
	SYSTEM_SERVICE_TABLE win32k;    // win32k.sys   (gdi/user)
	SYSTEM_SERVICE_TABLE Table3;    // not used
	SYSTEM_SERVICE_TABLE Table4;    // not used
}SERVICE_DESCRIPTOR_TABLE, *PSERVICE_DESCRIPTOR_TABLE;

typedef struct _KSERVICE_TABLE_DESCRIPTOR
{
	PULONG_PTR Base;	// ������ַ
	PULONG Count;		// ������з��񱻵��ô����ļ�����  
	ULONG Limit;			// ������������ж��ٸ������� 
	PUCHAR Number;		// ���������  
} KSERVICE_TABLE_DESCRIPTOR, *PKSERVICE_TABLE_DESCRIPTOR;

typedef struct _SYSTEM_THREADS
{
	LARGE_INTEGER  KernelTime;
	LARGE_INTEGER  UserTime;
	LARGE_INTEGER  CreateTime;
	ULONG    WaitTime;
	PVOID    StartAddress;
	CLIENT_ID   ClientID;
	KPRIORITY   Priority;
	KPRIORITY   BasePriority;
	ULONG    ContextSwitchCount;
	ULONG    ThreadState;
	KWAIT_REASON  WaitReason;
	ULONG    Reserved; //Add  
}SYSTEM_THREADS, *PSYSTEM_THREADS;

typedef struct _SYSTEM_PROCESS_INFORMATION {
	ULONG                   NextEntryOffset;
	ULONG                   NumberOfThreads;
	LARGE_INTEGER           Reserved[3];
	LARGE_INTEGER           CreateTime;
	LARGE_INTEGER           UserTime;
	LARGE_INTEGER           KernelTime;
	UNICODE_STRING          ImageName;
	KPRIORITY               BasePriority;
	HANDLE                  ProcessId;
	HANDLE                  InheritedFromProcessId;
	ULONG                   HandleCount;
	ULONG                   Reserved2[2];
	ULONG                   PrivatePageCount;
	VM_COUNTERS             VirtualMemoryCounters;
	IO_COUNTERS             IoCounters;
	SYSTEM_THREADS           Threads[0];
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;


typedef enum _SYSTEM_INFORMATION_CLASS
{
	SystemBasicInformation,                 //  0 Y N     
	SystemProcessorInformation,             //  1 Y N     
	SystemPerformanceInformation,           //  2 Y N     
	SystemTimeOfDayInformation,             //  3 Y N     
	SystemNotImplemented1,                  //  4 Y N     
	SystemProcessesAndThreadsInformation,   //  5 Y N     
	SystemCallCounts,                       //  6 Y N     
	SystemConfigurationInformation,         //  7 Y N     
	SystemProcessorTimes,                   //  8 Y N     
	SystemGlobalFlag,                       //  9 Y Y     
	SystemNotImplemented2,                  // 10 Y N     
	SystemModuleInformation,                // 11 Y N     
	SystemLockInformation,                  // 12 Y N     
	SystemNotImplemented3,                  // 13 Y N     
	SystemNotImplemented4,                  // 14 Y N     
	SystemNotImplemented5,                  // 15 Y N     
	SystemHandleInformation,                // 16 Y N     
	SystemObjectInformation,                // 17 Y N     
	SystemPagefileInformation,              // 18 Y N     
	SystemInstructionEmulationCounts,       // 19 Y N     
	SystemInvalidInfoClass1,                // 20     
	SystemCacheInformation,                 // 21 Y Y     
	SystemPoolTagInformation,               // 22 Y N     
	SystemProcessorStatistics,              // 23 Y N     
	SystemDpcInformation,                   // 24 Y Y     
	SystemNotImplemented6,                  // 25 Y N     
	SystemLoadImage,                        // 26 N Y     
	SystemUnloadImage,                      // 27 N Y     
	SystemTimeAdjustment,                   // 28 Y Y     
	SystemNotImplemented7,                  // 29 Y N     
	SystemNotImplemented8,                  // 30 Y N     
	SystemNotImplemented9,                  // 31 Y N     
	SystemCrashDumpInformation,             // 32 Y N     
	SystemExceptionInformation,             // 33 Y N     
	SystemCrashDumpStateInformation,        // 34 Y Y/N     
	SystemKernelDebuggerInformation,        // 35 Y N     
	SystemContextSwitchInformation,         // 36 Y N     
	SystemRegistryQuotaInformation,         // 37 Y Y     
	SystemLoadAndCallImage,                 // 38 N Y     
	SystemPrioritySeparation,               // 39 N Y     
	SystemNotImplemented10,                 // 40 Y N     
	SystemNotImplemented11,                 // 41 Y N     
	SystemInvalidInfoClass2,                // 42     
	SystemInvalidInfoClass3,                // 43     
	SystemTimeZoneInformation,              // 44 Y N     
	SystemLookasideInformation,             // 45 Y N     
	SystemSetTimeSlipEvent,                 // 46 N Y     
	SystemCreateSession,                    // 47 N Y     
	SystemDeleteSession,                    // 48 N Y     
	SystemInvalidInfoClass4,                // 49     
	SystemRangeStartInformation,            // 50 Y N     
	SystemVerifierInformation,              // 51 Y Y     
	SystemAddVerifier,                      // 52 N Y     
	SystemSessionProcessesInformation       // 53 Y N     
} SYSTEM_INFORMATION_CLASS;

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

typedef PPEB32(__stdcall * pfn_PsGetProcessWow64Process) (PEPROCESS Process);
extern "C" {
	NTKERNELAPI PEB *NTAPI PsGetProcessPeb(_In_ PEPROCESS process);
	NTKERNELAPI UCHAR *NTAPI PsGetProcessImageFileName(_In_ PEPROCESS process);
	NTKERNELAPI NTSTATUS NTAPI ZwQuerySystemInformation(
		_In_       SYSTEM_INFORMATION_CLASS SystemInformationClass,
		_Inout_    PVOID SystemInformation,
		_In_       ULONG SystemInformationLength,
		_Out_opt_  PULONG ReturnLength
	);
}
_IRQL_requires_max_(PASSIVE_LEVEL) EXTERN_C
    static void DdimonpFreeAllocatedTrampolineRegions();

_IRQL_requires_max_(PASSIVE_LEVEL) EXTERN_C static NTSTATUS
    DdimonpEnumExportedSymbols(_In_ ULONG_PTR base_address,
                               _In_ EnumExportedSymbolsCallbackType callback,
                               _In_opt_ void* context);

_IRQL_requires_max_(PASSIVE_LEVEL) EXTERN_C
    static bool DdimonpEnumExportedSymbolsCallback(
        _In_ ULONG index, _In_ ULONG_PTR base_address,
        _In_ PIMAGE_EXPORT_DIRECTORY directory, _In_ ULONG_PTR directory_base,
        _In_ ULONG_PTR directory_end, _In_opt_ void* context,BOOLEAN ssdt);

static std::array<char, 5> DdimonpTagToString(_In_ ULONG tag_value);

template <typename T>
static T DdimonpFindOrignal(_In_ T handler);

static VOID DdimonpHandleExQueueWorkItem(_Inout_ PWORK_QUEUE_ITEM work_item,
                                         _In_ WORK_QUEUE_TYPE queue_type);

static PVOID DdimonpHandleExAllocatePoolWithTag(_In_ POOL_TYPE pool_type,
                                                _In_ SIZE_T number_of_bytes,
                                                _In_ ULONG tag);

static VOID DdimonpHandleExFreePool(_Pre_notnull_ PVOID p);

static VOID DdimonpHandleExFreePoolWithTag(_Pre_notnull_ PVOID p,
                                           _In_ ULONG tag);

static NTSTATUS DdimonpHandleNtQuerySystemInformation(
    _In_ SystemInformationClass SystemInformationClass,
    _Inout_ PVOID SystemInformation, _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);
static NTSTATUS DdimonpHandleNtOpenProcess(
	_Out_    PHANDLE            ProcessHandle,
	_In_     ACCESS_MASK        DesiredAccess,
	_In_     POBJECT_ATTRIBUTES ObjectAttributes,
	_In_opt_ PCLIENT_ID         ClientId
);
static NTSTATUS DdimonpHandleNtCreateUserProcess(
	OUT PHANDLE ProcessHandle,
	OUT PHANDLE ThreadHandle,
	IN ACCESS_MASK ProcessDesiredAccess,
	IN ACCESS_MASK ThreadDesiredAccess,
	IN POBJECT_ATTRIBUTES ProcessObjectAttributes OPTIONAL,
	IN POBJECT_ATTRIBUTES ThreadObjectAttributes OPTIONAL,
	IN ULONG CreateProcessFlags,
	IN ULONG CreateThreadFlags,
	IN PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
	IN PVOID Parameter9,
	IN PNT_PROC_THREAD_ATTRIBUTE_LIST AttributeList
);
static NTSTATUS DdimonpHandleNtCreateProcessEx(
	OUT PHANDLE ProcessHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes,
	IN HANDLE InheritFromProcessHandle,
	IN BOOLEAN InheritHandles,
	IN HANDLE SectionHandle OPTIONAL,
	IN HANDLE DebugPort OPTIONAL,
	IN HANDLE ExceptionPort OPTIONAL,
	IN HANDLE Unknown);



#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, DdimonInitialization)
#pragma alloc_text(INIT, DdimonpEnumExportedSymbols)
#pragma alloc_text(INIT, DdimonpEnumExportedSymbolsCallback)
#pragma alloc_text(PAGE, DdimonTermination)
#pragma alloc_text(PAGE, DdimonpFreeAllocatedTrampolineRegions)
#endif

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

//���������ﰲװӰ�ӹ��Ӽ��䴦�����
//
//��ΪDdiMon�ļ�ʵ�֣�DdiMon�޷������κ�
//��ȷ�������µ�����
// - �Ѿ�δӳ��ĵ��������磬INIT���ֵĵ���������Ϊ��û��
//�������ڴ���
// - �������ݣ���Ϊ����0xcc�����������û���κ�����
// - ����������x64����Լ��������Zw *
// ���ܡ� ��Ϊ��ջ�����ݲ��ܱ�������ֵ����
//�������ʧ�ܵĲ������������ܵ��´����顣
//
//ҲӦ��ע�����¼��㣺
// - ���������������û���ַ�ռ�ָ�룬������
//���Ρ� ������Ӧ������һ���ں˵�ַ�ռ�ָ��
//��������ȫ�� ��֤�������û�����������
//�ṩ��VMM�ĵ�ַ��Ȼ��ʹ�����ǡ�

#ifdef _WIN64
PSYSTEM_SERVICE_TABLE KeServiceDescriptorTable = NULL;
#else
extern "C"  PKSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTable;
#endif




static ShadowHookTarget g_ddimonp_hook_targets[] = {
	{
		RTL_CONSTANT_STRING(L"EXQUEUEWORKITEM"), DdimonpHandleExQueueWorkItem,
		nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"EXALLOCATEPOOLWITHTAG"),
		DdimonpHandleExAllocatePoolWithTag, nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"EXFREEPOOL"), DdimonpHandleExFreePool, nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"EXFREEPOOLWITHTAG"),
		DdimonpHandleExFreePoolWithTag, nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"NTQUERYSYSTEMINFORMATION"),
		DdimonpHandleNtQuerySystemInformation, nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"NTOPENPROCESS"),
		DdimonpHandleNtOpenProcess, nullptr, FALSE
	},
	{
		RTL_CONSTANT_STRING(L"NTCREATEUSERPROCESS"),
		DdimonpHandleNtCreateUserProcess, nullptr, TRUE
	},
	{
		RTL_CONSTANT_STRING(L"NTCREATEPROCESSEX"),
		DdimonpHandleNtCreateProcessEx, nullptr, TRUE
	},
};

NOTIFY_HANDLE g_NotifyHandle;
TRY_SOKE g_uUserChoice;
//����������
std::vector<Protection> lProtectionList = {
	{ L"notepad.exe","notepad.exe",0},
	/*{ L"���ƶ�.vshost.exe","���ƶ�.vshost.exe",0 },
	{ L"���ƶ�.exe","���ƶ�.exe",0 },
	{ L"lulujxjs.vshost.exe","lulujxjs.vshost.exe",0 },
	{ L"lulujxjs.exe","lulujxjs.exe",0 },
	{ L"devenv.exe","devenv.exe",0 },*/
};
std::vector<Module> lModuleList = {
	{ L"XueTr.exe", "�ں˲鿴����"},
	{ L"PCHunter32.exe", "�ں˲鿴����" },
	{ L"PCHunter64.exe", "�ں˲鿴����" },
	{ L"SRSniffer.exe", "�������" },
	{ L"WpeSpy.dll", "�������" },
	{ L"psvince,1.dll", "PE����" },
	{ L"libgcc_s_dw2-1.dll", "�����빤��" },
	{ L"ida.wll", "�����빤��" },
	{ L"dbgmsgcfg.dll", "��������" },
	{ L"x32dbg.dll", "���Թ���" },
	{ L"x64dbg.dll", "���Թ���" },
	{ L"API_Break.dll", "���Թ���" },
	{ L"OllyPath.dll", "���Թ���" },
	{ L"StrongOD.dll", "���Թ���" },
	{ L"allochook-x86_64.dll", "�ڴ湤��" },
	{ L"allochook-i386.dll", "�ڴ湤��" },
	{ L"krnln.fne", "��������" },
};

bool DdimonpAddProtection(TRY_SOKE &soke)
{
	UNICODE_STRING ucStrFile;
	ANSI_STRING cStrFile;
	Protection pin;
	RtlInitUnicodeString(&ucStrFile, soke.ProcessInfo);
	if (RtlUnicodeStringToAnsiString(&cStrFile, &ucStrFile, TRUE) != STATUS_SUCCESS)
		return false;
	pin.wcProcessName = soke.ProcessInfo;
	pin.cProcessName = cStrFile.Buffer;
	lProtectionList.push_back(pin);
	HYPERPLATFORM_LOG_INFO("DdimonpAddProtection %ws\n", soke.ProcessInfo);
	return true;
}

bool DdimonpResetProtection()
{
	lProtectionList.clear();
	HYPERPLATFORM_LOG_INFO("lProtectionList.clear");
	return true;
}

_Use_decl_annotations_ EXTERN_C static bool DdimonpEnumProcessModuleCallback(
	ULONG index, PVOID LdrDataEntry, BOOL Wow64) {
	PAGED_CODE();
	wchar_t *FunDllName = NULL;
	size_t NameLen = NULL;
	if (Wow64){
		auto Ldr = (PLDR_DATA_TABLE_ENTRY32)LdrDataEntry;
		FunDllName = (wchar_t *)Ldr->FullDllName.Buffer;
		NameLen = Ldr->FullDllName.Length;
		//HYPERPLATFORM_LOG_INFO("Wow64 %d %ws \n", index, FunDllName);
	}
	else {
		auto Ldr = (PLDR_DATA_TABLE_ENTRY)LdrDataEntry;
		FunDllName = (wchar_t *)Ldr->FullDllName.Buffer;
		NameLen = Ldr->FullDllName.Length;
		//HYPERPLATFORM_LOG_INFO("8086 %d %ws \n", index, FunDllName);
	}
	std::vector<Module>::iterator it;
	__try{
		for (it = lModuleList.begin(); it != lModuleList.end(); it++){
			if (_wcsnicmp(FunDllName, it->wcModuleName.c_str(), NameLen) == 0) {
				HYPERPLATFORM_LOG_INFO("Try %ws  dd:%ws\n", FunDllName, it->Info.c_str());
				return false;
			}
		}
	}
	__except (1){

	}
	return true;
}

_Use_decl_annotations_ NTSTATUS EmumProcessModules
(HANDLE ulProcessId , DdimonpEnumProcessModuleCallbackType back)
{
	PEPROCESS  pEProcess = NULL;
	KAPC_STATE KAPC = { 0 };
	static pfn_PsGetProcessWow64Process PsGetProcessWow64Process = NULL;

	auto nStatus = PsLookupProcessByProcessId((HANDLE)ulProcessId, &pEProcess);
	if (!NT_SUCCESS(nStatus)){
		HYPERPLATFORM_LOG_INFO("Get EProcess Failed~!\n");
		return STATUS_UNSUCCESSFUL;
	}

	auto pPEB = PsGetProcessPeb(pEProcess);
	if (pPEB == NULL){
		HYPERPLATFORM_LOG_INFO("Get pPEB Failed~!\n");
		return STATUS_UNSUCCESSFUL;
	}
	//���ӵ�����  
	KeStackAttachProcess(pEProcess, &KAPC);
	auto bIsAttached = TRUE;

	__try
	{
		auto pPebLdrData = pPEB->Ldr;
		auto pListEntryStart = pPebLdrData->InMemoryOrderModuleList.Flink;
		auto pListEntryEnd = pListEntryStart;
		int i = 0;
		do {
			//ͨ��_LIST_ENTRY��Flink��Ա��ȡ_LDR_DATA_TABLE_ENTRY�ṹ    
			auto pLdrDataEntry = (PLDR_DATA_TABLE_ENTRY)CONTAINING_RECORD(pListEntryStart, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
			//���DLLȫ·��  
			if (!back(i, pLdrDataEntry, false)) {
				break;
			}
			pListEntryStart = pListEntryStart->Flink;
		} while (pListEntryStart != pListEntryEnd);
	}
	__except (1){
		HYPERPLATFORM_LOG_INFO("bt __except \n");
	}
	
#ifdef _WIN64
	if (!PsGetProcessWow64Process) {
		UNICODE_STRING uniFunctionName;
		RtlInitUnicodeString(&uniFunctionName, L"PsGetProcessWow64Process");
		PsGetProcessWow64Process = (pfn_PsGetProcessWow64Process)(SIZE_T)MmGetSystemRoutineAddress(&uniFunctionName);
	}
	__try
	{
		auto pPEB32 = PsGetProcessWow64Process(pEProcess);
		if (pPEB32){
			auto pListEntryStart32 = (PLIST_ENTRY32)(((PEB_LDR_DATA32*)pPEB32->Ldr)->InMemoryOrderModuleList.Flink);
			auto pListEntryEnd32 = (PLIST_ENTRY32)(((PEB_LDR_DATA32*)pPEB32->Ldr)->InMemoryOrderModuleList.Flink);
			int i = 0;
			do {
				auto pLdrDataEntry32 = (PLDR_DATA_TABLE_ENTRY32)CONTAINING_RECORD(pListEntryStart32, LDR_DATA_TABLE_ENTRY32, InMemoryOrderLinks);
				if (!back(i, pLdrDataEntry32, true)) {
					break;
				}
				pListEntryStart32 = (PLIST_ENTRY32)pListEntryStart32->Flink;
			} while (pListEntryStart32 != pListEntryEnd32);
		}
	}
	__except (1){
		HYPERPLATFORM_LOG_INFO("32bt __except \n");
	}
#endif // _WIN64

	if (bIsAttached != FALSE){
		KeUnstackDetachProcess(&KAPC);
	}

	if (pEProcess != NULL){
		ObDereferenceObject(pEProcess);
		pEProcess = NULL;
	}

	return STATUS_SUCCESS;
}

_Use_decl_annotations_ EXTERN_C NTSTATUS
ThreadProc()
{
	VMProtectBegin("Driver_Dispatch");

	NTSTATUS                                status;
	ULONG                                   retusize;
	UNICODE_STRING                          ZwFunName;
	PVOID                                   AllSize = 0;
	SYSTEM_PROCESS_INFORMATION*             ProcessInfo;
	RtlInitUnicodeString(&ZwFunName, L"ZwQuerySystemInformation");

	status = ZwQuerySystemInformation(SystemProcessesAndThreadsInformation, 0, 0, &retusize);

	if (retusize == 0){
		HYPERPLATFORM_LOG_INFO("retu size is null");
		PsTerminateSystemThread(STATUS_SUCCESS);
		return STATUS_SUCCESS;
	}

	AllSize = ExAllocatePool(NonPagedPool, retusize);
	if (AllSize == 0){
		HYPERPLATFORM_LOG_INFO("AllSize size is null");
		PsTerminateSystemThread(STATUS_SUCCESS);
		return STATUS_SUCCESS;
	}

	status = ZwQuerySystemInformation(SystemProcessesAndThreadsInformation, AllSize, (ULONG)retusize, &retusize);

	if (!NT_SUCCESS(status)){
		HYPERPLATFORM_LOG_INFO("ZwQuerySystemInformation is faild!");
		ExFreePool(AllSize);
		return STATUS_SUCCESS;
	}

	ProcessInfo = (SYSTEM_PROCESS_INFORMATION*)AllSize;
	int i = 0;
	while (ProcessInfo->NextEntryOffset){
		HYPERPLATFORM_LOG_INFO("%d ProcessId:%d------ProcessName:%wZ",(i++,i), ProcessInfo->ProcessId, &ProcessInfo->ImageName);
		EmumProcessModules(ProcessInfo->ProcessId, DdimonpEnumProcessModuleCallback);
		ProcessInfo = (SYSTEM_PROCESS_INFORMATION*)((ULONGLONG)ProcessInfo + ProcessInfo->NextEntryOffset);
	}

	ExFreePool(AllSize);

	PsTerminateSystemThread(STATUS_SUCCESS);
	VMProtectEnd();
	return STATUS_SUCCESS;
}

_Use_decl_annotations_ EXTERN_C NTSTATUS
GetKeServiceDescriptorTable64()
{
	VMProtectBegin("GetKeServiceDescriptorTable64");
#ifdef _WIN64
	
	PUCHAR StartSearchAddress = (PUCHAR)UtilReadMsr64(Msr::kIa32Lstar);// (PUCHAR)__readmsr(0xc0000082);
	PUCHAR EndSearchAddress = StartSearchAddress + 0x500;

	//��ַЧ��
	if (!MmIsAddressValid(EndSearchAddress)) { 
		return false;
	}

	PUCHAR i = NULL;
	UCHAR b1 = 0, b2 = 0, b3 = 0;
	ULONG templong = 0;
	ULONGLONG addr = 0;

	for (i = StartSearchAddress; i < EndSearchAddress; i++){
		if (MmIsAddressValid(i) && MmIsAddressValid(i + 1) && MmIsAddressValid(i + 2)){
			b1 = *i;
			b2 = *(i + 1);
			b3 = *(i + 2);
			if (b1 == 0x4c && b2 == 0x8d && b3 == 0x15)  /*4c8d15*/			{
				memcpy(&templong, i + 3, 4);
				addr = (ULONGLONG)templong + (ULONGLONG)i + 7;
				break;
			}
		}
	}
	if (addr){
		KeServiceDescriptorTable = (PSYSTEM_SERVICE_TABLE)addr;
		HYPERPLATFORM_LOG_INFO("PSYSTEM_SERVICE_TABLE %x", KeServiceDescriptorTable);
	}
#endif // _WIN64
	VMProtectEnd();
	return true;
}

// Initializes DdiMon
_Use_decl_annotations_ EXTERN_C NTSTATUS
DdimonInitialization(SharedShadowHookData* shared_sh_data) {
	// Get a base address of ntoskrnl
  auto nt_base = UtilPcToFileHeader(KdDebuggerEnabled);
  if (!nt_base) {
    return STATUS_UNSUCCESSFUL;
  }
  VMProtectBegin("DdimonInitialization");

  GetKeServiceDescriptorTable64();

  // Install hooks by enumerating exports of ntoskrnl, but not activate them yet
  // ͨ��ö��ntoskrnl�ĵ�����װ���ӣ����ǲ���������
  auto status = DdimonpEnumExportedSymbols(reinterpret_cast<ULONG_PTR>(nt_base),
                                           DdimonpEnumExportedSymbolsCallback,
                                           shared_sh_data);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  // Activate installed hooks
  // �����Ѱ�װ�Ĺҹ�
  status = ShEnableHooks();
  if (!NT_SUCCESS(status)) {
    DdimonpFreeAllocatedTrampolineRegions();
    return status;
  }
  //�������̼���߳�
  HANDLE threadHandle = NULL;
  auto lstatus = PsCreateSystemThread(&threadHandle,
	  0,
	  NULL, //����THREAD_ALL_ACCESS  
	  NtCurrentProcess(),
	  NULL,
	  (PKSTART_ROUTINE)ThreadProc,
	  NULL);



  HYPERPLATFORM_LOG_INFO("DdiMon has been initialized.");
  VMProtectEnd();
  return status;
}

// Terminates DdiMon
// ��ֹDiMon
_Use_decl_annotations_ EXTERN_C void DdimonTermination() {
  PAGED_CODE();

  ShDisableHooks();
  UtilSleep(1000);
  DdimonpFreeAllocatedTrampolineRegions();
  HYPERPLATFORM_LOG_INFO("DdiMon has been terminated.");
}

// Frees trampoline code allocated and stored in g_ddimonp_hook_targets by
// �ͷŷ��䲢�洢��g_ddimonp_hook_targets�еıĴ�����
// DdimonpEnumExportedSymbolsCallback()
_Use_decl_annotations_ EXTERN_C static void
DdimonpFreeAllocatedTrampolineRegions() {
  PAGED_CODE();

  for (auto& target : g_ddimonp_hook_targets) {
    if (target.original_call) {
      ExFreePoolWithTag(target.original_call, kHyperPlatformCommonPoolTag);
      target.original_call = nullptr;
    }
  }
}

// Enumerates all exports in a module specified by base_address.
// ö���ɻ���ַָ����ģ���е����е�����
_Use_decl_annotations_ EXTERN_C static NTSTATUS DdimonpEnumExportedSymbols(
    ULONG_PTR base_address, EnumExportedSymbolsCallbackType callback,
    void* context) {
  PAGED_CODE();
  //�������
  NTSTATUS status;
  SIZE_T Size = 0;
  HANDLE hSection, hFile;
  OBJECT_ATTRIBUTES oa;
  IO_STATUS_BLOCK iosb;
  UNICODE_STRING pDllName;
  ULONG_PTR BaseAddress = NULL;

  VMProtectBegin("DdimonpEnumExportedSymbols");
  

  auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base_address);
  auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base_address + dos->e_lfanew);
  auto dir = reinterpret_cast<PIMAGE_DATA_DIRECTORY>(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]);
  if (!dir->Size || !dir->VirtualAddress) {
    return STATUS_SUCCESS;
  }
  
  auto dir_base = base_address + dir->VirtualAddress;
  auto dir_end = base_address + dir->VirtualAddress + dir->Size - 1;
  auto exp_dir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base_address +
                                                           dir->VirtualAddress);
  for (auto i = 0ul; i < exp_dir->NumberOfNames; i++) {
    if (!callback(i, base_address, exp_dir, dir_base, dir_end, context, FALSE)) {
      return STATUS_SUCCESS;
    }
  }
  
  RtlInitUnicodeString(&pDllName, L"\\SystemRoot\\System32\\ntdll.dll");
  InitializeObjectAttributes(&oa, &pDllName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
  status = ZwOpenFile(&hFile, FILE_GENERIC_READ | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ, FILE_SYNCHRONOUS_IO_NONALERT);
  if (NT_SUCCESS(status)){
	  oa.ObjectName = 0;
	  status = ZwCreateSection(&hSection, SECTION_ALL_ACCESS, &oa, 0, PAGE_READONLY, 0x01000000, hFile);
	  if (NT_SUCCESS(status)){
		  BaseAddress = NULL;
		  status = ZwMapViewOfSection(hSection, NtCurrentProcess(), (PVOID*)&BaseAddress, 0, 0, 0, &Size, ViewShare, MEM_TOP_DOWN, PAGE_READONLY);
		  if (NT_SUCCESS(status)){
			  dos = (PIMAGE_DOS_HEADER)BaseAddress;
			  nt = (PIMAGE_NT_HEADERS)(BaseAddress + dos->e_lfanew);
			  dir = reinterpret_cast<PIMAGE_DATA_DIRECTORY>(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]);
			  if (!dir->Size || !dir->VirtualAddress) {
				  return STATUS_SUCCESS;
			  }
			  dir_base = BaseAddress + dir->VirtualAddress;
			  dir_end = BaseAddress + dir->VirtualAddress + dir->Size - 1;
			  exp_dir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(BaseAddress + dir->VirtualAddress);
			  for (ULONG i = 0; i < exp_dir->NumberOfNames; i++){
				  if (!callback(i, BaseAddress, exp_dir, dir_base, dir_end, context, TRUE)) {
					  return STATUS_SUCCESS;
				  }
			  }
			  ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)BaseAddress);
		  }
		  ZwClose(hSection);
	  }
	  ZwClose(hFile);
  }
  VMProtectEnd();
  return STATUS_SUCCESS;
}

// Checks if the export is listed as a hook target, and if so install a hook.
// ��鵼���Ƿ���Ϊ����Ŀ�꣬�����������װ���ӡ�
_Use_decl_annotations_ EXTERN_C static bool DdimonpEnumExportedSymbolsCallback(
    ULONG index, ULONG_PTR base_address, PIMAGE_EXPORT_DIRECTORY directory,
    ULONG_PTR directory_base, ULONG_PTR directory_end, void* context, BOOLEAN ssdt) {
  PAGED_CODE();

  VMProtectBegin("DdimonpEnumExportedSymbolsCallback");

  if (!context) {
    return false;
  }
  static auto ssdt_index = -1;
  auto functions =
      reinterpret_cast<ULONG*>(base_address + directory->AddressOfFunctions);
  auto ordinals = reinterpret_cast<USHORT*>(base_address +
                                            directory->AddressOfNameOrdinals);
  auto names =
      reinterpret_cast<ULONG*>(base_address + directory->AddressOfNames);

  auto export_address = base_address + 0;
  auto export_name = reinterpret_cast<const char*>(base_address + names[index]);
  char export_str[256] = { 0 };
  if (ssdt) {
	  
	  if (!KeServiceDescriptorTable) {
		  return false;
	  }
	
	  if (export_name[0] != 'Z' || export_name[1] != 'w') {
		  return true;
	  }
	  ssdt_index++;
#ifdef _WIN64
	  export_address = PULONG(KeServiceDescriptorTable->ServiceTableBase)[ssdt_index];
	  export_address = export_address >> 4;
	  export_address = export_address + (ULONGLONG)KeServiceDescriptorTable->ServiceTableBase;
		 
#else
	  export_address = KeServiceDescriptorTable[0].Base[ssdt_index];
#endif

	  if (export_name){
		  strcpy_s(export_str, 256, export_name);
		  export_str[0] = 'N';
		  export_str[1] = 't';
		  export_name = reinterpret_cast<const char*>(export_str);
	  }
	  //HYPERPLATFORM_LOG_INFO("ssdt api at %d %x %s.", ssdt_index, export_address, export_name);
  }
  else {
	  auto ord = ordinals[index];
	  export_address = base_address + functions[ord];
	  //HYPERPLATFORM_LOG_INFO("def api at %d %x %s.", index, export_address, export_name);
  }

  // ��鵼���Ƿ�ת���� ����ǣ����������
  if (UtilIsInBounds(export_address, directory_base, directory_end)) {
    return true;
  }

  // ������ת��ΪUNICODE_STRING
  wchar_t name[100];
  auto status =
      RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"%S", export_name);
  if (!NT_SUCCESS(status)) {
    return true;
  }

  

  UNICODE_STRING name_u = {};
  RtlInitUnicodeString(&name_u, name);

  for (auto& target : g_ddimonp_hook_targets) {
	  // Is this export listed as a target
	  if (!FsRtlIsNameInExpression(&target.target_name, &name_u, TRUE, nullptr)) {
		  continue;
	  }
	  if (target.SSDT != ssdt) {
		  return true;
	  }

	// �ǵģ���װһ�����ӵ�����
    if (!ShInstallHook(reinterpret_cast<SharedShadowHookData*>(context),
                       reinterpret_cast<void*>(export_address), &target)) {
	  // ����һ����Ӧ�÷����Ĵ���
      DdimonpFreeAllocatedTrampolineRegions();
      return false;
    }
    HYPERPLATFORM_LOG_INFO("Hook has been installed at %p %s.", export_address,
                           export_name);
  }
  VMProtectEnd();
  return true;
}

// �������еĳر��ת��Ϊ�ɴ�ӡ���ַ���
_Use_decl_annotations_ static std::array<char, 5> DdimonpTagToString(
    ULONG tag_value) {
  PoolTag tag = {tag_value};
  for (auto& c : tag.chars) {
    if (!c && isspace(c)) {
      c = ' ';
    }
    if (!isprint(c)) {
      c = '.';
    }
  }

  std::array<char, 5> str;
  auto status =
      RtlStringCchPrintfA(str.data(), str.size(), "%c%c%c%c", tag.chars[0],
                          tag.chars[1], tag.chars[2], tag.chars[3]);
  NT_VERIFY(NT_SUCCESS(status));
  return str;
}

// ���ҵ���ԭʼ�����Ĵ������
template <typename T>
static T DdimonpFindOrignal(T handler) {
  for (const auto& target : g_ddimonp_hook_targets) {
    if (target.handler == handler) {
      NT_ASSERT(target.original_call);
      return reinterpret_cast<T>(target.original_call);
    }
  }
  NT_ASSERT(false);
  return nullptr;
}


// ExFreePool�����Ĺ��Ӵ������ ��־��ExFreePool�������������
// ��֧���κ�ͼ��
_Use_decl_annotations_ static VOID DdimonpHandleExFreePool(PVOID p) {
  const auto original = DdimonpFindOrignal(DdimonpHandleExFreePool);
  original(p);

  // Is inside image?
  // �ڲ�ͼ��
  auto return_addr = _ReturnAddress();
  if (UtilPcToFileHeader(return_addr)) {
    return;
  }

  /*HYPERPLATFORM_LOG_INFO_SAFE("%p: ExFreePool(P= %p)", return_addr, p);*/
}


// ExFreePoolWithTag�����Ĺ��Ӵ������ ���ExFreePoolWithTag��������־
// �����ﲻ���κ�ͼ��֧�֡�
_Use_decl_annotations_ static VOID DdimonpHandleExFreePoolWithTag(PVOID p,
                                                                  ULONG tag) {
  const auto original = DdimonpFindOrignal(DdimonpHandleExFreePoolWithTag);
  original(p, tag);

  // Is inside image?
  auto return_addr = _ReturnAddress();
  if (UtilPcToFileHeader(return_addr)) {
    return;
  }

  /*HYPERPLATFORM_LOG_INFO_SAFE("%p: ExFreePoolWithTag(P= %p, Tag= %s)",
                              return_addr, p, DdimonpTagToString(tag).data());*/
}


// ExQueueWorkItem�����Ĺ��Ӵ������ �����������ָ�����־
// ���в�֧���κ�ͼ��
_Use_decl_annotations_ static VOID DdimonpHandleExQueueWorkItem(
    PWORK_QUEUE_ITEM work_item, WORK_QUEUE_TYPE queue_type) {
  const auto original = DdimonpFindOrignal(DdimonpHandleExQueueWorkItem);

  // Is inside image?
  if (UtilPcToFileHeader(work_item->WorkerRoutine)) {
    // Call an original after checking parameters. It is common that a work
    // routine frees a work_item object resulting in wrong analysis.
	// �����������ԭ���� ͨ��������
	// �����ͷŹ�������󣬵��´���ķ�����
    original(work_item, queue_type);
    return;
  }

//  auto return_addr = _ReturnAddress();
  /*HYPERPLATFORM_LOG_INFO_SAFE(
      "%p: ExQueueWorkItem({Routine= %p, Parameter= %p}, %d)", return_addr,
      work_item->WorkerRoutine, work_item->Parameter, queue_type);*/

  original(work_item, queue_type);
}


// ExAllocatePoolWithTag�����Ĺ��Ӵ������ ��¼ExAllocatePoolWithTag����
// �Ӳ�֧���κ�ͼ��ĵط����á�
_Use_decl_annotations_ static PVOID DdimonpHandleExAllocatePoolWithTag(
    POOL_TYPE pool_type, SIZE_T number_of_bytes, ULONG tag) {
  const auto original = DdimonpFindOrignal(DdimonpHandleExAllocatePoolWithTag);
  const auto result = original(pool_type, number_of_bytes, tag);

  // Is inside image?
  // �ڲ�ͼ��
  auto return_addr = _ReturnAddress();
  if (UtilPcToFileHeader(return_addr)) {
    return result;
  }

  /*HYPERPLATFORM_LOG_INFO_SAFE(
      "%p: ExAllocatePoolWithTag(POOL_TYPE= %08x, NumberOfBytes= %08X, Tag= "
      "%s) => %p",
      return_addr, pool_type, number_of_bytes, DdimonpTagToString(tag).data(),
      result);*/
  return result;
}


// NtQuerySystemInformation�����Ĺ��Ӵ������ ɾ��cmd.exe����Ŀ
// �����������г���
_Use_decl_annotations_ static NTSTATUS DdimonpHandleNtQuerySystemInformation(
    SystemInformationClass system_information_class, PVOID system_information,
    ULONG system_information_length, PULONG return_length) {
  const auto original =
      DdimonpFindOrignal(DdimonpHandleNtQuerySystemInformation);
  const auto result = original(system_information_class, system_information,
                               system_information_length, return_length);
  if (!NT_SUCCESS(result)) {
    return result;
  }
  if (system_information_class != kSystemProcessInformation) 
  {
    return result;
  }

  auto next = reinterpret_cast<SystemProcessInformation*>(system_information);
  while (next->next_entry_offset) 
  {
    auto curr = next;
    next = reinterpret_cast<SystemProcessInformation*>(reinterpret_cast<UCHAR*>(curr) + curr->next_entry_offset);
	std::vector<Protection>::iterator it;
	for (it = lProtectionList.begin(); it != lProtectionList.end(); it++)
	{
		if (_wcsnicmp(next->image_name.Buffer, it->wcProcessName.c_str(), next->image_name.Length) == 0) {
			if (next->next_entry_offset)
			{
				curr->next_entry_offset += next->next_entry_offset;
			}
			else
			{
				curr->next_entry_offset = 0;
			}
			next = curr;
		}
	}
	
  }
  return result;
}

NTSTATUS DdimonpHandleNtOpenProcess(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PCLIENT_ID ClientId)
{
	VMProtectBegin("DdimonpHandleNtOpenProcess");
	PEPROCESS EProcess;
	const auto original = DdimonpFindOrignal(DdimonpHandleNtOpenProcess);
	auto return_addr = _ReturnAddress();
	const auto result = original(ProcessHandle, DesiredAccess,
		ObjectAttributes, ClientId);

	auto status = ObReferenceObjectByHandle(*ProcessHandle, FILE_READ_DATA, 0, KernelMode, (PVOID*)&EProcess, 0);
	if (status == STATUS_SUCCESS)
	{
		auto * FileName = PsGetProcessImageFileName(EProcess);
		if (FileName)
		{
			std::vector<Protection>::iterator it;
			for (it = lProtectionList.begin(); it != lProtectionList.end(); it++)
			{
				if (!strcmp((char*)FileName, it->cProcessName.c_str()) ||
					(HANDLE)it->dwProcessId == ClientId->UniqueProcess)
				{
					HYPERPLATFORM_LOG_INFO_SAFE("%p: NtOpenProcess(ProcessHandle= %d, DesiredAccess= %d,Pid= %d ,filename:%s)",
						return_addr, ProcessHandle, DesiredAccess, ClientId->UniqueProcess, FileName);
					ClientId->UniqueProcess = (HANDLE)-1;
					return STATUS_ACCESS_DENIED;
				}
			}
			
		}

		//ZwClose(ProcessHandle);
	}
	if (!NT_SUCCESS(result)) {
		return result;
	}
	
	VMProtectEnd();
	return result;
}

NTSTATUS DdimonpHandleNtCreateUserProcess(OUT PHANDLE ProcessHandle, OUT PHANDLE ThreadHandle,
	IN ACCESS_MASK ProcessDesiredAccess, IN ACCESS_MASK ThreadDesiredAccess, IN POBJECT_ATTRIBUTES ProcessObjectAttributes OPTIONAL,
	IN POBJECT_ATTRIBUTES ThreadObjectAttributes OPTIONAL, IN ULONG CreateProcessFlags, IN ULONG CreateThreadFlags, 
	IN PRTL_USER_PROCESS_PARAMETERS ProcessParameters, IN PVOID Parameter9, IN PNT_PROC_THREAD_ATTRIBUTE_LIST AttributeList)
{
	VMProtectBegin("DdimonpHandleNtCreateUserProcess");
	PEPROCESS EProcess;
	const auto original = DdimonpFindOrignal(DdimonpHandleNtCreateUserProcess);
	auto return_addr = _ReturnAddress();
	const auto hProcessID = PsGetCurrentProcessId();
	const auto result = original(ProcessHandle, ThreadHandle,
		ProcessDesiredAccess, ThreadDesiredAccess, ProcessObjectAttributes,
		ThreadObjectAttributes,CreateProcessFlags, CreateThreadFlags,
		ProcessParameters, Parameter9, AttributeList);

	HYPERPLATFORM_LOG_INFO_SAFE("%p: NtCreateUserProcess:(ProcessHandle= %d)",
		return_addr, ProcessHandle);
	return result;
}

NTSTATUS DdimonpHandleNtCreateProcessEx(
	OUT PHANDLE ProcessHandle, 
	IN ACCESS_MASK DesiredAccess, 
	IN POBJECT_ATTRIBUTES ObjectAttributes, 
	IN HANDLE InheritFromProcessHandle, 
	IN BOOLEAN InheritHandles, 
	IN HANDLE SectionHandle OPTIONAL, 
	IN HANDLE DebugPort OPTIONAL, 
	IN HANDLE ExceptionPort OPTIONAL, 
	IN HANDLE Unknown)
{
	VMProtectBegin("DdimonpHandleNtCreateProcessEx");
	PEPROCESS EProcess;
	const auto original = DdimonpFindOrignal(DdimonpHandleNtCreateProcessEx);
	auto return_addr = _ReturnAddress();
	const auto hProcessID = PsGetCurrentProcessId();
	const auto result = original(ProcessHandle, DesiredAccess,
		ObjectAttributes, InheritFromProcessHandle, InheritHandles,
		SectionHandle, DebugPort, ExceptionPort, Unknown);
	
	HYPERPLATFORM_LOG_INFO_SAFE("%p: NtCreateProcessEx:(ProcessHandle= %d)",
		return_addr, ProcessHandle);
	VMProtectEnd();
	return result;
}
