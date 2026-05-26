#include "ApiHead.h"
#include <ntddk.h>
// #include <ntifs.h>

#define ACTIVE_PROCESS_LINKS_OFFSET   0x2F0   // dt nt!_EPROCESS ActiveProcessLinks                                                                             
#define IMAGE_FILE_NAME_OFFSET        0x450   // dt nt!_EPROCESS ImageFileName                                                                                  
#define THREAD_LIST_HEAD_OFFSET       0x488   // dt nt!_EPROCESS ThreadListHead     
#define ETHREAD_THREAD_LIST_ENTRY_OFFSET  0x6B8
#define ETHREAD_CID_OFFSET 0x648        
#define ETHREAD_PKAPC_STATE 0x098  

                                                                                                                     
#define PS_ACTIVE_PROCESS_HEAD_OFFSET 0x438B40  // ? nt!PsActiveProcessHead - nt                                                                                
#define PSP_CID_TABLE_OFFSET          0x574530  // ? nt!PspCidTable - nt     

BOOLEAN MatchImageName(PUCHAR name, const char* target) {
	for (int i = 0; i < 15; i++) {
		if (name[i] != target[i]) return FALSE;
		if (name[i] == '\0') return TRUE;                                                                                               
	}
	return target[14] == '\0';                                                                                                
}

ULONG64 GetNtoskrnlbase() {
	Ptr_ZwQuerySystemInformation ZwQuerySystemInformation = NULL;
	UNICODE_STRING Routine;
	RtlInitUnicodeString(&Routine, L"ZwQuerySystemInformation");
	ZwQuerySystemInformation = (Ptr_ZwQuerySystemInformation)MmGetSystemRoutineAddress(&Routine);
	DbgPrint("[GetEProcess]ZwQuerySystemInformation Address:%p\n", ZwQuerySystemInformation);
	if (!ZwQuerySystemInformation) return NULL;
	ULONG ByteSize;
	NTSTATUS ret = ZwQuerySystemInformation(0xB,NULL,NULL,&ByteSize);
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
	DbgPrint("[GetEProcess]ntoskrnl base:%llu\n",base);
	ExFreePool(Module);
	return base;
}

void PrintALlProcess() {
	PULONG64 PS_ACTIVE_PROCESS_HEAD = (PULONG64)((PUCHAR)GetNtoskrnlbase() + PS_ACTIVE_PROCESS_HEAD_OFFSET);
	Plist_entry list_entry = (Plist_entry)PS_ACTIVE_PROCESS_HEAD;
	PULONG64 FristFlag = list_entry->first_Entry;
	PULONG64 EProcessFristBase = (PULONG64)((PUCHAR)FristFlag - ACTIVE_PROCESS_LINKS_OFFSET);
	PULONG64 EPS_Flink;
	do {
		EPS_Flink = (PULONG64)((PUCHAR)EProcessFristBase + ACTIVE_PROCESS_LINKS_OFFSET);
		//PULONG64 EPS_Blink = EProcessFristBase + ACTIVE_PROCESS_LINKS_OFFSET + 8;
		PULONG64 EPS_Image_File_Nname = (PULONG64)((PUCHAR)EProcessFristBase + IMAGE_FILE_NAME_OFFSET);
		DbgPrint("[GetEProcess]Process name:%s\n", EPS_Image_File_Nname);
		EProcessFristBase = (PULONG64)((PUCHAR)(*(PULONG64)EPS_Flink) - ACTIVE_PROCESS_LINKS_OFFSET);
	} while ((PULONG64)(((PUCHAR)EProcessFristBase) + ACTIVE_PROCESS_LINKS_OFFSET) != FristFlag);
}

PULONG64 GetEProcess(const char* name) {
	PULONG64 PS_ACTIVE_PROCESS_HEAD = (PULONG64)((PUCHAR)GetNtoskrnlbase() + PS_ACTIVE_PROCESS_HEAD_OFFSET);
	Plist_entry list_entry = (Plist_entry)PS_ACTIVE_PROCESS_HEAD;
	PULONG64 FristFlag = list_entry->first_Entry;
	PULONG64 EProcessFristBase = (PULONG64)((PUCHAR)FristFlag - ACTIVE_PROCESS_LINKS_OFFSET);
	PULONG64 EPS_Flink;
	do {
		EPS_Flink = (PULONG64)((PUCHAR)EProcessFristBase + ACTIVE_PROCESS_LINKS_OFFSET);
		PULONG64 EPS_Image_File_Nname = (PULONG64)((PUCHAR)EProcessFristBase + IMAGE_FILE_NAME_OFFSET);
		if (MatchImageName((PUCHAR)EPS_Image_File_Nname, name)==true) return EProcessFristBase;
		DbgPrint("[GetEProcess]Process name:%s\n", EPS_Image_File_Nname);
		EProcessFristBase = (PULONG64)((PUCHAR)(*(PULONG64)EPS_Flink) - ACTIVE_PROCESS_LINKS_OFFSET);
	} while (*((PULONG64)EPS_Flink) != (ULONG64)FristFlag);
	return NULL;
}


/*
void PrintALLThread(PULONG64 EprocessBase) {
	Plist_entry THREAD_LIST_HEAD = (Plist_entry)((PUCHAR)EprocessBase + THREAD_LIST_HEAD_OFFSET);
	PUCHAR FristEThreadBase, Fristflag = (PUCHAR)THREAD_LIST_HEAD->first_Entry;
	int i=NULL;
	do {
		PULONG64 Threadcid = (PULONG64)(FristEThreadBase + ETHREAD_CID_OFFSET);
		DbgPrint("[GetEProcess]find a Thread [&d] cid:%llu\n", i, *Threadcid);
		FristEThreadBase = FristEThreadBase + ETHREAD_THREAD_LIST_ENTRY_OFFSET;
		i++;
	} while (FristEThreadBase!= Fristflag);
}
*/

void PrintALLThread(PUCHAR EprocessBase) {
	PLIST_ENTRY head = (PLIST_ENTRY)(EprocessBase + THREAD_LIST_HEAD_OFFSET);
	PLIST_ENTRY entry = head->Flink;  // Flink                                                                                                            

	int i = 0;
	while ((PUCHAR)entry != (PUCHAR)head) {
		PUCHAR thread = (PUCHAR)entry - ETHREAD_THREAD_LIST_ENTRY_OFFSET;

		PETHREAD ethread = (PETHREAD)thread;
		ULONG tid = (ULONG)(ULONG_PTR)PsGetThreadId(ethread);
		DbgPrint("[GetEProcess] Thread [%d] ETHREAD=%p TID=%lu\n", i, thread, tid);

		entry = entry->Flink;  // = entry->Flink                                                                                                          
		i++;
	}
}

PUCHAR GETaThread(PUCHAR EprocessBase) {
	PLIST_ENTRY head = (PLIST_ENTRY)(EprocessBase + THREAD_LIST_HEAD_OFFSET);
	PLIST_ENTRY entry = head->Flink;  // Flink                                                                                                            

	int i = 0;
	while ((PUCHAR)entry != (PUCHAR)head) {
		PUCHAR thread = (PUCHAR)entry - ETHREAD_THREAD_LIST_ENTRY_OFFSET;
		PETHREAD ethread = (PETHREAD)thread;
		ULONG tid = (ULONG)(ULONG_PTR)PsGetThreadId(ethread);
		if (tid) return (PUCHAR)ethread;
		entry = entry->Flink;  // = entry->Flink                                                                                                          
		i++;
	}
}


VOID MyKernelApcRoutine(
	PKAPC           Apc,
	PKNORMAL_ROUTINE *NormalRoutine,
	PVOID* NormalContext,
	PVOID* SystemArgument1,
	PVOID* SystemArgument2)
{
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess] CPU=%lu Thread=%p Arg1=%p Arg2=%p\n",
		KeGetCurrentProcessorNumber(),
		PsGetCurrentThread(),
		*SystemArgument1,
		*SystemArgument2);
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	DbgPrint("[GetEProcess]fuck Microsoft \n");
	ExFreePool(Apc);
}

VOID APCin(PUCHAR PEThraed) {
	/*
	KIRQL oldIrql;
	KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
	KeLowerIrql(oldIrql);
	*/
	PKAPC1 apc = (PKAPC1)ExAllocatePoolWithTag(NonPagedPool, 0x58, 'CPAK');
	RtlZeroMemory(apc, 0x58);

	apc->Type = 0x12;
	apc->Size = 0x58;
	apc->Thread = (PKTHREAD)PEThraed; 
	apc->KernelRoutine = MyKernelApcRoutine;
	apc->RundownRoutine = NULL;
	apc->NormalRoutine = NULL;
	apc->NormalContext = NULL;
	apc->SystemArgument1 = (PVOID)0xCAFE0001;
	apc->SystemArgument2 = (PVOID)0xBEEF0002;
	apc->ApcStateIndex = 0;
	apc->ApcMode = 0;                                                                                                                     
	apc->Inserted = 0;

	PKAPC_STATE PETHREAD_APCSTATE =(PKAPC_STATE)(PEThraed + ETHREAD_PKAPC_STATE);
	//0x029 KernelApcPending
	//PUCHAR PETHREAD_APCSTATE_KernelApcPending = PETHREAD_APCSTATE+0x029;
	PLIST_ENTRY PAPC_LIST = (PLIST_ENTRY)PETHREAD_APCSTATE;
	PLIST_ENTRY apcEntry = &apc->ApcListEntry;
	//提升irql
	KIRQL oldIrql;
	KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
	apcEntry->Flink = PAPC_LIST->Flink;
	apcEntry->Blink = PAPC_LIST;
	PAPC_LIST->Flink->Blink = apcEntry;
	PAPC_LIST->Flink = apcEntry;

	apc->Inserted = TRUE;
	PETHREAD_APCSTATE->KernelApcPending = TRUE;
	KeLowerIrql(oldIrql);

}

VOID UnLoad(PDRIVER_OBJECT DriverObj) {
	DbgPrint("[GetEProcess]UnLoad\n");
}



extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObj, PUNICODE_STRING Path) {
	DbgPrint("[GetEProcess]DriverEntry\n");
	DriverObj->DriverUnload = UnLoad;
	//Everything.exe
	PULONG64 EProcess = GetEProcess("notepad.exe");
	if (EProcess) {
		PrintALLThread((PUCHAR)EProcess);
		PUCHAR aTHREAD = GETaThread((PUCHAR)EProcess);
		DbgPrint("[GetEProcess]a THREAD:%p\n", aTHREAD);
		APCin(aTHREAD);
	}else {
		DbgPrint("find process error\n");
	}
	//ULONG64 PPS_ACTIVE_PROCESS_HEAD = GetNtoskrnlbase() + PS_ACTIVE_PROCESS_HEAD_OFFSET;
	//DbgPrint("[GetEProcess]PPS_ACTIVE_PROCESS_HEAD:%p\n", PPS_ACTIVE_PROCESS_HEAD);
	//GetNtoskrnlbase();
	//PrintALlProcess();
	return STATUS_SUCCESS;
}