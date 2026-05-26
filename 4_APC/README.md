# 4_APC

前言

```
这两天状态不好，写的代码有点天机工程，见谅
```

首先APC是什么，专业的说是异步过程调用

本质就是插队。

那都知道

进程是一个空间概念，线程才是真正运行指令的

所以如果要玩APC，我们要先找到一个进程，然后随便找条线程，然后插队。这个就是apc注入。

在用户态也就是r3里面，一个线程必须要自己sleepEX(0,True),反正就是要必须自己进入可预警状态下才会调用APC，否则就算休眠了也不会调用。

那内核APC就不会，至于为什么，后面再说。

那么我们首先要找一个进程对吧

怎么找？

一般来说两种办法，第一种通过pid，第二种通过名字。

这两个都有对应的内核函数，会返回一个PEPROCESS指针，可以自己查一下。

------

pid是通过查CID表，把pid作为数组索引查

名字就是遍历PsActiveProcessHead

------

不过我更好奇这个函数是去哪找EPROCESS的，逆向了一下。

是ntoskrnl里面全局LIST_ENTRY变量

那就像做外挂一样拿ntoskrnl的base然后+偏移就行了

不过要注意，这个偏移可能会跟着windows版本更新变，所以你真要放到生产代码里面最好用特征码来定位

不过我们学习就来偷个懒，直接windbg找

```
kd> ? nt!PspCidTable - nt
Evaluate expression: -4220160473 = ffffffff`04a90027
  符号距离 ntoskrnl 基址的偏移

kd> ? nt!PsActiveProcessHead - nt  
Evaluate expression: -4149317552 = ffffffff`08e000d0
```

那么还需要找一下ntoskrnl的base。

```
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
```

ZwQuerySystemInformation这个函数要自己导入一下。获取到的内核模块结构的第一个就是ntoskrnl。

结构体也要自己查或者逆，我放头文件里面了。

**wdk的不好用**

那么我们就可以得出名字找PEPROCESS的函数了

```
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
```

字符串对比的话其实自己随便写一个就好

```
BOOLEAN MatchImageName(PUCHAR name, const char* target) {
	for (int i = 0; i < 15; i++) {
		if (name[i] != target[i]) return FALSE;
		if (name[i] == '\0') return TRUE;                                                                                               
	}
	return target[14] == '\0';                                                                              
}
```

------

那APC既然是线程的东西，那我们还要找条线程来对吧

线程是EPROCESS结构里面有一个THREAD_LIST_HEAD

和EPROCESS一样是一个链表，一样的查找方法。

```
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
```

我们随便找一条就好了

```
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
```

那么我们看ETHREAD结构可以看到有一个ApcState结构体

里面存放的就是APC结构了吗？不对，里面还有一个LIST_ENTRY，里面存的是KAPC结构

所以这个APCstate其实和EPROCESS和THREAD一样。

梳理一下线路

```
PsActiveProcessHead → EPROCESS → ETHREAD → KTHREAD.ApcState → KAPC
```

在APCstate结构里面有一个字段KernelApcPending，把这个设置成true，也就是1。在这个线程进出内核的时候就会去检查APC队列，如果有就执行。

先来设计一个我们要插队的函数

```
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
	DbgPrint("[GetEProcess] CPU=%lu Thread=%p Arg1=%p Arg2=%p\n",
		KeGetCurrentProcessorNumber(),
		PsGetCurrentThread(),
		*SystemArgument1,
		*SystemArgument2);
	ExFreePool(Apc);
}
```

然后我们自己给APCstate字段填写，然后自己创建一个KAPC结构，然后插入链表，就ok了

```
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
```

这里必须提升IRQL，原本我看资料是说ETHREAD里面有一个锁字段的，可以用这个把线程上锁，不过我没找到就是了。

所以要用高IRQL保证原子性，不然可能会有线程安全问题。

apc->Inserted是KAPC结构里面说明自己这个APC可以运行了，如果不写TRUE的话就算外面KernelApcPending=TRUE也不会执行

------

结尾：

其实做的这些事情都有对应的函数，不过我感觉调函数几句话的事情太没意思了

 虽然说ntoskrnl被HOOK的概率很小，但不是0.也有可能被VT HOOK

自己实现就不会有这种问题。