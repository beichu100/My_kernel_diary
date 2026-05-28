#include <ntddk.h>
#include "head.h"
#include "offset.h"

ULONG64 GetNtoskrnlbase() {
	Ptr_ZwQuerySystemInformation ZwQuerySystemInformation = NULL;
	UNICODE_STRING Routine;
	RtlInitUnicodeString(&Routine, L"ZwQuerySystemInformation");
	ZwQuerySystemInformation = (Ptr_ZwQuerySystemInformation)MmGetSystemRoutineAddress(&Routine);
	DbgPrint("[GetEProcess]ZwQuerySystemInformation Address:%p\n", ZwQuerySystemInformation);
	if (!ZwQuerySystemInformation) return NULL;
	ULONG ByteSize;
	NTSTATUS ret = ZwQuerySystemInformation(0xB, NULL, NULL, &ByteSize);
	DbgPrint("[GetEProcess]ZwQuerySystemInformation ByteSize:%llu\n", ByteSize);
	if (!ByteSize) return NULL;
	PRTL_PROCESS_MODULE Module = (PRTL_PROCESS_MODULE)ExAllocatePoolWithTag(NonPagedPool, ByteSize, 'prpm');
	DbgPrint("[GetEProcess]Module Address:%p\n", Module);
	if (!Module) return NULL;
	ret = ZwQuerySystemInformation(0xB, Module, ByteSize, &ByteSize);
	if (ret != STATUS_SUCCESS) {
		DbgPrint("[GetEProcess]ZwQuerySystemInformation error\n");
		ExFreePool(Module);
		return NULL;
	}
	ULONG64 base = (ULONG64)Module->Modules[0].ImageBase;
	DbgPrint("[GetEProcess]ntoskrnl base:%llu\n", base);
	ExFreePool(Module);
	return base;
}


VOID printProcessCallBack() {
	PUCHAR Base = ((PUCHAR)GetNtoskrnlbase()) + OFFSET_PspCreateProcessNotifyRoutine;
	for (int i=NULL;i < 64;i++) {
		ULONG64 Entry = ((PULONG64)Base)[i];
		if (!Entry) continue;
		ULONG64 EntryCont = Entry & 0xF;
		PEX_CALLBACK_ROUTINE_BLOCK block =(PEX_CALLBACK_ROUTINE_BLOCK)(Entry & ~0xF);
		DbgPrint("find i:[%d],Entry:%llu,EntryCont:%llu,callback:%p\n",i,Entry,EntryCont,block->CallbackRoutine);
	}
}

BOOLEAN ADDProcessCallBack(PULONG64 CallBack) {
	PUCHAR Base = ((PUCHAR)GetNtoskrnlbase()) + OFFSET_PspCreateProcessNotifyRoutine;
	for (int i = NULL;i < 64;i++) {
		ULONG64 Entry = ((PULONG64)Base)[i];
		if (!Entry) {
			PVOID block = ExAllocatePoolWithTag(NonPagedPool, 0x30, 'kCbE');
			RtlZeroMemory(block, 0x30);
			*(PULONG64)((PUCHAR)block + 0x00) = 0;
			*(PULONG64)((PUCHAR)block + 0x08) = (ULONG64)CallBack;
			*(PULONG64)((PUCHAR)block + 0x10) = 2;//ex版本标记
			*(PULONG64)((PUCHAR)block + 0x18) = 0;
			*(PULONG64)((PUCHAR)block + 0x20) = 0x6C57784502030000ULL;
			((PULONG64)Base)[i] = (ULONG64)block;
			DbgPrint("[callback]ADDProcessCallBack i[%d]\n",i);
			return TRUE;
		}
	}
	return FALSE;
}

BOOLEAN removeProcessCallBack(PULONG64 CallBack) {
	PUCHAR Base = ((PUCHAR)GetNtoskrnlbase()) + OFFSET_PspCreateProcessNotifyRoutine;
	for (int i = NULL;i < 64;i++) {
		ULONG64 Entry = ((PULONG64)Base)[i];
		if (Entry) {
			PEX_CALLBACK_ROUTINE_BLOCK block = (PEX_CALLBACK_ROUTINE_BLOCK)(Entry & ~0xF);
			if (block->CallbackRoutine == CallBack) {
				((PULONG64)Base)[i] = NULL;
				DbgPrint("[callback] ROMOVEProcessCallBack i[%d]\n", i);
				return TRUE;
			}
		}else continue;
	}
	return FALSE;
}
         


extern "C"                                                                                         
VOID MyProcessNotifyEx(PEPROCESS Process, HANDLE ProcessId,
	PPS_CREATE_NOTIFY_INFO CreateInfo) {
	
	if (CreateInfo) {
		UNICODE_STRING notepad;
		RtlInitUnicodeString(&notepad, L"NOTEPAD.EXE");
		PCUNICODE_STRING full = CreateInfo->ImageFileName;
		if (full->Length >= notepad.Length) {                                                                                                                               
			UNICODE_STRING tail;
			tail.Buffer = (PWCHAR)((PUCHAR)full->Buffer + full->Length - notepad.Length);
			tail.Length = notepad.Length;
			tail.MaximumLength = notepad.Length;
			if (RtlEqualUnicodeString(&tail, &notepad, TRUE)) {
				CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
				DbgPrint("[callback] BLOCKED notepad!\n");
			}
		}
	}else {                                                                                                                                           
		DbgPrint("[callback-] PID=%llu EXITED\n", (UINT64)ProcessId);
	}
}



VOID UnLoad(PDRIVER_OBJECT DriverObj) {
	if (removeProcessCallBack((PULONG64)MyProcessNotifyEx)) {
		DbgPrint("[callback]REMOVEProcessCallBack SUCESS\n");
	}else DbgPrint("[callback]REMOVEProcessCallBack ERROR\n");
	//if (NT_SUCCESS(PsSetCreateProcessNotifyRoutineEx(MyProcessNotifyEx, 1))) DbgPrint("[callback]romove PsSetCreateProcessNotifyRoutineEx SUCCESS\n");
	DbgPrint("[callback]UnLoad\n");
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObj, PUNICODE_STRING Path) {
	DbgPrint("[callback]DriverEntry\n");
	DriverObj->DriverUnload = UnLoad;
	printProcessCallBack();
	if (!ADDProcessCallBack((PULONG64)MyProcessNotifyEx)) {
		DbgPrint("[callback]ADDProcessCallBack ERROR\n");
	}
	printProcessCallBack();
	return STATUS_SUCCESS;
}