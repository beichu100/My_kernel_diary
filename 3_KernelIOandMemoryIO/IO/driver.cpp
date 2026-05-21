#include <ntddk.h>

#define IOCTL_TESTBUFF \
CTL_CODE(FILE_DEVICE_UNKNOWN,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)

UNICODE_STRING g_DevName;
UNICODE_STRING g_SymLink;
PDEVICE_OBJECT g_DevObj;

VOID UnLoad(PDRIVER_OBJECT Driver) {
	KdPrint(("Driver Unload\n"));
	IoDeleteSymbolicLink(&g_SymLink);
	IoDeleteDevice(g_DevObj);
}

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

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	KdPrint(("Driver Loader\n"));
	KdPrint(("Driver base=0x%p\n", DriverObject));
	KdPrint(("Driver IRQL=0x%d\n", KeGetCurrentIrql()));
	RtlInitUnicodeString(&g_DevName, L"\\Device\\KernelIO");
	RtlInitUnicodeString(&g_SymLink, L"\\DosDevices\\KernelIO");


	DriverObject->DriverUnload = (PDRIVER_UNLOAD)UnLoad;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverIOtest;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = MYIRP_MJ_CREATE;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = MYIRP_MJ_CLOSE;

	if(IoCreateDevice(DriverObject, NULL, &g_DevName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DevObj)!=STATUS_SUCCESS){
		KdPrint(("IoCreateDevice ERROR"));
	}


	if (IoCreateSymbolicLink(&g_SymLink, &g_DevName) != STATUS_SUCCESS) {
		KdPrint(("IoCreateSymbolicLink ERROR"));
	}

	return STATUS_SUCCESS;
}