#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include "lock.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
class http_conn {
public:
    static int m_epollfd;     // 所有连接所对应的epoll对象
    static int m_user_count;  // 统计连接的数量
    // 常量
    static const int READ_BUFFER_SIZE = 2048;

public:
    http_conn();
    ~http_conn();

public:
    void init(int sockfd, const sockaddr_in& addr);  //初始化新的连接
    void close_conn();                               // 关闭
    void process();                                  // 处理新的连接
    bool read();                                     // 非阻塞读
    bool write();                                    // 非阻塞写
private:
    void init();

private:
    int         m_sockfd;   // 该http连接对应的fd
    sockaddr_in m_address;  // 对应的地址
};

#endif