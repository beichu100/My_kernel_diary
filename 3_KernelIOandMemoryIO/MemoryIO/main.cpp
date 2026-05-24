#include <ntddk.h>
#include <intrin.h>

#define PFN_MASK  0x000FFFFFFFFFF000ULL
#define PAGE_4K   0xFFFULL                                                   
#define PAGE_2MB  0x1FFFFFULL                                                
#define PAGE_1GB  0x3FFFFFFFULL  

VOID unload(PDRIVER_OBJECT devobj) {
	KdPrint(("[patovaread]unload\n"));
}


NTSTATUS ReadPhysicalMemory(ULONG64 PhysAddr, PVOID Buffer, SIZE_T Size) {
	PHYSICAL_ADDRESS pa;
	pa.QuadPart = PhysAddr;
	PVOID mapped = MmMapIoSpace(pa, Size, MmNonCached);
	if (!mapped) {
		KdPrint(("[patovaread]ReadPhysicalMemory error pa:%llx size:%d\n", PhysAddr, Size));
		return STATUS_UNSUCCESSFUL;
	}
	RtlCopyMemory(Buffer, mapped, Size);
	MmUnmapIoSpace(mapped, Size);
	return STATUS_SUCCESS;
}

NTSTATUS copyPhysicalMemory(ULONG64 PhysAddr, PVOID Buffer, SIZE_T Size) {
	PHYSICAL_ADDRESS pa;
	pa.QuadPart = PhysAddr;
	MM_COPY_ADDRESS pa2;
	pa2.PhysicalAddress = pa;
	if (MmCopyMemory(Buffer, pa2, Size, MM_COPY_MEMORY_PHYSICAL, NULL) == STATUS_SUCCESS) {
		return STATUS_SUCCESS;
	}else {
		KdPrint(("[patovaread]copyPhysicalMemory error pa:%llx size:%d\n", PhysAddr, Size));
		return STATUS_UNSUCCESSFUL;
	}
}

VOID readmm(ULONG64 PhysAddr, PVOID Buffer, SIZE_T Size) {
	PHYSICAL_ADDRESS pa;
	pa.QuadPart = PhysAddr;
	ULONG64 va=(ULONG64)MmGetVirtualForPhysical(pa);
	for (int i=0;i < Size;i++) {
		*((CHAR*)Buffer + i) = *((CHAR*)va + i);
	}
}


ULONG64 vatopa(ULONG64 cr3,ULONG64 virtualaddr) {
	KdPrint(("[patovaread]vatopa cr3:%llx va:%p\n", cr3, virtualaddr));
	ULONG64 base= cr3 & ~(ULONG64)0xFFF;
	ULONG64 PML4index = (virtualaddr >> 39)&(ULONG64)0x1FF;
	KdPrint(("[patovaread]vatopa base:%llx PML4index:%llx\n", base, PML4index));
	ULONG64 PML4pte,offset,PDPTpte,PDTpte,pte;
	readmm(base + 8 * PML4index, &PML4pte, 8);
	if ((PML4pte & 0x1) == 1) {//缺页
		ULONG64 PDPTbase = PML4pte  & (ULONG64)0x000FFFFFFFFFF000;
		ULONG64 PDPTindex = (virtualaddr >> 30) & (ULONG64)0x1FF;
		KdPrint(("[patovaread]vatopa PDPTbase:%llx PDPTindex:%llx\n", PDPTbase, PDPTindex));
		readmm(PDPTbase + 8 * PDPTindex, &PDPTpte, 8);
		if ((PDPTpte & 0x1) == 1) {
			if (((PDPTpte >> 7) & 0x1) == 1) {//大页
				offset = (virtualaddr & (ULONG64)0x3FFFFFFF);
				ULONG64 base = PDPTpte & (ULONG64)0x000FFFFFFFFFF000;
				return base + offset;
			}
			else {
				ULONG64 PDTbase = PDPTpte  & (ULONG64)0x000FFFFFFFFFF000;
				ULONG64 PDTindex = (virtualaddr >> 21) & (ULONG64)0x1FF;
				KdPrint(("[patovaread]vatopa PDTbase:%llx PDTindex:%llx\n", PDTbase, PDTindex));
				readmm(PDTbase + 8 * PDTindex, &PDTpte, 8);
				if ((PDTpte & 0x1) == 1) {
					if (((PDTpte >> 7) & 0x1) == 1) {//大页
						offset = (virtualaddr & (ULONG64)0x1FFFFF);
						ULONG64 base = PDTpte  & (ULONG64)0x000FFFFFFFFFF000;
						return base + offset;
					}
					else {
						ULONG64 PTEbase = PDTpte  & (ULONG64)0x000FFFFFFFFFF000;
						ULONG64 PTEindex = (virtualaddr >> 12) & (ULONG64)0x1FF;
						KdPrint(("[patovaread]vatopa PTEbase:%llx PTEindex:%llx\n", PTEbase, PTEindex));
						readmm(PTEbase + 8 * PTEindex, &pte, 8);
						if (!(pte & 1)) return 0;
						offset = (virtualaddr & (ULONG64)0xFFF);
						ULONG64 base = pte  & (ULONG64)0x000FFFFFFFFFF000;
						return base + offset;
					}
				}else return 0;
			}
		}else return 0;
	}else return 0;
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	KdPrint(("[patovaread]DriverEntry\n"));
	DriverObject->DriverUnload = unload;
	ULONG64 cr3 = __readcr3();
	PHYSICAL_ADDRESS cr3p;
	cr3p.QuadPart = cr3;
	static const UCHAR magic[12] = "YuanShenNB";
	ULONG64 va = (ULONG64)magic;
	PHYSICAL_ADDRESS pa = MmGetPhysicalAddress((PVOID)magic);
	UCHAR buffer[12];
	KdPrint(("[patovaread]target va:%p\n",va));
	KdPrint(("[patovaread]target pa:%llx\n", pa.QuadPart));
	KdPrint(("[patovaread]vatopa cr3:%llx\n", cr3));
	//KdPrint(("[patovaread]vatopa pml4 va:%llx\n", MmGetVirtualForPhysical(cr3p)));
	ULONG64 myvatopaa = vatopa(cr3, va);
	KdPrint(("[patovaread]target myvatopa:%llx\n", myvatopaa));
	return STATUS_SUCCESS;
}