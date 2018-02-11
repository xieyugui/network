#define _GNU_SOURCE // F_SETSIG
#include <sys/types.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "utarray.h"

/*****************************************************************************
 * This is an example of a basic TCP server for concurrent clients. It uses the
 * SIGIO signal to get notified that there is network activity to be handled.
 * That includes new clients to accept, or input to read, or a client closure.
 * It cleanly handles signals like Ctrl-C, and does periodic other work.  It is
 * oriented toward servers that receive input only. We could write back to the
 * clients of course, but it should be noted that the I/O is set non-blocking.
 *
 * This particular program does not get notified about "which" file descriptors
 * are ready. It simply uses SIGIO to tell it, "check everything that might
 * need to be handled." It uses a function, service_network(), to check on all
 * those things that might need to handled. Since service_network() only uses
 * non-blocking calls, it's OK to call it any time- when input is ready on any
 * descriptor- or even when it's not.  Of course it would be more theoretically
 * efficient to service only the exact descriptors that are ready, e.g. using
 * the info.si_fd field, but this simpler program is adequate for TCP servers
 * with smaller number of clients and lower rates of input.
 *
 * Troy D. Hanson
 ****************************************************************************/

struct {
  in_addr_t addr;    /* local IP or INADDR_ANY   */
  int port;          /* local port to listen on  */
  int fd;            /* listener descriptor      */
  UT_array *fds;     /* array of client descriptors */
  int verbose;
  int ticks;         /* uptime in seconds        */
  int pid;           /* our own pid              */
  char *prog;
} cfg = {
  .addr = INADDR_ANY, /* by default, listen on all local IP's   */
};

void usage() {
  fprintf(stderr,"usage: %s [-v] [-a <ip>] -p <port\n", cfg.prog);
  exit(-1);
}

/* do periodic work here */
void periodic() {
  if (cfg.verbose) fprintf(stderr,"up %d seconds\n", cfg.ticks);
}

/* signals that we'll accept synchronously in sigwaitinfo */
/**
 * SIGIO  描述符上可以进行IO操作
 * SIGHUP  终端挂断或者进程死亡
 * SIGTERM 杀或的killall命令发送到进程默认的信号，该信号可以被处理和堵塞，要求程序自己正常退出
 * SIGINT 来自键盘的中断信号 ( ctrl + c )
 * SIGQUIT 和SIGINT类似, 但由QUIT字符(通常是Ctrl-\)来控制. 进程在因收到SIGQUIT退出时会产生core文件
 * SIGALRM  时钟定时信号 在Linux系统下，每一个进程都有惟一的一个定时器，该定时器提供了以秒为单位的定时功能。
 *          在定时器设置的超时时间到达后，调用alarm的进程将收到SIGALRM信号
 */
int sigs[] = {SIGIO,SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

int setup_listener() {
  int rc = -1, one=1, fl;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    fprintf(stderr,"socket: %s\n", strerror(errno));
    goto done;
  }

  /**********************************************************
   * internet socket address structure: our address and port
   *********************************************************/
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = cfg.addr;
  sin.sin_port = htons(cfg.port);

  /**********************************************************
   * bind socket to address and port
   * 允许完全重复捆绑
   *********************************************************/
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
    fprintf(stderr,"bind: %s\n", strerror(errno));
    goto done;
  }

  /**********************************************************
   * request signal be sent to us on descriptor ready
   * fcntl是计算机中的一种函数，通过fcntl可以改变已打开的文件性质
   * 复制一个现有的描述符(cmd=F_DUPFD).
   * 获得／设置文件描述符标记(cmd=F_GETFD或F_SETFD).
   * 获得／设置文件状态标记(cmd=F_GETFL或F_SETFL).
   * 获得／设置异步I/O所有权(cmd=F_GETOWN或F_SETOWN).
   * 获得／设置记录锁(cmd=F_GETLK , F_SETLK或F_SETLKW).
   * F_GETOWN, F_SETOWN, F_GETSIG 和 F_SETSIG 被用于IO可获取的信号
   * F_GETOWN：获取当前在文件描述词 fd上接收到SIGIO 或 SIGURG事件信号的进程或进程组标识 。
   * F_SETOWN：设置将要在文件描述词fd上接收SIGIO 或 SIGURG事件信号的进程或进程组标识 。
   * F_GETSIG：获取标识输入输出可进行的信号。
   * F_SETSIG：设置标识输入输出可进行的信号。
   *********************************************************/
  fl = fcntl(fd, F_GETFL); 
  fl |= O_ASYNC|O_NONBLOCK;     /* want a signal on fd ready */
  fcntl(fd, F_SETFL, fl);
  fcntl(fd, F_SETSIG, SIGIO);
  fcntl(fd, F_SETOWN, cfg.pid); /* send it to our pid */

  /**********************************************************
   * put socket into listening state
   *********************************************************/
  if (listen(fd,1) == -1) {
    fprintf(stderr,"listen: %s\n", strerror(errno));
    goto done;
  }

  cfg.fd = fd;
  rc=0;

 done:
  if ((rc < 0) && (fd != -1)) close(fd);
  return rc;
}

/* accept a new client connection to the listening socket */
int accept_client() {
  int fd, fl;
  struct sockaddr_in in;
  socklen_t sz = sizeof(in);

  fd = accept(cfg.fd,(struct sockaddr*)&in, &sz);
  if (fd == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) goto done;
    fprintf(stderr,"accept: %s\n", strerror(errno)); goto done;
  }

  if (cfg.verbose && (sizeof(in)==sz)) {
    fprintf(stderr,"connection fd %d from %s:%d\n", fd,
    inet_ntoa(in.sin_addr), (int)ntohs(in.sin_port));
  }

  /* request signal whenever input from client is available */
  fl = fcntl(fd, F_GETFL);
  fl |= O_ASYNC|O_NONBLOCK;        /* want a signal on fd ready */
  fcntl(fd, F_SETFL, fl);
  fcntl(fd, F_SETSIG, SIGIO);
  fcntl(fd, F_SETOWN, cfg.pid);    /* send it to our pid */

 done:
  if (fd != -1) utarray_push_back(cfg.fds,&fd);
  return fd;
}

void drain_clients() {
  int rc, *fd, pos;
  char buf[1024];

  fd=NULL;
  while ( (fd=(int*)utarray_next(cfg.fds,fd))) {
    do {
      rc = read(*fd, buf, sizeof(buf));
      switch(rc) { 
        default: fprintf(stderr,"received %d bytes\n", rc);         break;
        case  0: fprintf(stderr,"fd %d closed\n", *fd);             break;
        case -1: if (errno == EWOULDBLOCK || errno == EAGAIN)       break;
                 fprintf(stderr, "recv: %s\n", strerror(errno));    break;
      }
    } while(rc > 0);

    if (rc==0) {
      fprintf(stderr,"client %d has closed\n", *fd);
      close(*fd);
      *fd = -1; /* mark for cleanup after forward iteration */
    }
  }

  /* cleanup any sockets that we closed, reverse iteration */
  fd=NULL;
  while ( (fd=(int*)utarray_prev(cfg.fds,fd))) {
    pos = utarray_eltidx(cfg.fds,fd);
    if (*fd == -1) utarray_erase(cfg.fds,pos,1);
  }

}

/* this handles any network activity that might be pending. always ok to call. */
void service_network() {
  accept_client();
  drain_clients();
}

int main(int argc, char *argv[]) {
  cfg.prog = argv[0];
  cfg.prog=argv[0];
  cfg.pid = getpid();
  int n, signo, opt, *fd;
  siginfo_t info;
  utarray_new(cfg.fds,&ut_int_icd);

  while ( (opt=getopt(argc,argv,"vp:a:h")) != -1) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'p': cfg.port=atoi(optarg); break; 
      case 'a': cfg.addr=inet_addr(optarg); break; 
      case 'h': default: usage(); break;
    }
  }
  if (cfg.addr == INADDR_NONE) usage();
  if (cfg.port==0) usage();

  /* block all signals. we only take signals synchronously in sigwaitinfo */
  sigset_t all;
  sigfillset(&all);
  sigprocmask(SIG_SETMASK,&all,NULL);

  /* make a set of the signals we accept in sigwaitinfo. others blocked */
  sigset_t sw;
  sigemptyset(&sw);
  for(n=0; n < sizeof(sigs)/sizeof(*sigs); n++) sigaddset(&sw, sigs[n]);

  if (setup_listener()) goto done;

  alarm(1);
  while ( (signo = sigwaitinfo(&sw, &info)) > 0) {
    switch (signo) {
      case SIGALRM: if ((++cfg.ticks % 10) == 0) periodic(); alarm(1); break;
      case SIGIO:   service_network();                                 break;
      default:     printf("got signal %d\n", signo);                   goto done;
    }
  }

done:
  fd=NULL;
  while ( (fd=(int*)utarray_next(cfg.fds,fd))) close(*fd);
  utarray_free(cfg.fds);
  return 0;
}
