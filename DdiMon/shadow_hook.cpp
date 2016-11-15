// Copyright (c) 2015-2016, tandasat. All rights reserved.
// Use of this source code is governed by a MIT-style license that can be
// found in the LICENSE file.

/// @file
/// Implements shadow hook functions.

#include "shadow_hook.h"
#include <ntimage.h>
#define NTSTRSAFE_NO_CB_FUNCTIONS
#include <ntstrsafe.h>
#include "../HyperPlatform/HyperPlatform/common.h"
#include "../HyperPlatform/HyperPlatform/log.h"
#include "../HyperPlatform/HyperPlatform/util.h"
#include "../HyperPlatform/HyperPlatform/ept.h"
#include "../HyperPlatform/HyperPlatform/kernel_stl.h"
#include <vector>
#include <memory>
#include <algorithm>
#include "capstone.h"

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

// Copy of a page seen by a guest as a result of memory shadowing
// ��Ϊ�ڴ���Ӱ�Ľ����guest������鿴��ҳ��ĸ���
struct Page {
  UCHAR* page;  // A page aligned copy of a page
  Page();
  ~Page();
};

// Contains a single steal thook information
//����������ȡ��Ϣ
struct HookInformation {
  void* patch_address;  // An address where a hook is installed
  void* handler;        // An address of the handler routine

  // A copy of a pages where patch_address belongs to. shadow_page_base_for_rw
  // is exposed to a guest for read and write operation against the page of
  // patch_address, and shadow_page_base_for_exec is exposed for execution.
  // patch_address����ҳ��ĸ����� shadow_page_base_for_rw
  // ����¶���ͻ����ж�ȡ��д�������ҳ��
  // patch_address��shadow_page_base_for_exec��¶ִ�С�
  std::shared_ptr<Page> shadow_page_base_for_rw;
  std::shared_ptr<Page> shadow_page_base_for_exec;

  // Physical address of the above two copied pages
  //������������ҳ��������ַ
  ULONG64 pa_base_for_rw;
  ULONG64 pa_base_for_exec;
};

// Data structure shared across all processors
struct SharedShadowHookData {
  std::vector<std::unique_ptr<HookInformation>> hooks;  // Hold installed hooks
};

// Data structure for each processor
//ÿ�������������ݽṹ
struct ShadowHookData {
  const HookInformation* last_hook_info;  // Remember which hook hit the last
};

// A structure reflects inline hook code.
//�ṹ��ӳ����hook���롣
#include <pshpack1.h>
#if defined(_AMD64_)

struct TrampolineCode {
  UCHAR nop;
  UCHAR jmp[6];
  void* address;
};
static_assert(sizeof(TrampolineCode) == 15, "Size check");

#else

struct TrampolineCode {
  UCHAR nop;
  UCHAR push;
  void* address;
  UCHAR ret;
};
static_assert(sizeof(TrampolineCode) == 7, "Size check");

#endif
#include <poppack.h>

////////////////////////////////////////////////////////////////////////////////
//
// prototypes
//

_IRQL_requires_max_(PASSIVE_LEVEL) static std::unique_ptr<
    HookInformation> ShpCreateHookInformation(_In_ SharedShadowHookData*
                                                  shared_sh_data,
                                              _In_ void* address,
                                              _In_ ShadowHookTarget* target);

_IRQL_requires_max_(PASSIVE_LEVEL) _Success_(return ) EXTERN_C
    static bool ShpSetupInlineHook(_In_ void* patch_address,
                                   _In_ UCHAR* shadow_exec_page,
                                   _Out_ void** original_call_ptr);

_IRQL_requires_max_(PASSIVE_LEVEL) EXTERN_C static SIZE_T
    ShpGetInstructionSize(_In_ void* address);

_IRQL_requires_max_(PASSIVE_LEVEL) EXTERN_C static TrampolineCode
    ShpMakeTrampolineCode(_In_ void* hook_handler);

static HookInformation* ShpFindPatchInfoByPage(
    _In_ const SharedShadowHookData* shared_sh_data, _In_ void* address);

static HookInformation* ShpFindPatchInfoByAddress(
    _In_ const SharedShadowHookData* shared_sh_data, _In_ void* address);

static void ShpEnablePageShadowingForExec(_In_ const HookInformation& info,
                                          _In_ EptData* ept_data);

static void ShpEnablePageShadowingForRW(_In_ const HookInformation& info,
                                        _In_ EptData* ept_data);

static void ShpDisablePageShadowing(_In_ const HookInformation& info,
                                    _In_ EptData* ept_data);

static void ShpSetMonitorTrapFlag(_In_ ShadowHookData* sh_data,
                                  _In_ bool enable);

static void ShpSaveLastHookInfo(_In_ ShadowHookData* sh_data,
                                _In_ const HookInformation& info);

static const HookInformation* ShpRestoreLastHookInfo(
    _In_ ShadowHookData* sh_data);

static bool ShpIsShadowHookActive(
    _In_ const SharedShadowHookData* shared_sh_data);

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(INIT, ShAllocateShadowHookData)
#pragma alloc_text(INIT, ShAllocateSharedShaowHookData)
#pragma alloc_text(INIT, ShEnableHooks)
#pragma alloc_text(INIT, ShInstallHook)
#pragma alloc_text(INIT, ShpSetupInlineHook)
#pragma alloc_text(INIT, ShpGetInstructionSize)
#pragma alloc_text(INIT, ShpMakeTrampolineCode)
#pragma alloc_text(PAGE, ShFreeShadowHookData)
#pragma alloc_text(PAGE, ShFreeSharedShadowHookData)
#pragma alloc_text(PAGE, ShDisableHooks)
#endif

////////////////////////////////////////////////////////////////////////////////
//
// variables
//

////////////////////////////////////////////////////////////////////////////////
//
// implementations
//

// Allocates per-processor shadow hook data
//�����ÿ��������Ӱ�ӹ�������
_Use_decl_annotations_ EXTERN_C ShadowHookData* ShAllocateShadowHookData() {
  PAGED_CODE();

  auto p = new ShadowHookData();
  RtlFillMemory(p, sizeof(ShadowHookData), 0);
  return p;
}

// Frees per-processor shadow hook data
_Use_decl_annotations_ EXTERN_C void ShFreeShadowHookData(
    ShadowHookData* sh_data) {
  PAGED_CODE();

  delete sh_data;
}

// Allocates processor-shared shadow hook data
//���䴦��������Ӱ�ӹ�������
_Use_decl_annotations_ EXTERN_C SharedShadowHookData*
ShAllocateSharedShaowHookData() {
  PAGED_CODE();

  auto p = new SharedShadowHookData();
  RtlFillMemory(p, sizeof(SharedShadowHookData), 0);
  return p;
}

// Frees processor-shared shadow hook data
_Use_decl_annotations_ EXTERN_C void ShFreeSharedShadowHookData(
    SharedShadowHookData* shared_sh_data) {
  PAGED_CODE();

  delete shared_sh_data;
}

// Enables page shadowing for all hooks
//Ϊ���й�������ҳ����Ӱ
_Use_decl_annotations_ EXTERN_C NTSTATUS ShEnableHooks() {
  PAGED_CODE();

  return UtilForEachProcessor(
      [](void* context) {
        UNREFERENCED_PARAMETER(context);
        return UtilVmCall(HypercallNumber::kShEnablePageShadowing, nullptr);
      },
      nullptr);
}

// Disables page shadowing for all hooks
//�������йҹ���ҳ����Ӱ
_Use_decl_annotations_ EXTERN_C NTSTATUS ShDisableHooks() {
  PAGED_CODE();

  return UtilForEachProcessor(
      [](void* context) {
        UNREFERENCED_PARAMETER(context);
        return UtilVmCall(HypercallNumber::kShDisablePageShadowing, nullptr);
      },
      nullptr);
}

// Enables page shadowing for all hooks
//Ϊ���й�������ҳ����Ӱ
_Use_decl_annotations_ NTSTATUS ShEnablePageShadowing(
    EptData* ept_data, const SharedShadowHookData* shared_sh_data) {
  //HYPERPLATFORM_COMMON_DBG_BREAK();

  for (auto& info : shared_sh_data->hooks) {
    ShpEnablePageShadowingForExec(*info, ept_data);
  }
  return STATUS_SUCCESS;
}

// Disables page shadowing for all hooks
//�������йҹ���ҳ����Ӱ
_Use_decl_annotations_ void ShVmCallDisablePageShadowing(
    EptData* ept_data, const SharedShadowHookData* shared_sh_data) {
  //HYPERPLATFORM_COMMON_DBG_BREAK();

  for (auto& info : shared_sh_data->hooks) {
    ShpDisablePageShadowing(*info, ept_data);
  }
}

// Handles #BP�� ���#BP�Ƿ�����DeMon���öϵ�ĵط���
// ����ǣ��޸Ŀͻ�IP��������ִ����Ӧ��
// hook�������
_Use_decl_annotations_ bool ShHandleBreakpoint(
    ShadowHookData* sh_data, const SharedShadowHookData* shared_sh_data,
    void* guest_ip) {
  UNREFERENCED_PARAMETER(sh_data);

  if (!ShpIsShadowHookActive(shared_sh_data)) {
    return false;
  }

  const auto info = ShpFindPatchInfoByAddress(shared_sh_data, guest_ip);
  if (!info) {
    return false;
  }

  // Update guest's IP
  UtilVmWrite(VmcsField::kGuestRip, reinterpret_cast<ULONG_PTR>(info->handler));
  return true;
}

//����MTF VM�˳��� ��������Ӱ�ӹ������MTF��
_Use_decl_annotations_ void ShHandleMonitorTrapFlag(
    ShadowHookData* sh_data, const SharedShadowHookData* shared_sh_data,
    EptData* ept_data) {
  NT_VERIFY(ShpIsShadowHookActive(shared_sh_data));

  const auto info = ShpRestoreLastHookInfo(sh_data);
  ShpEnablePageShadowingForExec(*info, ept_data);
  ShpSetMonitorTrapFlag(sh_data, false);
}

//����EPTΥ��VM�˳���
_Use_decl_annotations_ void ShHandleEptViolation(
    ShadowHookData* sh_data, const SharedShadowHookData* shared_sh_data,
    EptData* ept_data, void* fault_va) {
  if (!ShpIsShadowHookActive(shared_sh_data)) {
    return;
  }

  const auto info = ShpFindPatchInfoByPage(shared_sh_data, fault_va);
  if (!info) {
    return;
  }

  // EPT violation was caused because a guest tried to read or write to a page
  // where currently set as execute only for protecting a hook. Let a guest
  // read or write a page from a read/write shadow page and run a single
  // instruction.
  // EPTΥ������Ϊ�ͻ��˳��Զ�ȡ��д��ҳ��
  // ���е�ǰ����Ϊ��ִ���Ա����ҹ��� �ÿ���
  // �Ӷ�/дӰ��ҳ���ȡ��д��ҳ�沢����һ��
  // ָ�
  ShpEnablePageShadowingForRW(*info, ept_data);
  ShpSetMonitorTrapFlag(sh_data, true);
  ShpSaveLastHookInfo(sh_data, *info);
}

// Set up inline hook at the address without activating it
// �ڵ�ַ�������������Ӷ���������
_Use_decl_annotations_ EXTERN_C bool ShInstallHook(
    SharedShadowHookData* shared_sh_data, void* address,
    ShadowHookTarget* target) {
  PAGED_CODE();

  auto info = ShpCreateHookInformation(
      shared_sh_data, reinterpret_cast<void*>(address), target);
  if (!info) {
    return false;
  }

  if (!ShpSetupInlineHook(info->patch_address,
                          info->shadow_page_base_for_exec->page,
                          &target->original_call)) {
    return false;
  }

  HYPERPLATFORM_LOG_DEBUG(
      "Patch = %p, Exec = %p, RW = %p, Trampoline = %p", info->patch_address,
      info->shadow_page_base_for_exec->page + BYTE_OFFSET(info->patch_address),
      info->shadow_page_base_for_rw->page + BYTE_OFFSET(info->patch_address),
      target->original_call);

  shared_sh_data->hooks.push_back(std::move(info));
  return true;
}

// Creates or reuses a couple of copied pages and initializes HookInformation
// ��������һЩ���Ƶ�ҳ�沢��ʼ��Hook��Ϣ
_Use_decl_annotations_ static std::unique_ptr<HookInformation>
ShpCreateHookInformation(SharedShadowHookData* shared_sh_data, void* address,
                         ShadowHookTarget* target) {
  auto info = std::make_unique<HookInformation>();
  auto reusable_info = ShpFindPatchInfoByPage(shared_sh_data, address);
  if (reusable_info) {
    // Found an existing HookInformation object targeting the same page as this
    // one. re-use shadow pages.
	// �ҵ�һ��Ŀ�����ҳ����ͬ������HookInformation����
	// һ���� ����ʹ��Ӱ��ҳ�档
    info->shadow_page_base_for_rw = reusable_info->shadow_page_base_for_rw;
    info->shadow_page_base_for_exec = reusable_info->shadow_page_base_for_exec;
  } else {
    // This hook is for a page that is not currently have any hooks (i.e., not
    // shadowed). Creates shadow pages.
	// �˹������ڵ�ǰû���κι��ӵ�ҳ�棨��������
	// shadowed���� ����Ӱ��ҳ��
    info->shadow_page_base_for_rw = std::make_shared<Page>();
    info->shadow_page_base_for_exec = std::make_shared<Page>();
    auto page_base = PAGE_ALIGN(address);
    RtlCopyMemory(info->shadow_page_base_for_rw->page, page_base, PAGE_SIZE);
    RtlCopyMemory(info->shadow_page_base_for_exec->page, page_base, PAGE_SIZE);
  }
  info->patch_address = address;
  info->pa_base_for_rw = UtilPaFromVa(info->shadow_page_base_for_rw->page);
  info->pa_base_for_exec = UtilPaFromVa(info->shadow_page_base_for_exec->page);
  info->handler = target->handler;
  return info;
}

// Builds a trampoline code for calling an original code and embeds 0xcc on the
// shadow_exec_page
// ����һ��trampoline���룬���ڵ���ԭʼ���벢������Ƕ��0xcc
// shadow_exec_page
_Use_decl_annotations_ EXTERN_C static bool ShpSetupInlineHook(
    void* patch_address, UCHAR* shadow_exec_page, void** original_call_ptr) {
  PAGED_CODE();

  const auto patch_size = ShpGetInstructionSize(patch_address);
  if (!patch_size) {
    return false;
  }

  // Build trampoline code (copied stub -> in the middle of original)
  // ����trampoline���루���Ƶ�stub - >��ԭ�����м䣩
  const auto jmp_to_original = ShpMakeTrampolineCode(
      reinterpret_cast<UCHAR*>(patch_address) + patch_size);
#pragma warning(push)
#pragma warning(disable : 30030)  // Allocating executable POOL_TYPE memory
  const auto original_call = ExAllocatePoolWithTag(
      NonPagedPoolExecute, patch_size + sizeof(jmp_to_original),
      kHyperPlatformCommonPoolTag);
#pragma warning(pop)
  if (!original_call) {
    return false;
  }

  // Copy original code and embed jump code following original code
  // ����ԭʼ���룬����ԭʼ�������Ƕ����ת����
  RtlCopyMemory(original_call, patch_address, patch_size);
#pragma warning(push)
#pragma warning(disable : 6386)
  // Buffer overrun while writing to 'reinterpret_cast<UCHAR
  // *>original_call+patch_size':  the writable size is
  // 'patch_size+sizeof((jmp_to_original))' bytes, but '15' bytes might be
  // written.
  //д��'reinterpret_cast <UCHARʱ���������
  // *> original_call + patch_size'����д��СΪ
  //'patch_size + sizeof����jmp_to_original����'�ֽڣ�����'15'�ֽڿ���
  //д��
  RtlCopyMemory(reinterpret_cast<UCHAR*>(original_call) + patch_size,
                &jmp_to_original, sizeof(jmp_to_original));
#pragma warning(pop)

  // install patch to shadow page
  // ��װ������Ӱ��ҳ��
  static const UCHAR kBreakpoint[] = {
      0xcc,
  };
  RtlCopyMemory(shadow_exec_page + BYTE_OFFSET(patch_address), kBreakpoint,
                sizeof(kBreakpoint));

  KeInvalidateAllCaches();

  *original_call_ptr = original_call;
  return true;
}

// Returns a size of an instruction at the address
// ���ص�ַ��ָ��Ĵ�С
_Use_decl_annotations_ EXTERN_C static SIZE_T ShpGetInstructionSize(
    void* address) {
  PAGED_CODE();

  // Save floating point state
  // ���渡��״̬
  KFLOATING_SAVE float_save = {};
  auto status = KeSaveFloatingPointState(&float_save);
  if (!NT_SUCCESS(status)) {
    return 0;
  }

  // Disassemble at most 15 bytes to get an instruction size
  // ������15���ֽ��Ի�ȡָ���С
  csh handle = {};
  const auto mode = IsX64() ? CS_MODE_64 : CS_MODE_32;
  if (cs_open(CS_ARCH_X86, mode, &handle) != CS_ERR_OK) {
    KeRestoreFloatingPointState(&float_save);
    return 0;
  }

  static const auto kLongestInstSize = 15;
  cs_insn* instructions = nullptr;
  const auto count =
      cs_disasm(handle, reinterpret_cast<uint8_t*>(address), kLongestInstSize,
                reinterpret_cast<uint64_t>(address), 1, &instructions);
  if (count == 0) {
    cs_close(&handle);
    KeRestoreFloatingPointState(&float_save);
    return 0;
  }

  // Get a size of the first instruction
  // ��ȡ��һ��ָ��Ĵ�С
  const auto size = instructions[0].size;
  cs_free(instructions, count);
  cs_close(&handle);

  // Restore floating point state
  // �ָ�����״̬
  KeRestoreFloatingPointState(&float_save);
  return size;
}

// Returns code bytes for inline hooking
// ���������ҹ��Ĵ����ֽ�
_Use_decl_annotations_ EXTERN_C static TrampolineCode ShpMakeTrampolineCode(
    void* hook_handler) {
  PAGED_CODE();

#if defined(_AMD64_)
  // 90               nop
  // ff2500000000     jmp     qword ptr cs:jmp_addr
  // jmp_addr:
  // 0000000000000000 dq 0
  return {
      0x90,
      {
          0xff, 0x25, 0x00, 0x00, 0x00, 0x00,
      },
      hook_handler,
  };
#else
  // 90               nop
  // 6832e30582       push    offset nt!ExFreePoolWithTag + 0x2 (8205e332)
  // c3               ret
  return {
      0x90, 0x68, hook_handler, 0xc3,
  };
#endif
}

// Find a HookInformation instance by address
// ����ַ����Hook��Ϣʵ��
_Use_decl_annotations_ static HookInformation* ShpFindPatchInfoByPage(
    const SharedShadowHookData* shared_sh_data, void* address) {
  const auto found = std::find_if(
      shared_sh_data->hooks.cbegin(), shared_sh_data->hooks.cend(),
      [address](const auto& info) {
        return PAGE_ALIGN(info->patch_address) == PAGE_ALIGN(address);
      });
  if (found == shared_sh_data->hooks.cend()) {
    return nullptr;
  }
  return found->get();
}

// Find a HookInformation instance that are on the same page as the address
// �������ַ��ͬһҳ���ϵ�Hook��Ϣʵ��
_Use_decl_annotations_ static HookInformation* ShpFindPatchInfoByAddress(
    const SharedShadowHookData* shared_sh_data, void* address) {
  auto found = std::find_if(
      shared_sh_data->hooks.cbegin(), shared_sh_data->hooks.cend(),
      [address](const auto& info) { return info->patch_address == address; });
  if (found == shared_sh_data->hooks.cend()) {
    return nullptr;
  }
  return found->get();
}

// Show a shadowed page for execution
// ��ʾ��Ӱҳ���Թ�ִ��
_Use_decl_annotations_ static void ShpEnablePageShadowingForExec(
    const HookInformation& info, EptData* ept_data) {
  const auto ept_pt_entry =
      EptGetEptPtEntry(ept_data, UtilPaFromVa(info.patch_address));

  // Allow the VMM to redirect read and write access to the address by denying
  // those accesses and handling them on EPT violation
  // ����VMMͨ���ܾ��ض���Ե�ַ�Ķ�д����
  // ��Щ���ʲ���EPTΥ��ʱ��������
  ept_pt_entry->fields.write_access = false;
  ept_pt_entry->fields.read_access = false;

  // Only execution is allowed on the adresss. Show the copied page for exec
  // that has an actual breakpoint to the guest.
  // ֻ����Ե�ִַ�С� ��ʾ�Կͻ��˾���ʵ�ʶϵ��exec 
  // �ĸ���ҳ�档
  ept_pt_entry->fields.physial_address = UtilPfnFromPa(info.pa_base_for_exec);

  UtilInveptGlobal();
}

// Show a shadowed page for read and write
// ��ʾ���ڶ�д����Ӱҳ��
_Use_decl_annotations_ static void ShpEnablePageShadowingForRW(
    const HookInformation& info, EptData* ept_data) {
  const auto ept_pt_entry =
      EptGetEptPtEntry(ept_data, UtilPaFromVa(info.patch_address));

  // Allow a guest to read and write as well as execute the address. Show the
  // copied page for read/write that does not have an breakpoint but reflects
  // all modification by a guest if that happened.
  // ����ͻ��˶�д���Լ�ִ�е�ַ�� ��ʾ
  // ���Ƶ�ҳ�棬����û�жϵ㵫��ӳ�Ķ�/д
  // ���˵������޸ģ���������ˡ�
  ept_pt_entry->fields.write_access = true;
  ept_pt_entry->fields.read_access = true;
  ept_pt_entry->fields.physial_address = UtilPfnFromPa(info.pa_base_for_rw);

  UtilInveptGlobal();
}

// Stop showing a shadow page
// ֹͣ��ʾ��Ӱҳ��
_Use_decl_annotations_ static void ShpDisablePageShadowing(
    const HookInformation& info, EptData* ept_data) {
  const auto pa_base = UtilPaFromVa(PAGE_ALIGN(info.patch_address));
  const auto ept_pt_entry = EptGetEptPtEntry(ept_data, pa_base);
  ept_pt_entry->fields.write_access = true;
  ept_pt_entry->fields.read_access = true;
  ept_pt_entry->fields.physial_address = UtilPfnFromPa(pa_base);

  UtilInveptGlobal();
}

// Set MTF on the current processor
// �ڵ�ǰ������������MTF
_Use_decl_annotations_ static void ShpSetMonitorTrapFlag(
    ShadowHookData* sh_data, bool enable) {
  VmxProcessorBasedControls vm_procctl = {
      static_cast<unsigned int>(UtilVmRead(VmcsField::kCpuBasedVmExecControl))};
  vm_procctl.fields.monitor_trap_flag = enable;
  UtilVmWrite(VmcsField::kCpuBasedVmExecControl, vm_procctl.all);
}

// Saves HookInformation as the last one for reusing it on up coming MTF VM-exit
// ��Hook��Ϣ����Ϊ���һ������һ��MTF VM�˳�ʱ����������Ϣ
_Use_decl_annotations_ static void ShpSaveLastHookInfo(
    ShadowHookData* sh_data, const HookInformation& info) {
  NT_ASSERT(!sh_data->last_hook_info);
  sh_data->last_hook_info = &info;
}

// Retrieves the last HookInformation
// ��������Hooke��Ϣ
_Use_decl_annotations_ static const HookInformation* ShpRestoreLastHookInfo(
    ShadowHookData* sh_data) {
  NT_ASSERT(sh_data->last_hook_info);
  auto info = sh_data->last_hook_info;
  sh_data->last_hook_info = nullptr;
  return info;
}

// Checks if DdiMon is already initialized
// ���DaeMon�Ƿ��Ѿ���ʼ��
_Use_decl_annotations_ static bool ShpIsShadowHookActive(
    const SharedShadowHookData* shared_sh_data) {
  return !!(shared_sh_data);
}

// Allocates a non-paged, page-aligned page. Issues bug check on failure
// ����δ��ҳ��ҳ�����ҳ�档 ��ʧ��ʱ����������
Page::Page()
    : page(reinterpret_cast<UCHAR*>(ExAllocatePoolWithTag(
          NonPagedPool, PAGE_SIZE, kHyperPlatformCommonPoolTag))) {
  if (!page) {
    HYPERPLATFORM_COMMON_BUG_CHECK(
        HyperPlatformBugCheck::kCritialPoolAllocationFailure, 0, 0, 0);
  }
}

// De-allocates the allocated page
//�ͷŷ����ҳ��
Page::~Page() { ExFreePoolWithTag(page, kHyperPlatformCommonPoolTag); }
