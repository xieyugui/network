#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

char *server = "127.0.0.1";  /* loopback */
int port = 6180;             /* no significance */

int main(int argc, char *argv[]) {

  char *buf = "hello, world!";
  int buflen = strlen(buf), rc;

  /**********************************************************
   * create an IPv4/TCP socket, not yet bound to any address
   * 协议族（family）AF_INET、AF_INET6、AF_LOCAL（或称AF_UNIX，Unix域socket）、AF_ROUTE
   * socket类型 SOCK_STREAM、SOCK_DGRAM、SOCK_RAW、SOCK_PACKET、SOCK_SEQPACKET
   * 指定协议 常用的协议有，IPPROTO_TCP、IPPTOTO_UDP、IPPROTO_SCTP、IPPROTO_TIPC
   *         0时，会自动选择type类型对应的默认协议
   *********************************************************/
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    printf("socket: %s\n", strerror(errno));
    exit(-1);
  }

  /**********************************************************
   * internet socket address structure, for the remote side
   * 指向要绑定给sockfd的协议地址
   *********************************************************/
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(server);
  sin.sin_port = htons(port);

  if (sin.sin_addr.s_addr == INADDR_NONE) {
    printf("invalid remote IP %s\n", server);
    exit(-1);
  }

  /**********************************************************
   * Perform the 3 way handshake, (c)syn, (s)ack/syn, c(ack)
   * socket描述字，通过socket()函数创建
   * 指向要绑定给sockfd的协议地址
   * 对应的是地址的长度
   *********************************************************/
  if (connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
    printf("connect: %s\n", strerror(errno));
    exit(-1);
  }

  if ( (rc=write(fd,buf,buflen)) != buflen) {
    printf("write: %s\n", (rc<0)?strerror(errno):"incomplete");
    exit(-1);
  }

  close(fd);
  exit(0);
}
