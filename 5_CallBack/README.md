# **5_CallBack**

前言

```
本期微软④马了
查资料也查不到
ai还给我往坑里面带
```

回调说白了就是官方标准的HOOK

在早期没有PG的时候都是直接HOOKssdt表，或者ntoskrnl代码

后面微软提供的更标准的方式

------

我们这期写个拦截笔记本进程创建的回调

下面来看看wdk提供的三种办法

```
  // WDK 公开声明:
  NTSTATUS PsSetCreateProcessNotifyRoutine(  cb, BOOLEAN Remove);                                           NTSTATUS PsSetCreateProcessNotifyRoutineEx(cb, BOOLEAN Remove);                                           NTSTATUS PsSetCreateProcessNotifyRoutineEx2(opts, cb, BOOLEAN Remove);      
```

PsSetCreateProcessNotifyRoutine是老版本的，他不能拦截进程创建，他的回调函数里面只能接受到有进程创建的信息。

**EX版本的可以，但是看WDK第二个参数都写的BOOLEAN，你以为是写FALSE或者TRUE吗？**

```
  // 内部:                                                                                                                                                    
  PsSetCreateProcessNotifyRoutine(cb, Remove)                                                                                                                     
     PspSetCreateProcessNotifyRoutine(cb, Remove ? 1 : 0)       // flag=0 注册 / 1 注销                                                                          
                                                                                                                                                                  
  PsSetCreateProcessNotifyRoutineEx(cb, Remove)                                                                                                                   
     PspSetCreateProcessNotifyRoutine(cb, (Remove != 0) + 2)     // flag=2 注册 / 3 注销                                                                         
                                                                                                                                                                  
  PsSetCreateProcessNotifyRoutineEx2(opts, cb, Remove)                                                                                                            
     if (opts) return STATUS_NOT_SUPPORTED;                                                                                                                      
     PspSetCreateProcessNotifyRoutine(cb, (Remove != 0) + 6)     // flag=6 注册 / 7 注销     
```

后面的东西我没查到公开资料，都是我自己逆出来的，不一定准确。

```
  PspSetCreateProcessNotifyRoutine(callback, flag):
      esi = flag                                                                                                                                                  
      ebx = flag                                                                                                                                                  
                                                                                                                                                                  
      // 区分注册/注销                                                                                                                                        
      test dl, 1                                                                                                                                                  
      jnz UNREGISTER            // flag bit0=1  注销路径                                                                                                         
                                                                                                                                                                  
      // 注册路径                                                                                                                                             
      and esi, 2                // Ex/Ex2 才有                                                                                                       
      if (esi != 0)             // Ex/Ex2: 多一步驱动签名验证                                                                                                     
          edx = 0x20                                                                                                                                              
          call MmVerifyCallbackFunctionCheckFlags(callback, 0x20)                                                                                                 
          test eax, eax                                                                                                                                           
          je return_C0000022    // 测试驱动到这就死                                                                                                             
                                                                                                                                                                  
      // [3] 分配 EX_CALLBACK_ROUTINE_BLOCK                                                                                                                       
      call ExAllocateCallBack(callback, ebx)                                                                                                                      
          // flag=0 → 0x18 字节, +0x10 = 0                                                                                                                        
          // flag=2 → 0x30 字节, +0x10 = 2 (bit1=Ex标记)                                                                                                          
          // flag=6 → 更大, +0x10 = 6 (bit1+bit2=Ex2标记)                                                                                                         
                                                                                                                                                                  
      if (block == NULL) return 0xC000009A                                                                                                                        
                                                                                                                                                                  
      // [4] 遍历 64 槽找空位                                                                                                                                     
      for (i = 0; i < 64; i++)
          if (CAS(&array[i], 0, block))                                                                                                                           
              goto INSERTED                                                                                                                                       
                                                                                                                                                                  
      ExFreePool(block)                                                                                                                                           
      return 0xC000000D
                                                                                                                                                                  
  INSERTED:                                                                                                                                                       
      if (esi != 0) lock inc [PspCreateProcessNotifyRoutineExCount]                                                                                               
      else          lock inc [PspCreateProcessNotifyRoutineCount]                                                                                                 
      return 0                                                                                                                                                    
                                                                                                                                                                  
  UNREGISTER:                                                                                                                                                     
      for (i = 0; i < 64; i++)                                                                                                                                    
          block = ExReferenceCallBackBlock(&array[i])                                                                                                             
          if (block && block->Function == callback && block->Flags == ebx)                                                                                        
              CAS zero into array slot                                                                                                                            
              Dereference + Rundown + Free                                                                                                                        
                                                                                                                                                                  
      return 0xC000007A           // 没找到 
```

下面是实际进程创建的调用

```
  PspCallProcessNotifyRoutines(ParentId, ProcessId, CreateFlag):
      for (i = 0; i < 64; i++)                                                                                                                                    
          block = ExReferenceCallBackBlock(&PspCreateProcessNotifyRoutine[i])                                                                                     
          if (!block) continue                                                                                                                                    
                                                                                                                                                                  
          flags = *(DWORD*)(block + 0x10)  // 版本标记                                                                                                  
                                                                                                                                                                  
          if (flags & 2)                   // bit1=Ex版本                                                                                                         
              block->CallbackRoutine(Process, ProcessId, &CreateInfo)
                  // 第三个参数 = PS_CREATE_NOTIFY_INFO* (含 ImageFileName, CommandLine)                                                                          
          else                             // bit1=0=老版本                                                                                                       
              block->CallbackRoutine(ParentId, ProcessId, CreateFlag)                                                                                             
                  // 第三个参数 = BOOLEAN (TRUE=创建,FALSE=退出)                                                                                                  
                                                                                                                                                                  
          ExDereferenceCallBackBlock(...)         
```

**直接说结论。Ex/Ex2 版本对驱动签名有隐性要求**

我测试签名一直给我爆0x22,

```
MmVerifyCallbackFunctionCheckFlags(cb, 0x20) 检查 _KLDR_DATA_TABLE_ENTRY.Flags (offset +0x68) 是否包含 0x20 位。                                                
sc create/start 加载的测试驱动没有该位
老版本flag=0不走此检查      
```

**然后一开始我本来ida看着就有点不对劲然后看WDK参数是一个BOOL值，就给我整的有点蒙，再加上ai给我带坑里面了。**

**让我从MmVerifyCallbackFunctionCheckFlags跟成牢大了**

------

反正吧我们自己插一个进去就好了

依旧是windbg拿偏移

```
x nt!PspCreateProcessNotifyRoutine
```

这个是一个ULONG64[64]数组，里面的entry低4位作为引用计数，剩下的作为指向EX_FAST_REF结构的指针

EX_FAST_REF结构这个网上我没找到准确资料，不过老版本的是确定的。EX版本是我自己的逆出来的

```
EX_CALLBACK_ROUTINE_BLOCK 真实布局 (Ex 版本, 0x30 字节):
  +0x000 RundownProtect  : EX_RUNDOWN_REF   (RO=0)
  +0x008 CallbackRoutine : PVOID           cb
  +0x010 CallbackContext : PVOID           //ex版本标记
  +0x018                   PVOID            扩展句柄?, NULL 保险
  +0x020 VersionTag      : ULONG64          [23:16]=版本号, [31:24]=Cookie Tag
```

那有了这些我们就能写出一个拦截笔记本程序创建的驱动了

先来个打印函数测试

```
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
```

没问题之后

我们来手动实现PspCreateProcessNotifyRoutine的两个功能

```
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
```

再写一个回调函数拦截

```

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

```

------

WINDBG拦截成功

```
[callback]DriverEntry
[GetEProcess]ZwQuerySystemInformation Address:FFFFF8071A678EB0
[GetEProcess]ZwQuerySystemInformation ByteSize:53880
[GetEProcess]Module Address:FFFFD80129BBD000
[GetEProcess]ntoskrnl base:18446735308122468352
find i:[0],Entry:18446700098244379839,EntryCont:15,callback:FFFFF8071A5E9130
find i:[1],Entry:18446700098245095967,EntryCont:15,callback:FFFFF8071B4F7220
find i:[2],Entry:18446700098255656415,EntryCont:15,callback:FFFFF8071B10B420
find i:[3],Entry:18446700098255658095,EntryCont:15,callback:FFFFF8071C3030C0
find i:[4],Entry:18446700098257018559,EntryCont:15,callback:FFFFF8071C82D930
find i:[5],Entry:18446700098257019231,EntryCont:15,callback:FFFFF8071B485110
find i:[6],Entry:18446700098257020287,EntryCont:15,callback:FFFFF8071D2B8C60
find i:[7],Entry:18446700098251709295,EntryCont:15,callback:FFFFF8071CE11C20
find i:[8],Entry:18446700098251724991,EntryCont:15,callback:FFFFF8071F123CF0
[GetEProcess]ZwQuerySystemInformation Address:FFFFF8071A678EB0
[GetEProcess]ZwQuerySystemInformation ByteSize:53880
[GetEProcess]Module Address:FFFFD80129BBD000
[GetEProcess]ntoskrnl base:18446735308122468352
[callback]ADDProcessCallBack i[9]
[GetEProcess]ZwQuerySystemInformation Address:FFFFF8071A678EB0
[GetEProcess]ZwQuerySystemInformation ByteSize:53880
[GetEProcess]Module Address:FFFFD80129BBD000
[GetEProcess]ntoskrnl base:18446735308122468352
find i:[0],Entry:18446700098244379839,EntryCont:15,callback:FFFFF8071A5E9130
find i:[1],Entry:18446700098245095967,EntryCont:15,callback:FFFFF8071B4F7220
find i:[2],Entry:18446700098255656415,EntryCont:15,callback:FFFFF8071B10B420
find i:[3],Entry:18446700098255658095,EntryCont:15,callback:FFFFF8071C3030C0
find i:[4],Entry:18446700098257018559,EntryCont:15,callback:FFFFF8071C82D930
find i:[5],Entry:18446700098257019231,EntryCont:15,callback:FFFFF8071B485110
find i:[6],Entry:18446700098257020287,EntryCont:15,callback:FFFFF8071D2B8C60
find i:[7],Entry:18446700098251709295,EntryCont:15,callback:FFFFF8071CE11C20
find i:[8],Entry:18446700098251724991,EntryCont:15,callback:FFFFF8071F123CF0
find i:[9],Entry:18446700098239578448,EntryCont:0,callback:FFFFF8071EE01470
[callback-] PID=2368 EXITED
[callback] BLOCKED notepad!
```

------

**不过我还是说明一下，真写生产代码还是用微软的。**

**不过自己玩随便了**