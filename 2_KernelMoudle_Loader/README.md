

# **2_KernelMoudle_Loader**

#### 前言

```
我一直在想应该如何写好内核的开始
写了一大堆的理论知识，后来还是删掉了
学习还是以实践见真章
从写项目来了解
```

#### 环境

```
调试端:
WDK+SDK配套
vs 2022
windbg
被调试端:
Windows 10虚拟机
开启调试模式和无签名检查
开启windbg双机调试
WindDbg Preview
```

#### 本期目标

```
编译加载一个kmdf驱动程序(加载驱动，观察从sc create到DriverEntry)
```

------

#### 1.1 编译一个最小wdk驱动确认环境可用

##### *只能说很多人卡编译这关过不去*

vs选择empty WDM Driver

进去先把.inf文件删了，他会导致很多你不想遇到的问题

接着进入项目属性-->inf2cat-->General-->Run inf2Cat 关掉它

然后来到资源文件里面写一个main.cpp

我的代码如下:

```
#include <ntddk.h>

extern "C" NTSTATUS
DriverEntry(
	PDRIVER_OBJECT  DriverObject,
	PUNICODE_STRING RegistryPath
) {
	return NULL;
};
```

如果你写的是.c的话应该不用导出函数

这样的话你编译应该还会爆错，因为并没有引用DriverEntry的参数

进设置把警告视为错误关闭就好了

这样的话，在环境没有问题的情况下应该可以编译你的第一个驱动程序

------

#### 1.2第一个kmdf驱动程序

##### *比wdk编译简单*

在vs里面找到空的kmdf驱动项目

写一个main.cpp

记得把警告视为错误关掉

我的代码如下:

```
#include <ntddk.h>
#include <wdf.h>

extern "C"
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	DbgPrint("Driver Loader\n");
	DbgPrint("Driver base=0x%p\n", DriverObject);
	DbgPrint("Driver IRQL=0x%d\n", KeGetCurrentIrql());
	return NULL;
}
```

非常的简单，就可以编译了

和上面的wdk程序差别在于在ntddk.h下引用了wdf.h头

不过还是要写的标准一点:

```
#include <ntddk.h>
#include <wdf.h>

VOID UnLoad(WDFDRIVER Driver) {
	DbgPrint("Driver Unload\n");
}

extern "C"
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	const wchar_t *DriverLoadPath = L"C:/testdriver/LoadDriver.sys";
	DbgPrint("Driver Loader\n");
	DbgPrint("Driver base=0x%p\n", DriverObject);
	DbgPrint("Driver IRQL=0x%d\n", KeGetCurrentIrql());
	WDF_DRIVER_CONFIG config;
	config.EvtDriverUnload = UnLoad;
	WDF_DRIVER_CONFIG_INIT(&config, NULL);
	NTSTATUS status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
	return status;
}
```

使用WdfDriverCreate初始化一下，我们没弄什么占用可以不写销毁函数

这是一些sc.exe的命令

```
sc.exe create mydriver type=kernel binPath=C:\testdriver\driver.sys
sc.exe start mydriver
sc.exe stop  mydriver
sc.exe delete mydriver
```

有签名的加载就是这样了

------

#### 1.3从sc create到DriverEntry

##### 忠告:内核的东西一定要动手看一遍，不要相信ai。

我的断点:

```

# 1. NtLoadDriver 
bp nt!NtLoadDriver ".printf \"[NtLoadDriver] DriverService=%mu\\n\", @rcx; g"
# 参数是驱动注册表路径: \Registry\Machine\System\CurrentControlSet\Services\hello_kmdf

# 2. SeValidateImageHeader — CI (Code Integrity) 签名验证
# 注意: 用 nt!SeValidateImageHeader 而非 CI!CiValidateImageHeader
# CI.dll 不一定已加载, CI! 符号会一直 deferred
bp nt!SeValidateImageHeader ".printf \"[2-SeValidateImageHeader]\\n\"; g"

# 3. (Mi* 系列) — 不推荐断点
# MiMapViewOfImageSection 触发非常频繁, 会卡的要死
# 建议: PE 加载细节用 IDA 静态分析 ntoskrnl.exe

# 4. IopLoadDriver
bp nt!IopLoadDriver ".printf \"[3-IopLoadDriver]\\n\"; g"

# 5. DriverEntry — 写的驱动代码
# 模块名取 .sys 文件名, 非 sc create 的服务名
bp LoadDriver!DriverEntry "k; g"
```

先贴我的windbg输出结果:

```
                                                                                     
  [2-SeValidateImageHeader]                                                                               
  [2-SeValidateImageHeader]                                                                               
  [1-NtLoadDriver]                                                                                       
  [3-IopLoadDriver]                                                                                       
  [2-SeValidateImageHeader]                                                                               
  [2-SeValidateImageHeader]                                                                               
  [2-SeValidateImageHeader]                                                                               
   # Child-SP          RetAddr               Call Site                                                   
  00 ffffd78f`314d88d8 fffff807`32b2130b     LoadDriver!DriverEntry                                       
  [C:\Users\beichu\Documents\beichu\code\LoadDriver\main.cpp @ 10]                                       
  01 ffffd78f`314d88e0 fffff807`32b21240     LoadDriver!FxDriverEntryWorker+0xbf                         
  [minkernel\wdf\framework\kmdf\src\dynamic\stub\stub.cpp @ 360]                                         
  02 ffffd78f`314d8920 fffff807`2d9cf1f6     LoadDriver!FxDriverEntry+0x20                              
  [minkernel\wdf\framework\kmdf\src\dynamic\stub\stub.cpp @ 249]                        
  03 ffffd78f`314d8950 fffff807`2d9cec2e     nt!IopLoadDriver+0x4c2                                       
  04 ffffd78f`314d8b30 fffff807`2d37d465     nt!IopLoadUnloadDriver+0x4e                                 
  05 ffffd78f`314d8b70 fffff807`2d3ea725     nt!ExpWorkerThread+0x105                                     
  06 ffffd78f`314d8c10 fffff807`2d48886a     nt!PspSystemThreadStartup+0x55                               
  07 ffffd78f`314d8c60 00000000`00000000     nt!KiStartSystemThread+0x2a                                 
  [4-DriverEntry]                                                                                         
  [2-SeValidateImageHeader]                                                                               
  [2-SeValidateImageHeader]
```

其实也没什么好说的，就是这么个加载过程。如果真想深入还得是逆向看ntoskrnl.exe

下面是一个导图(AI写的不一定对):

```
sc start 驱动名
  │
  ▼
services.exe
  │  (需要有 SeLoadDriverPrivilege)
  │  (SCM RPC → services.exe → NtLoadDriver)
  ▼
NtLoadDriver(RegistryPath)   ← ntdll.dll → syscall → 内核
  │
  ├── SeValidateImageHeader(.sys PE)    ← CI签名验证 (DSE在这里)
  │     └── 失败 → STATUS_INVALID_IMAGE_HASH
  │     └── TESTSIGNING ON → 跳过完整验证
  │
  ├── MiLoadImage(.sys)                 ← PE加载器 (映射段/解析导入/重定位)
  │
  └── 排队给系统工作线程                 ← NtLoadDriver 不直接调 IopLoadDriver
        │
        ▼
      ExpWorkerThread                    ← System 进程 (PID 4), 非 services.exe
        │
        └── IopLoadUnloadDriver          ← 驱动加载/卸载调度
              │
              └── IopLoadDriver          ← 创建 DriverObject
                    ├── DRIVER_OBJECT 初始化
                    ├── MajorFunction 填入默认分发函数
                    └── FxDriverEntry (WDF框架)
                          └── FxDriverEntryWorker
                                └── DriverEntry(DriverObject, RegistryPath) ← 驱动代码
                                      └── WdfDriverCreate
                                      └── DriverEntry 返回
```

最后重要的事情说三遍

## **自己试一遍，自己试一遍，自己试一遍。**
