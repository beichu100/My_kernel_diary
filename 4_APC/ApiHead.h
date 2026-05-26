#pragma once
#include <ntddk.h>

typedef NTSTATUS (*Ptr_ZwQuerySystemInformation)(
    IN ULONG SystemInformationClass,
    OUT PVOID SystemInformation,
    IN ULONG SystemInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );


typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
    PVOID Section;
    PVOID MappedBase;
    PVOID ImageBase;            //映射基地址
    ULONG ImageSize;            //映射大小
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT ModuleNameOffset;
    CHAR FullPathName[0x0100];  //模块路径名称
}RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;


typedef struct _RTL_PROCESS_MODULES
{
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[ANYSIZE_ARRAY];
}RTL_PROCESS_MODULE,*PRTL_PROCESS_MODULE;


typedef struct _list_entry {
    PULONG64 first_Entry;
    PULONG64 end_Entry;
}list_entry,*Plist_entry;

// Ring3 回调
typedef VOID(*PKNORMAL_ROUTINE)(
    PVOID NormalContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
    );

// Ring0 回调                                                                                                  
typedef VOID(*PKKERNEL_ROUTINE)(
    PKAPC            Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
    );

typedef struct _KAPC1 {
    UCHAR    Type;                 // +0x00  0x12 = ApcObject
    UCHAR    SpareByte0;           // +0x01                                                                                                                     
    UCHAR    Size;                 // +0x02  0x58                                                                                                               
    UCHAR    SpareByte1;           // +0x03                                                                                                                     
    ULONG    SpareLong0;           // +0x04                                                                                                                     
    PKTHREAD Thread;               // +0x08  目标线程                                                                                                           
    LIST_ENTRY ApcListEntry;       // +0x10  挂在 ApcListHead 上                                                                                                
    PVOID    KernelRoutine;        // +0x20  Ring0 回调                                                                                                         
    PVOID    RundownRoutine;       // +0x28  APC 取消/线程终止时调用                                                                                            
    PVOID    NormalRoutine;        // +0x30  Ring3 回调 (Kernel APC 填 NULL)                                                                                    
    PVOID    NormalContext;        // +0x38  传给 NormalRoutine 的参数                                                                                          
    PVOID    SystemArgument1;      // +0x40  随便填                                                                                                             
    PVOID    SystemArgument2;      // +0x48  随便填                                                                                                             
    UCHAR    ApcStateIndex;        // +0x50  0 = OriginalApcEnvironment                                                                                         
    UCHAR    ApcMode;              // +0x51  0 = KernelMode, 1 = UserMode                                                                                       
    UCHAR    Inserted;             // +0x52  调度器标记, 初始化 FALSE                                                                                           
} KAPC1,*PKAPC1;


// ==================== 从 dt nt!_KAPC_STATE 翻译 ====================                                                                                          

typedef struct _KAPC_STATE {
    LIST_ENTRY ApcListHead[2];     // +0x00  [0]=Kernel, [1]=User
    PKPROCESS Process;             // +0x20                                                                                                                     
    UCHAR      KernelApcInProgress;// +0x28  位域 bit 0                                                                                                         
    UCHAR      KernelApcPending;   // +0x29  ← 你设成 TRUE                                                                                                      
    UCHAR      UserApcPendingAll;  // +0x2a                                                                                                                     
} KAPC_STATE, * PKAPC_STATE;