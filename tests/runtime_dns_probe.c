/* Manual on-device transport test. Requires root, signed executable, installed
 * HotspotVPNDNS and a temporary bridge101 with IPv4 172.20.11.1.
 * Pass the expected DNS for the current preferences. Uses loopback UDP 67/68;
 * it does not send DHCP to hotspot clients or test bootpd's BPF transport.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <roothide.h>
extern void setprogname(const char *);
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 int tx=socket(AF_INET,SOCK_DGRAM,0),rx=socket(AF_INET,SOCK_DGRAM,0);
 struct sockaddr_in addr={.sin_len=sizeof(addr),.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK),.sin_port=htons(67)};
 if(bind(tx,(struct sockaddr*)&addr,sizeof(addr))){perror("bind67");return 3;}
 addr.sin_port=htons(68);
 if(bind(rx,(struct sockaddr*)&addr,sizeof(addr))){perror("bind68");return 4;}
 struct timeval tv={.tv_sec=2};setsockopt(rx,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
 setprogname("bootpd");
 if(!dlopen(jbroot("/Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.dylib"),RTLD_NOW)){puts(dlerror());return 5;}
 uint8_t packet[300]={0},reply[400];packet[0]=2;packet[1]=1;packet[2]=6;
 const uint8_t options[]={99,130,83,99,53,1,2,54,4,172,20,11,1,6,4,172,20,11,1,255};
 memcpy(packet+236,options,sizeof(options));
 uint8_t expected[4];if(inet_pton(AF_INET,argv[1],expected)!=1)return 6;
 for(int test=0;test<2;test++) {
  ssize_t result;
  if(!test)result=sendto(tx,packet,sizeof(packet),0,(struct sockaddr*)&addr,sizeof(addr));
  else { struct iovec v[2]={{packet,247},{packet+247,sizeof(packet)-247}};struct msghdr msg={.msg_name=&addr,.msg_namelen=sizeof(addr),.msg_iov=v,.msg_iovlen=2};result=sendmsg(tx,&msg,0); }
  if(result!=sizeof(packet)){perror("send");return 7;}
  ssize_t got=recv(rx,reply,sizeof(reply),0);if(got!=sizeof(packet)){perror("recv");return 8;}
  if(memcmp(reply+251,expected,4)){printf("wrong DNS=%u.%u.%u.%u\n",reply[251],reply[252],reply[253],reply[254]);return 9;}
  printf("%s DNS=%u.%u.%u.%u OK\n",test?"sendmsg":"sendto",reply[251],reply[252],reply[253],reply[254]);
 }
 close(rx);close(tx);return 0;
}
