# **3_2PhysicalMemoryReadWrite**

***目前最牢的一集***

------

首先要知道，在80386或者80286之后cpu特权指令无法直接读写物理地址

寻址公式是通过当前cr3寄存器(存的PML4物理地址base)找到页表，再解析虚拟地址当索引和偏移查找页表找到物理地址后由cpu读写

不过我们可以使用hal(硬件抽象层)提供的函数来帮助我们读写

比如:

```
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
```

这个函数把物理地址映射到我们可以操作的虚拟地址空间

我们可以通过下面的小程序来测试:

```
#include <ntddk.h>

VOID unload(PDRIVER_OBJECT devobj) {
	KdPrint(("[readwritephysicalmemory]unload\n"));
}

NTSTATUS ReadPhysicalMemory(ULONG64 PhysAddr,PVOID Buffer,SIZE_T Size) {
	PHYSICAL_ADDRESS pa;
	pa.QuadPart = PhysAddr;
	PVOID mapped = MmMapIoSpace(pa, Size, MmNonCached);
	if (!mapped) return STATUS_UNSUCCESSFUL;
	RtlCopyMemory(Buffer, mapped, Size);
	MmUnmapIoSpace(mapped, Size);
	return STATUS_SUCCESS;
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	KdPrint(("[readwritephysicalmemory]DriverEntry\n"));
	static const UCHAR magic[12] = "YuanShenNB";
	PHYSICAL_ADDRESS pa = MmGetPhysicalAddress((PVOID)magic);
	UCHAR buffer[12];
	if (ReadPhysicalMemory(pa.QuadPart, buffer, 12) != STATUS_SUCCESS) { 
		KdPrint(("[readwritephysicalmemory]ReadPhysicalMemory ERROR\n"));
	}else {
		KdPrint(("[readwritephysicalmemory]ReadPhysicalMemory success\n"));
		KdPrint(("[readwritephysicalmemory]target va:%p\n", magic));
		KdPrint(("[readwritephysicalmemory]target pa:%llx\n", pa.QuadPart));
		KdPrint(("[readwritephysicalmemory]target pa readbuffer:%s\n", buffer));
	}

	DriverObject->DriverUnload = unload;
	return STATUS_SUCCESS;
}
```

输出结果是:

```
[readwritephysicalmemory]DriverEntry
[readwritephysicalmemory]ReadPhysicalMemory success
[readwritephysicalmemory]target va:FFFFF80077E521C0
[readwritephysicalmemory]target pa:7cffb1c0
[readwritephysicalmemory]target pa readbuffer:YuanShenNB
```

下面是虚拟地址转物理地址到底应该怎么算:

```
Virtual Address (64-bit, 实际只用 48-bit):
  [63:48]  Sign Extension   (全 0 = 用户态, 全 1 = 内核态, 非法则 #GP)
  [47:39]  PML4 Index       (9 bits → 512 entries, 每个 entry 指向一个 PDPT)
  [38:30]  PDPT Index       (9 bits → 512 entries, 每个 entry 指向一个 PDT)
  [29:21]  PDT Index        (9 bits → 512 entries, 每个 entry 指向一个 PT)
  [20:12]  PT Index         (9 bits → 512 entries, 每个 entry 指向一个 4KB Page)
  [11: 0]  Page Offset      (12 bits → 0-4095)

每种页表大小: 512 entries × 8 bytes = 4KB (恰好一页)
一个 PML4 entry 覆盖: 512GB (2^39)
一个 PDPT entry 覆盖: 1GB   (2^30)
一个 PDT  entry 覆盖: 2MB   (2^21)
一个 PT   entry 覆盖: 4KB   (2^12)

64-bit PTE (PML4E / PDPTE / PDE / PTE 都类似):
  [63]     XD (Execute Disable / NX) — 1 = 不可执行
  [62:52]  PFN hi (物理页帧号高位)
  [51:12]  PFN lo (物理页帧号, 共 40 bits, 可寻址 2^52 物理内存)
  [11: 9]  Ignored / Available (软件可自由使用)
  [ 8]     G   (Global) — CR4.PGE=1 时跨进程保留
  [ 7]     PAT / PS (Page Attribute Table / Page Size in PDE)
  [ 6]     D   (Dirty) — 1 = 已写入
  [ 5]     A   (Accessed) — 1 = 已访问 (CPU 硬件自动设, OS 软件清)
  [ 4]     PCD (Page-level Cache Disable)
  [ 3]     PWT (Page-level Write-Through)
  [ 2]     U/S (User/Supervisor) — 0 = Ring0 only, 1 = Ring3 accessible
  [ 1]     R/W (Read/Write) — 0 = Read-only, 1 = Read-Write
  [ 0]     P   (Present) — 0 = 缺页 → #PF, 1 = 有效
```

用人话来说就是:

**假设有一个虚拟地址，把他按bit拆分拿PML4部分当索引，然后读取物理内存地址:cr3+sizeof(long)*索引，读一个long长度。然后也按照bte解析。先判断P位是不是为1，然后取12位到51位作为一个long的高位，剩下的补零。这个是PDPT的基址，然后再加PDPT的索引继续解析下去，然后判断PS位是不是大页，如果是就不继续解析。然后一直到PTE，取12位到51位作为一个long的高位，剩下补零，然后加上偏移。这个就是那个虚拟地址的物理地址了。如果在PDPT或者PDT遇到大页则后面的虚拟地址作为offset寻址。**

那么下面是我写的寻址算法:

```
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
```

这里要说一下，本来我想用c语言的位域结构体作为page pte的解析，但是我不确定编译的时候他到底用哪边来对齐，所以还是用位运算mask这种简单可靠的办法来写。

**然后我测试的时候就喜提BSOD了**

我们的映射物理地址函数不能用

那我就想换:

```
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
```

依旧喜提BSOD

最后只能得出结论被HAL拦截

我后面想了一下

既然我们可以把虚拟地址转换成物理地址。那能不能反过来把物理地址转换成虚拟地址来读写

那么有一个函数MmGetPhysicalAddress，我尝试用它取cr3的pml4虚拟地址，还真取到了。

那么我们写一个:

```
VOID readmm(ULONG64 PhysAddr, PVOID Buffer, SIZE_T Size) {
	PHYSICAL_ADDRESS pa;
	pa.QuadPart = PhysAddr;
	ULONG64 va=(ULONG64)MmGetVirtualForPhysical(pa);
	for (int i=0;i < Size;i++) {
		*((CHAR*)Buffer + i) = *((CHAR*)va + i);
	}
}
```

代替我们之前的读取物理内存函数

最后终于:

```
[patovaread]DriverEntry
[patovaread]target va:FFFFF8061A0421B0
[patovaread]target pa:7c80b1b0
[patovaread]vatopa cr3:1ad000
[patovaread]vatopa cr3:1ad000 va:FFFFF8061A0421B0
[patovaread]vatopa base:1ad000 PML4index:1f0
[patovaread]vatopa PDPTbase:c08000 PDPTindex:18
[patovaread]vatopa PDTbase:c09000 PDTindex:d0
[patovaread]vatopa PTEbase:1102f000 PTEindex:42
[patovaread]target myvatopa:7c80b1b0
```

------

不过在查资料的时候我了解到MmGetPhysicalAddress函数在取一些有保护的内存的时候可能会返回空

在修改一些受保护的内存的时候也可能BSOD。要通过mdl什么的。后面写到再来仔细研究

不管怎么说，只要可以读写任意物理内存，实际上就可以穿透保护模式了
