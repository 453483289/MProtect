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

// A callback type for EnumExportedSymbols()
//ö�ٵ������ŵĻص����ͣ���
using EnumExportedSymbolsCallbackType = bool (*)(
    ULONG index, ULONG_PTR base_address, PIMAGE_EXPORT_DIRECTORY directory,
    ULONG_PTR directory_base, ULONG_PTR directory_end, void* context, BOOLEAN ssdt);

typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemBasicInformation,
	SystemProcessorInformation,
	SystemPerformanceInformation,
	SystemTimeOfDayInformation,
	SystemNotImplemented1,
	SystemProcessesAndThreadsInformation,
	SystemCallCounts,
	SystemConfigurationInformation,
	SystemProcessorTimes,
	SystemGlobalFlag,
	SystemNotImplemented2,
	SystemModuleInformation,
	SystemLockInformation,
	SystemNotImplemented3,
	SystemNotImplemented4,
	SystemNotImplemented5,
	SystemHandleInformation,
	SystemObjectInformation,
	SystemPagefileInformation,
	SystemInstructionEmulationCounts,
	SystemInvalidInfoClass1,
	SystemCacheInformation,
	SystemPoolTagInformation,
	SystemProcessorStatistics,
	SystemDpcInformation,
	SystemNotImplemented6,
	SystemLoadImage,
	SystemUnloadImage,
	SystemTimeAdjustment,
	SystemNotImplemented7,
	SystemNotImplemented8,
	SystemNotImplemented9,
	SystemCrashDumpInformation,
	SystemExceptionInformation,
	SystemCrashDumpStateInformation,
	SystemKernelDebuggerInformation,
	SystemContextSwitchInformation,
	SystemRegistryQuotaInformation,
	SystemLoadAndCallImage,
	SystemPrioritySeparation,
	SystemNotImplemented10,
	SystemNotImplemented11,
	SystemInvalidInfoClass2,
	SystemInvalidInfoClass3,
	SystemTimeZoneInformation,
	SystemLookasideInformation,
	SystemSetTimeSlipEvent,
	SystemCreateSession,
	SystemDeleteSession,
	SystemInvalidInfoClass4,
	SystemRangeStartInformation,
	SystemVerifierInformation,
	SystemAddVerifier,
	SystemSessionProcessesInformation
} SYSTEM_INFORMATION_CLASS, *PSYSTEM_INFORMATION_CLASS;
//------------------------------

//---------�߳���Ϣ�ṹ--------- 
typedef struct _SYSTEM_THREAD {
	LARGE_INTEGER           KernelTime;
	LARGE_INTEGER           UserTime;
	LARGE_INTEGER           CreateTime;
	ULONG                   WaitTime;
	PVOID                   StartAddress;
	CLIENT_ID               ClientId;
	KPRIORITY               Priority;
	LONG                    BasePriority;
	ULONG                   ContextSwitchCount;
	ULONG                   State;
	KWAIT_REASON            WaitReason;
} SYSTEM_THREAD, *PSYSTEM_THREAD;
//------------------------------

//---------������Ϣ�ṹ--------- 
typedef struct _SYSTEM_PROCESS_INFORMATION {
	ULONG                   NextEntryOffset; //NextEntryDelta ���ɽṹ���е�ƫ���� 
	ULONG                   NumberOfThreads; //�߳���Ŀ 
	LARGE_INTEGER           Reserved[3];
	LARGE_INTEGER           CreateTime;   //����ʱ�� 
	LARGE_INTEGER           UserTime;     //�û�ģʽ(Ring 3)��CPUʱ�� 
	LARGE_INTEGER           KernelTime;   //�ں�ģʽ(Ring 0)��CPUʱ�� 
	UNICODE_STRING          ImageName;    //�������� 
	KPRIORITY               BasePriority; //��������Ȩ 
	HANDLE                  ProcessId;    //ULONG UniqueProcessId ���̱�ʶ�� 
	HANDLE                  InheritedFromProcessId; //�����̵ı�ʶ�� 
	ULONG                   HandleCount; //�����Ŀ 
	ULONG                   Reserved2[2];
	ULONG                   PrivatePageCount;
	VM_COUNTERS             VirtualMemoryCounters; //����洢���Ľṹ 
	IO_COUNTERS             IoCounters; //IO�����ṹ 
	SYSTEM_THREAD           Threads[0]; //��������̵߳Ľṹ���� 
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

typedef struct _LDR_DATA_TABLE_ENTRY
{
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	DWORD SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	DWORD Flags;
	WORD LoadCount;
	WORD TlsIndex;
	LIST_ENTRY HashLinks;
	PVOID SectionPointer;
	DWORD CheckSum;
	DWORD TimeDateStamp;
	PVOID LoadedImports;
	PVOID EntryPointActivationContext;
	PVOID PatchInformation;
}LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

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

typedef struct _PEB_LDR_DATA {
	BYTE       Reserved1[8];
	PVOID      Reserved2[3];
	LIST_ENTRY InMemoryOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _RTL_USER_PROCESS_PARAMETERS {
	BYTE           Reserved1[16];
	PVOID          Reserved2[10];
	UNICODE_STRING ImagePathName;
	UNICODE_STRING CommandLine;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef ULONG   PPS_POST_PROCESS_INIT_ROUTINE;

typedef struct _PEB {
	BYTE                          Reserved1[2];
	BYTE                          BeingDebugged;
	BYTE                          Reserved2[1];
	PVOID                         Reserved3[2];
	PPEB_LDR_DATA                 Ldr;
	PRTL_USER_PROCESS_PARAMETERS  ProcessParameters;
	BYTE                          Reserved4[104];
	PVOID                         Reserved5[52];
	PPS_POST_PROCESS_INIT_ROUTINE PostProcessInitRoutine;
	BYTE                          Reserved6[128];
	PVOID                         Reserved7[1];
	ULONG                         SessionId;
} PEB, *PPEB;

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

typedef PPEB(__stdcall *PFNPsGetProcessPeb)(PEPROCESS pEProcess);
////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//
extern "C" {
	NTKERNELAPI UCHAR *NTAPI PsGetProcessImageFileName(_In_ PEPROCESS process);
	NTSTATUS NTAPI
		NtQuerySystemInformation(IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
			OUT PVOID SystemInformation,
			IN ULONG SystemInformationLength,
			OUT PULONG ReturnLength OPTIONAL);

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

//������
struct Protection {
	std::wstring wcProcessName;
	std::string cProcessName;
	DWORD dwProcessId;
};

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

// Defines where to install shadow hooks and their handlers
//
// Because of simplified implementation of DdiMon, DdiMon is unable to handle any
// of following exports properly:
//  - already unmapped exports (eg, ones on the INIT section) because it no
//    longer exists on memory
//  - exported data because setting 0xcc does not make any sense in this case
//  - functions does not comply x64 calling conventions, for example Zw*
//    functions. Because contents of stack do not hold expected values leading
//    handlers to failure of parameter analysis that may result in bug check.
//
// Also the following care should be taken:
//  - Function parameters may be an user-address space pointer and not
//    trusted. Even a kernel-address space pointer should not be trusted for
//    production level security. Verity and capture all contents from user
//    supplied address to VMM, then use them.
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
	{ L"���ƶ�.vshost.exe","���ƶ�.vshost.exe",0 },
	{ L"���ƶ�.exe","���ƶ�.exe",0 },
	{ L"lulujxjs.vshost.exe","lulujxjs.vshost.exe",0 },
	{ L"lulujxjs.exe","lulujxjs.exe",0 },
};



////////////////////////////////////////////////////////////////////////////////
//
// implementations
//
_Use_decl_annotations_ EXTERN_C NTSTATUS 
GetProcessModules(HANDLE ulProcessId)
{
	NTSTATUS nStatus;
	//PEB�ṹָ��  
	PPEB pPEB = NULL;

	//EPROCESS�ṹָ��  
	PEPROCESS  pEProcess = NULL;

	//���ҵĺ�������  
	UNICODE_STRING uniFunctionName;

	//���̲�����Ϣ  
	PRTL_USER_PROCESS_PARAMETERS pParam = NULL;

	//LDR���ݽṹ  
	PPEB_LDR_DATA pPebLdrData = NULL;

	//LDR�������  
	PLDR_DATA_TABLE_ENTRY pLdrDataEntry = NULL;

	//����ͷ�ڵ㡢β�ڵ�  
	PLIST_ENTRY pListEntryStart = NULL;
	PLIST_ENTRY pListEntryEnd = NULL;

	//����ָ��  
	PFNPsGetProcessPeb  PsGetProcessPeb = NULL;

	//����APC״̬  
	KAPC_STATE KAPC = { 0 };

	//�Ƿ��Ѿ����ӵ�����  
	BOOLEAN bIsAttached = FALSE;

	__try
	{
		//��ȡ���̵�EPROCESS�ṹָ��  
		nStatus = PsLookupProcessByProcessId((HANDLE)ulProcessId, &pEProcess);
		if (!NT_SUCCESS(nStatus))
		{
			return STATUS_UNSUCCESSFUL;
		}

		//���Һ�����ַ  
		RtlInitUnicodeString(&uniFunctionName, L"PsGetProcessPeb");
		PsGetProcessPeb = (PFNPsGetProcessPeb)MmGetSystemRoutineAddress(&uniFunctionName);
		if (PsGetProcessPeb == NULL)
		{
			HYPERPLATFORM_LOG_INFO("Get PsGetProcessPeb Failed~!\n");
			return STATUS_UNSUCCESSFUL;
		}

		//��ȡPEBָ��  
		pPEB = PsGetProcessPeb(pEProcess);
		if (pPEB == NULL)
		{
			HYPERPLATFORM_LOG_INFO("Get pPEB Failed~!\n");
			return STATUS_UNSUCCESSFUL;
		}

		//���ӵ�����  
		KeStackAttachProcess(pEProcess, &KAPC);

		bIsAttached = TRUE;

		//ָ��LDR  
		pPebLdrData = pPEB->Ldr;

		//ͷ�ڵ㡢β�ڵ�  
		pListEntryStart = pListEntryEnd = pPebLdrData->InMemoryOrderModuleList.Flink;


		//��ʼ����_LDR_DATA_TABLE_ENTRY  
		do
		{
			//ͨ��_LIST_ENTRY��Flink��Ա��ȡ_LDR_DATA_TABLE_ENTRY�ṹ    
			pLdrDataEntry = (PLDR_DATA_TABLE_ENTRY)CONTAINING_RECORD(pListEntryStart, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

			//���DLLȫ·��  
			HYPERPLATFORM_LOG_INFO("%wZ \n", &pLdrDataEntry->FullDllName);

			pListEntryStart = pListEntryStart->Flink;

		} while (pListEntryStart != pListEntryEnd);


		//Detach����  
		if (bIsAttached != FALSE)
		{
			KeUnstackDetachProcess(&KAPC);
		}

		//�������ü���  
		if (pEProcess != NULL)
		{
			ObDereferenceObject(pEProcess);
			pEProcess = NULL;
		}
	}
	__except (1)
	{

		HYPERPLATFORM_LOG_INFO("__except !!");
	}
	

	return STATUS_SUCCESS;
}


_Use_decl_annotations_ EXTERN_C NTSTATUS
ThreadProc()
{
	VMProtectBegin("Driver_Dispatch");
	//�����̱߳����ú���PsTerminateSystemThreadǿ���߳̽�����������߳����޷��Զ��˳��ġ�      
	ULONG pNeededSize = 0;
	int iCount = 1;
	int bOver = 0;
	NTSTATUS status;
	ULONG uSize;
	PVOID pSi = NULL;
	PSYSTEM_PROCESS_INFORMATION pSpiNext = NULL;
	uSize = 0x8000;
	pSi = ExAllocatePoolWithTag(NonPagedPool, uSize, 'tag1');
	if (pSi != NULL){
		status = NtQuerySystemInformation(SystemProcessesAndThreadsInformation, pSi, uSize, &pNeededSize);
		HYPERPLATFORM_LOG_INFO("[Aliwy] SUCCESS uSize = %.8X, pNeededSize = %.8X, status = %.8X/n", uSize, pNeededSize, status);
		uSize = pNeededSize;
		status = NtQuerySystemInformation(SystemProcessesAndThreadsInformation, pSi, uSize, &pNeededSize);
		if (STATUS_SUCCESS == status){
			pSpiNext = (PSYSTEM_PROCESS_INFORMATION)pSi;
			while (TRUE){
				if (pSpiNext->ProcessId == 0 || (ULONG)pSpiNext->ProcessId == 4){
					HYPERPLATFORM_LOG_INFO("[Aliwy] %d - System Idle Process/n", pSpiNext->ProcessId);
				}
				else{
					HYPERPLATFORM_LOG_INFO("[Aliwy] %d - %wZ/n", pSpiNext->ProcessId, &pSpiNext->ImageName);
					GetProcessModules(pSpiNext->ProcessId);
				}
				if (pSpiNext->NextEntryOffset == 0){
					HYPERPLATFORM_LOG_INFO("[Aliwy] EnumProcess Over, Count is: %d/n", iCount);
					bOver = 1;
					break;
				}
				pSpiNext = (PSYSTEM_PROCESS_INFORMATION)((ULONG)pSpiNext + pSpiNext->NextEntryOffset);
				iCount++;
			}
			ExFreePool(pSi);
		}
		else{
			HYPERPLATFORM_LOG_INFO("[Aliwy] SUCCESS uSize = %.8X, pNeededSize = %.8X, status = %.8X/n", uSize, pNeededSize, status);

		}
	}

	PsTerminateSystemThread(STATUS_SUCCESS);
	VMProtectEnd();
	return STATUS_SUCCESS;
}

_Use_decl_annotations_ EXTERN_C NTSTATUS
GetKeServiceDescriptorTable64()
{
	VMProtectBegin("GetKeServiceDescriptorTable64");
#ifdef _WIN64
	PUCHAR StartSearchAddress = (PUCHAR)__readmsr(0xc0000082);
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
  NTSTATUS lstatus;
  HANDLE threadHandle = NULL;
  lstatus = PsCreateSystemThread(&threadHandle,
	  0,
	  NULL, //����THREAD_ALL_ACCESS  
	  NtCurrentProcess(),
	  NULL,
	  (PKSTART_ROUTINE)ThreadProc,
	  NULL);

  if (!NT_SUCCESS(lstatus)){
	  ;
  }

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

//#ifdef _WIN64
//  RtlInitUnicodeString(&pDllName, L"\\SystemRoot\\SysWOW64\\ntdll.dll");
//#else
  RtlInitUnicodeString(&pDllName, L"\\SystemRoot\\System32\\ntdll.dll");
//#endif

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
	  //HYPERPLATFORM_LOG_INFO("def api at %d %x %s.", ssdt_index, export_address, export_name);
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
	if (!NT_SUCCESS(result)) 
	{
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
