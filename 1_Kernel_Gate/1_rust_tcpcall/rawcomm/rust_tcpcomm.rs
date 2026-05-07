use libc::{socket,connect,close,sockaddr_in,AF_INET,SOCK_STREAM};
use std::net::{TcpStream,Ipv4Addr};
use std::{mem, string};
use std::env;

#[repr(C, packed)] 
struct RawSockaddrIn {                                                                      
      sin_family: u16,                                                                        
      sin_port: u16,                                                                          
      sin_addr: u32,                                                                          
      sin_zero: [i8; 8],                                                                      
  }

fn test_libc_connect(ip:&str,port:u16){
    unsafe{
        let fd: i32=socket(AF_INET as i32, SOCK_STREAM, 0);
        if fd < 0 {println!("socker error");return;}
        let ipv4:Ipv4Addr=ip.parse().unwrap();
        let ip_u32:u32=u32::from_be_bytes(ipv4.octets());
        let addr:RawSockaddrIn=RawSockaddrIn{
            sin_family: AF_INET as u16,
            sin_port:port.to_be(),
            sin_addr:ip_u32,
            sin_zero:[0i8;8],
        };

        let ret=connect(
            fd,
            &addr as *const _ as *const libc::sockaddr,
            mem::size_of::<RawSockaddrIn>() as u32,
        );

        if ret == 0 {
            println!("链接{}:{}成功",ip,port);
        }else{
            let err = *libc::__errno_location();
            println!("链接错误:{}",err);
        }
        close(fd);
    }
}

fn main(){
    let args:Vec<String> = env::args().collect();
    test_libc_connect(&args[1], args[2].parse::<u16>().unwrap());;
}