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