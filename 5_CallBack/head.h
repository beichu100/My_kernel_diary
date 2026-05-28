#pragma once
#include <ntddk.h>



typedef NTSTATUS(*Ptr_ZwQuerySystemInformation)(
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
}RTL_PROCESS_MODULE, * PRTL_PROCESS_MODULE;

typedef struct _EX_CALLBACK_ROUTINE_BLOCK_OLD {
    ULONG64 RundownProtect;    // +0x000                                                                                                                        
    PVOID   CallbackRoutine;   // +0x008 回调                                                                                                            
    PVOID   CallbackContext;   // +0x010                                                                                                                        
} EX_CALLBACK_ROUTINE_BLOCK,*PEX_CALLBACK_ROUTINE_BLOCK;

