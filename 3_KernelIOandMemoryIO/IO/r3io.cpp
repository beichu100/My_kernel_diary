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
