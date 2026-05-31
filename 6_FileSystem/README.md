# **6_FileSystem**

前言

```
windows的文件系统我不好评价，有点恶心了
文件系统是一个非常庞大且复杂的系统，没办法在本章全部概括到
本章若有错误或者对我后面没研究明白的地方有不一样见解的师傅欢迎找我交流
```

那虽然电脑的启动和cpuinit和本章的内容没有关系，但是我觉得还是有必要简单讲一下

先从最经典的BIOS+MBR启动开始

```
在cpu上电之后，先运行写在主板固件的BIOS程序，初始化硬件。靠in,out指令读取0号扇区bootloader程序
随后进入到实模式，为了加载内核和更大寻址，初始化gdt表等等，随后jmp far切换进入保护模式
随后为了进入64位的长模式，建立四级页表PML4-PDTP-PD-PT,开启分页等等。jmp far正式进入长模式
```

在UEFI情况下，不再经历实模式。通常就在保护模式或者长模式下启动

------

所以本来这章我本来是想写bootkit的，但是有secure boot。(其实可以bois上侧载一个UEFI?)

总之就是这块没研究明白，太多了。那我们来说回正事。

就像上一篇CallBack一样，微软给Windows的文件系统提供的官方回调叫Minifilter

负责管理的device objct叫fltmgr

我只能说这windows的文件系统历史遗留问题很繁琐，非常绕。

先来看看调用栈吧

```
用户态: CreateFile("C:\test.txt")
  ↓
I/O Manager → 构造 IRP_MJ_CREATE
  ↓
Filter Manager (FltMgr.sys)
  ├─ FltpPerformPreCallbacks: 按 Altitude 遍历 Minifilter 链
  │    ├─ Altitude 40000  PreOp
  │    ├─ Altitude 180000 PreOp
  │    ├─ Altitude 328000 PreOp
  │    └─ Altitude 369999 PreOp
  ├─ IoCallDriver → NTFS.sys → Disk.sys
  └─ FltpPerformPostCallbacks: 反序遍历
       ├─ Altitude 369999 PostOp
       ├─ Altitude 328000 PostOp
       └─ ...
```

**NTFS是文件系统的真正实现，disk.sys是直接和物理存储交互。**

Altitude高度在minifilter里很重要的概念，这个高度吧正常来说是要找微软申请。但是实际不申请也能用。

这个高度越低，在preop(请求)里面就最先被调用，在postop(返回)最后被调用。

那其实仔细看看调用栈就可以得出攻防路线

我举个例子，据我了解大多数edr，xdr都用的minifilter。

```
1.我们可以玩叠叠乐，在windows DEVICE OBJCT我们可以堆叠io，用IoAttachDeviceToDeviceStack函数可以叠target Device上面
这样子我们叠fltmgr上面，这样子直接致盲下面全部minifilter。
2.我们可以hook fltmgr的dispatch函数，但是这个貌似被patchguard保护，这个我没研究明白，据公开资料显示pg应该只保护文件系统的ntfs这一环。
3.我们通过特权指令直接操控in/out读写扇区，这样子那确实很无解了。但是你要自己实现一个ntfs系统，这个就需要自己权衡了。
4.上面我提到了patchguard不保护disk.sys，我们在这里hook可以致盲postop这一环的比如读取文件，但是这和3一样，你要去自己实现ntfs系统的功能。
(没列完，反正文件系统攻防点很多)
说完了攻击方那自然要说一下怎么防御

那我们作为edr,xdr视角。我们肯定是第一时间加载的。我们对用户态的ntdll函数hook,比如readfile是吧
我们也可以hook disk.sys，做交叉验证。你调用了readfile返回NULL，但是我自己查扇区却是有数据，那直接遥测报警了。

那还有攻击方的1，他能叠我也能叠，可以定时检测这个叠叠乐的调用栈来判断。
```

我写了那么多，其实你就能发现文件系统的攻防其实是个成本问题。就看谁愿意投入更多成本了。

------

那先来看看miniflter的实现吧

```
#include <fltKernel.h>
#include "head.h"

PFLT_FILTER g_Filter;

FLT_PREOP_CALLBACK_STATUS PreCreate(
    PFLT_CALLBACK_DATA    Data,
    PCFLT_RELATED_OBJECTS FltObjects,
    PVOID* CompletionContext)
{
    PFLT_FILE_NAME_INFORMATION Fileinfo;
    NTSTATUS ret = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED,&Fileinfo);
    DbgPrint("[file]file name:%wZ",&Fileinfo->Name);
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

CONST FLT_OPERATION_REGISTRATION g_Ops[] = {
      { IRP_MJ_CREATE, 0, PreCreate, NULL, NULL },
      { IRP_MJ_OPERATION_END }
};


NTSTATUS FilterUnload(FLT_FILTER_UNLOAD_FLAGS Flags)
{
    FltUnregisterFilter(g_Filter);
    return STATUS_SUCCESS;
}

NTSTATUS InstanceSetup(PCFLT_RELATED_OBJECTS FltObjects,
    FLT_INSTANCE_SETUP_FLAGS Flags,
    DEVICE_TYPE VolumeDeviceType,
    FLT_FILESYSTEM_TYPE FsType)
{
    return STATUS_SUCCESS;  // 所有卷都挂                                                                                                                       
}



CONST FLT_REGISTRATION g_Reg = {
    sizeof(FLT_REGISTRATION),      // Size
    FLT_REGISTRATION_VERSION,      // Version
    0,                             // Flags (0 或 FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP=1)
    NULL,                          // ContextRegistration (上下文, 简单场景 NULL)
    g_Ops,                         // OperationRegistration
    FilterUnload,                  // 卸载回调
    InstanceSetup,                 // 挂卷回调 (attach 到一个新卷时调用)
    NULL,                          // InstanceQueryTeardown
    NULL, NULL, NULL, NULL, NULL   // 不用的字段
};

NTSTATUS RegMiniFilter(PDRIVER_OBJECT DriverObj) {
    NTSTATUS s = FltRegisterFilter(DriverObj, &g_Reg, &g_Filter);
    if (!NT_SUCCESS(s)) return s;
    s = FltStartFiltering(g_Filter);
    return s;
}

VOID UnLoad(PDRIVER_OBJECT DriverObj) {
	DbgPrint("[file]UnLoad\n");
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObj, PUNICODE_STRING Path) {
	DbgPrint("[file]DriverEntry\n");

    NTSTATUS ret = RegMiniFilter(DriverObj);
    if (NT_SUCCESS(ret)) {
        DbgPrint("[file]Regminifilter success\n");
    }else DbgPrint("[file]Regminifilter error:%d\n",ret);
	return STATUS_SUCCESS;
}
```

其实就是填表嘛，你还要写一个inf文件，或者自己手动填写注册表来注册，**不然会BSOD**。

这个我不仔细赘述了，反正真用的时候大伙都要查资料。

我来讲一下让我比较痛苦的叠叠乐

```
#include <ntddk.h>

PDEVICE_OBJECT g_MyDevice;
PDEVICE_OBJECT g_LowerDevice;  

NTSTATUS MyDirCtrlDispatch(PDEVICE_OBJECT DevObj, PIRP Irp) {
    PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation(Irp);                                                                                                                                        
    PFILE_OBJECT fileObj = irpsp->FileObject;
    if (fileObj && fileObj->FileName.Length > 0) {
        DbgPrint("[attachfltmgr] filename: %wZ\n", &fileObj->FileName);
    }
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(g_LowerDevice, Irp);
}


NTSTATUS PassThrough(PDEVICE_OBJECT DevObj, PIRP Irp) {
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(g_LowerDevice, Irp);
}


VOID UnLoad(PDRIVER_OBJECT DriverObj) {
    if (g_MyDevice) {
        IoDetachDevice(g_MyDevice);                                                                                                         
        IoDeleteDevice(g_MyDevice);
    }
    DbgPrint("[attachfltmgr]UnLoad\n");
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObj, PUNICODE_STRING Path) {
    DbgPrint("[attachfltmgr]DriverEntry\n");
    DriverObj->DriverUnload = UnLoad;

    UNICODE_STRING targetName;
    RtlInitUnicodeString(&targetName, L"\\Device\\HarddiskVolume3");
    PDEVICE_OBJECT targetDev;
    PFILE_OBJECT targetFile;
    NTSTATUS ret = IoGetDeviceObjectPointer(&targetName, FILE_READ_DATA, &targetFile, &targetDev);
    if (!NT_SUCCESS(ret)) {
        return ret;
    }
                                                                                                                      
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObj->MajorFunction[i] = PassThrough;
    }
    DriverObj->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = MyDirCtrlDispatch;
                                                                                                                                        
    ret = IoCreateDevice(DriverObj, 0, NULL,
        targetDev->DeviceType,
        0, FALSE, &g_MyDevice);
    if (!NT_SUCCESS(ret)) {
        ObDereferenceObject(targetFile);
        return ret;
    }
                                                                                                                                        
    if (targetDev->Flags & DO_BUFFERED_IO)
        g_MyDevice->Flags |= DO_BUFFERED_IO;
    if (targetDev->Flags & DO_DIRECT_IO)
        g_MyDevice->Flags |= DO_DIRECT_IO;
    if (targetDev->Characteristics & FILE_DEVICE_SECURE_OPEN)
        g_MyDevice->Characteristics |= FILE_DEVICE_SECURE_OPEN;
    g_MyDevice->Flags |= DO_POWER_PAGABLE;
                                                                                                                                             
    g_LowerDevice = IoAttachDeviceToDeviceStack(g_MyDevice, targetDev);
    g_MyDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    ObDereferenceObject(targetFile);

    DbgPrint("[attachfltmgr]: mydevice=%p, lowdevice=%p\n", g_MyDevice, g_LowerDevice);
    return STATUS_SUCCESS;
}
```

实现思路是，我们先拿原本挂fltmgr的DEVICEOBJCT信息，然后我们再自己attach叠到他上面。

接着把我们不关系的IRP直接往下丢给他，我们只拦截我们关心的。

我觉得我实现思路是没问题的，代码我也反复改了好久，**但是就是叠他上面会BSOD**

**：（**

我是暂时没招了，原因我跟着BSOD调用栈dbg也没找出来。

结论是可能有patchguard保护或者fltmgr系统太脆弱了，实现基于是自己为调用栈顶写的。

------

结尾

还是希望有看到这的师傅对attach到fltmgr device objct会bsod知道原因的告诉我一下

我现在对文件系统一口也吃不下了

目前来说还是minifliter攻防最安全方便，毕竟改个注册表的事情。

