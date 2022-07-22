#include "http_conn.h"
#include "lock.h"
#include "threadPool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#define MAX_FD 65536             // 最大用户连接数
#define MAX_EVENTS_NUMBER 10000  // 监听的最大事件数量

extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main()
{
    // if (argc <= 1) {
    //     std::cout << "usage: " << basename(argv[0]) << std::endl;
    //     return 1;
    // }

    // int port = atoi(argv[1]);
    int port = 10000;
    addsig(SIGPIPE, SIG_IGN);
    // 创建线程池
    threadPool<http_conn>* pool = nullptr;
    try {
        pool = new threadPool<http_conn>();
    }
    catch (...) {  //省略号的作用是表示捕获所有类型的异常。
        return 1;
    }
    // 所有用户连接数组
    http_conn* users = new http_conn[MAX_FD];

    // 获取监听的端口 使用tcp协议
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    // 服务端的ip和端口
    int                ret = -1;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);  // 转换为网络大端口

    // 端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    // 监听
    ret = listen(listenfd, 5);

    //  epoll事件数组
    epoll_event events[MAX_EVENTS_NUMBER];
    // 创建eopll对象 通过epollfd可以找到这个实例
    int epollfd = epoll_create(5);
    // 添加监听用的文件描述符

    epoll_event event;
    event.data.fd = listenfd;
    // 过EPOLLRDHUP属性，来判断是否对端已经关闭，
    // 这样可以减少一次系统调用。在2.6.17的内核版本之前，只能再通过调用一次recv函数来判断
    event.events = EPOLLRDHUP | EPOLLIN;
    event.events |= EPOLLONESHOT;
    // 添加事件
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

    // addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true) {
        // 等待，-1代表阻塞
        int number = epoll_wait(epollfd, events, MAX_EVENTS_NUMBER, -1);
        // errno是全局变量，表示上一个调用的错误代码，如果成功就为0.
        // EINTER 是系统调用返回错误的信号
        if ((number < 0) && (errno != EINTR)) {
            std::cout << "epoll fail" << std::endl;
            break;
        }

        // std::cout << "number" << number << std::endl;

        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t          client_addrlength = sizeof(client_address);
                int                connfd            = accept(
                                              listenfd, (struct sockaddr*)&client_address,
                                              &client_addrlength);
                if (connfd < 0) {
                    std::cout << "errno is: " << errno << std::endl;
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD) {
                    close(connfd);  // 关闭
                    continue;
                }
                users[connfd].init(connfd, client_address);
            }
            // 读写关闭或者读关闭或者错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            }
            // 有数据可读
            else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                }
                else {  // 读取失败
                    users[sockfd].close_conn();
                }
            }  // 有数据可以写
            else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;
}
