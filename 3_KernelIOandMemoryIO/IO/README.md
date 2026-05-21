# **3_1KernelIO**

前言

```
因为保护模式的存在，所以r0和r3通讯变的有点繁琐
还没办法调用熟悉的windows api
废话少说直接开写
```

那其实吧通讯这个东西也不一定得用标准的这些办法

只要你想，能交互，就能通讯了。

那么先说标准的buffer通讯

METHOD_BUFFERED

这个算是标准办法里面最简单的，IRQL高于0也可以用

不过大数据传输(>1MB)以上吧有性能开销

由IO管理器管理，从非分页池创建buffer

从r3复制内存到buffer里面

我们写完再复制回r3

其他还有办法比如METHOD_NEITHER

不经过IO管理器，切换到目标地址空间复制

复杂又容易蓝屏

剩下的比如什么文件传输，注册表传输，管道传输，还有各种神人办法不再赘述

那我们就用标准又安全的METHOD_BUFFERED

------

首先使用CTL_CODE这个宏来定义我们的请求

```
#define IOCTL_TESTBUFF \
CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)
```

然后我们需要定义一个IRP_MJ_DEVICE_CONTROL函数来处理

其实很像r3的消息回调吧(

我就直接贴我写的了:

```
NTSTATUS DriverIOtest(PDEVICE_OBJECT DevObj, PIRP Irp) {
	NTSTATUS ret = STATUS_SUCCESS;
	ULONG_PTR info = 0;
	PIO_STACK_LOCATION irpSP=IoGetCurrentIrpStackLocation(Irp);
	ULONG controlCode = irpSP->Parameters.DeviceIoControl.IoControlCode;
	ULONG inputbufflen = irpSP->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outputBufflen = irpSP->Parameters.DeviceIoControl.OutputBufferLength;
	PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
	switch (controlCode) {
	case IOCTL_TESTBUFF: {
			if (inputbufflen < sizeof(ULONG) || outputBufflen <sizeof(ULONG)) {
				ret = STATUS_BUFFER_TOO_SMALL;
				break;
			}
		ULONG getdata = *(PULONG)buffer;
		ULONG retdata = getdata + 1;
		*(PULONG)buffer = retdata;
		info = sizeof(ULONG);
		ret = STATUS_SUCCESS;
		KdPrint(("IOCTL: in=%u,out=%u", getdata, retdata));
		break;
		}
	default: {
		ret= STATUS_INVALID_DEVICE_REQUEST;
		break;
		}
	}
	Irp->IoStatus.Status = ret;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return ret;
}
```

我也不知道该怎么很详细的解读这段代码，都是公式和基础的东西，唯一一点就是微软的命名真的很奇怪。容易搞混，只能多查查了

另外还需要定义MYIRP_MJ_CREATE和MYIRP_MJ_CLOSE

这里存在版本差异，有些windows就可以直接返回STATUS_SUCCESS然后IO管理器就帮你返回成功

但是我们还是用标准写法:

```
NTSTATUS MYIRP_MJ_CREATE(PDEVICE_OBJECT pDevObj, PIRP
	pIrp) {
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS MYIRP_MJ_CLOSE(PDEVICE_OBJECT pDevObj, PIRP
	pIrp) {
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
```

就是让io请求包返回成功就可以了。

然后就到了DriverEntry里面，我们要把定义的IRP_MJ函数指针给赋值

```
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIOtest;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = MYIRP_MJ_CREATE;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = MYIRP_MJ_CLOSE;
```

这样子我们的驱动就可以处理Io请求包了。

不过还有个问题，我们没有注册驱动对象和符号链接。

在r3没办法直接找到我们。

先声明全局变量:

```
UNICODE_STRING g_DevName;
UNICODE_STRING g_SymLink;
PDEVICE_OBJECT g_DevObj;
```

赋值:

```
	RtlInitUnicodeString(&g_DevName, L"\\Device\\KernelIO");
	RtlInitUnicodeString(&g_SymLink, L"\\DosDevices\\KernelIO");
```

创建:

```
	if(IoCreateDevice(DriverObject, NULL, &g_DevName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DevObj)!=STATUS_SUCCESS){
		KdPrint(("IoCreateDevice ERROR"));
	}
	if (IoCreateSymbolicLink(&g_SymLink, &g_DevName) != STATUS_SUCCESS) {
		KdPrint(("IoCreateSymbolicLink ERROR"));
	}
```

别忘了，在驱动卸载函数要销毁掉我们创建的驱动对象和符号链接，这些不销毁会一直存在到系统重启

```
VOID UnLoad(PDRIVER_OBJECT Driver) {
	KdPrint(("Driver Unload\n"));
	IoDeleteSymbolicLink(&g_SymLink);
	IoDeleteDevice(g_DevObj);
}
```

------

## r3IO

直接贴代码:

```
#include <iostream>
#include <windows.h>

#define IOCTL_TESTBUFF \
CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)

int main() {
	HANDLE hDevice = CreateFileW(
		L"\\\\.\\KernelIO",
		GENERIC_READ | GENERIC_WRITE,
		NULL,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (hDevice == (HANDLE)-1) {
		printf("CreateFileW error: %lu \n",GetLastError());
		return 1;
	}
	printf("Device opened successfully, handle=%p\n", hDevice);
	ULONG indata = 20;
	ULONG outdata = NULL;
	DWORD bytesReturned = NULL;
	BOOL ok = DeviceIoControl(
		hDevice,
		IOCTL_TESTBUFF,
		&indata,
		sizeof(indata),
		&outdata,
		sizeof(outdata),
		&bytesReturned,
		NULL
	);
	if (!ok) {
		printf("DeviceIoControl failed: %lu\n", GetLastError());
		CloseHandle(hDevice);
		return 1;
	}
	printf("IOCTL success: in=%lu → out=%lu, bytesReturned=%lu\n",indata, outdata, bytesReturned);
	CloseHandle(hDevice);
	system("pause");
	return 0;
}

```

这个真的没什么好讲的了。不行就去查查函数定义。