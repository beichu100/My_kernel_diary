# **1_RUST_TCPCALL**

**简而言之就是**

**从用 `libc` crate 做 FFI 调 C 的 socket API，再进一步用内联汇编直接 syscall**

**手写RawTcpStream**

### 环境准备

```bash
rustup toolchain install nightly  # 内联汇编需要 nightly 或 stable + asm!
cargo new rawcomm --lib
```

### 先简单写一个rust的调用，作为对比:

(rust写的很烂见谅)

```
use libc::{socket,connect,close,sockaddr_in,AF_INET,SOCK_STREAM};

use std::net::{TcpStream,Ipv4Addr};

use std::{mem, string};

use std::env;

\#[repr(C, packed)] 

*struct* RawSockaddrIn {                                                                      

​      sin_family: u16,                                                                        

​      sin_port: u16,                                                                          

​      sin_addr: u32,                                                                          

​      sin_zero: [i8; 8],                                                                      

  }

fn test_libc_connect(ip:&str,port:u16){

​    unsafe{

​        *let* fd: i32=socket(AF_INET as i32, SOCK_STREAM, 0);

​        if fd < 0 {println!("socker error");return;}

​        *let* ipv4:Ipv4Addr=ip.parse().unwrap();

​        *let* ip_u32:u32=u32::from_be_bytes(ipv4.octets());

​        *let* addr:RawSockaddrIn=RawSockaddrIn{

​            sin_family: AF_INET as u16,

​            sin_port:port.to_be(),

​            sin_addr:ip_u32,

​            sin_zero:[0i8;8],

​        };

​        *let* ret=connect(

​            fd,

​            &addr as **const* _ as **const* libc::sockaddr,

​            mem::size_of::<RawSockaddrIn>() as u32,

​        );

​        if ret == 0 {

​            println!("链接{}:{}成功",ip,port);

​        }else{

​            *let* err = *libc::__errno_location();

​            println!("链接错误:{}",err);

​        }

​        close(fd);

​    }

}

fn main(){

​    *let* args:Vec<String> = env::args().collect();

​    test_libc_connect(&args[1], args[2].parse::<u16>().unwrap());;

}
```

那么接下来就看一下connect是怎么进入到内核的

用反编译软件打开libc.so.6 查找__connect函数，注意恢复符号。

我这里用的是Linux系统。如果你是windows根据不同语言差异可能是先调的Kernel.dll然后再调用的nt.dll。或者直接nt.dll。

### 下面是我ida反编译后标注的函数:

```
  unsigned __int64 __fastcall connect(int fd, struct sockaddr *addr, int addrlen)                                                                                       
  {                                                                                                                                                                     
    unsigned int oldtype; // r8d — 保存旧的 cancel type                                                                                                                 
                                                                                                                                                                        
    if (_libc_single_threaded)                                                                                                                                          
    {                                                                                                                                                                   
      result = sys_connect(fd, addr, addrlen);                                                                                                                          
      if (result > -4096ULL)            // 内核返回负 errno                                                                                                             
      {                                                                                                                                                                 
        __writefsdword(errno_tls_offset, -(int)result);  // errno = -result                                                                                             
        return -1;                                                                                                                                                      
      }           
    }                                                                                                                                                                   
    else                                                                                                                                                                
    {                                                                                                                                                                   
      oldtype = LIBC_CANCEL_ASYNC();    // 启用异步取消（因为 connect 可能阻塞）                                                                                        
      v4 = sys_connect(fd, addr, addrlen);                                                                                                                              
      if (v4 > -4096ULL)                                                                                                                                                
      {                                                                                                                                                                 
        __writefsdword(errno_tls_offset, -(int)v4);                                                                                                                     
        v4 = -1;                                                                                                                                                        
      }                                                                                                                                                                 
      LIBC_CANCEL_RESET(oldtype);       // 恢复取消类型                                                                                                                 
      return v4;                                                                                                                                                        
    }                                                                                                                                                                   
    return result;                                                                                                                                                      
  } 
```

同时来观察一下原始汇编:

```
.text:000000000010CA70                 cmp     cs:__libc_single_threaded, 0
.text:000000000010CA77                 jz      short loc_10CA90
.text:000000000010CA79                 mov     eax, 2Ah ; '*'
.text:000000000010CA7E                 syscall                 ; LINUX - sys_connect
.text:000000000010CA80                 cmp     rax, 0FFFFFFFFFFFFF000h
.text:000000000010CA86                 ja      short loc_10CAE0
.text:000000000010CA88                 retn
```

可以看到syscall指令本身没有任何操作数

eax/rax是系统调用号

其他寄存器是调用参数

接下来就是直接用内联汇编来调用connect

后面用c来写了rust写的我真绷不住了

```
#include <stdio.h>                                                                                     
#include <sys/socket.h>                                                                                
#include <netinet/in.h>                                                                                
#include <arpa/inet.h>                                                                                 
#include <unistd.h>                                                                                    
#include <errno.h>                                                                                     
#include <stdlib.h>                                                                                    
                        
static inline long syscall_connect(int fd, const struct sockaddr *addr, socklen_t len) {               
      long ret;                                                                                          
      __asm__ volatile (                                                                                 
          "syscall"                                                                                      
          : "=a" (ret)                                                                                   
          : "a" (42), "D" (fd), "S" (addr), "d" (len)                                                    
          : "rcx", "r11", "memory"                                                                       
      );                                                                                                 
      return ret;                                                                                        
  }


int main(int argc, char *argv[]) {                                                                     
    if (argc != 3) {                                                                                   
        fprintf(stderr, "用法: %s <ip> <port>\n", argv[0]);                                            
        return 1;                                                                                      
    }                                                                                                  
                                                                                                        
    int fd = socket(AF_INET, SOCK_STREAM, 0);                                                          
    if (fd < 0) {                                                                                      
        perror("socket error");                                                                        
        return 1;                                                                                      
    }                                                                                                  
                                                                                                        
    struct sockaddr_in addr;                                                                           
    addr.sin_family = AF_INET;                                                                         
    addr.sin_port = htons(atoi(argv[2]));                                                              
    addr.sin_addr.s_addr = inet_addr(argv[1]);                                                         
                                                                                                        
    //int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));   
    int ret = syscall_connect(fd, (struct sockaddr *)&addr, sizeof(addr));                                 
    if (ret == 0)                                                                                      
        printf("链接%s:%s成功\n", argv[1], argv[2]);                                                   
    else                                                                                               
        printf("链接错误:%d\n", errno);                                                                
                                                                                                        
    close(fd);                                                                                         
    return 0;                                                                                          
}   
```

gcc写法，如果用msvc的话不能那么写

并且msvc x64不支持内联汇编

