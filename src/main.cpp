#include "http_conn.h"
#include "lock.h"
#include "lst_timer.h"
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
#define TIMESLOT 5               // 最小超时单位

extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void setnonblocking(int fd);

// 定时器相关参数
static int             pipefd[2];
static sort_timer_list timer_list;
static int             epollfd = 0;

// 信号处理函数,把信号写入管道信号处理函数中仅仅通过管道发送信号，
// 不处理信号对应的逻辑，缩短异步执行时间，值减少对主程序的影响。
void sig_handler(int sig)
{
    // 为了保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg        = sig;
    // 0代表阻塞式发送
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数  仅关注SIGTERM和SIGALRM两个信号。
void addsig(int sig, void(handler)(int), bool restart = true)
{
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务
void timer_handler()
{
    timer_list.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，删除非活动连接在socket上的注册时间，并关闭
void cb_func(client_data* user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    std::cout << user_data->sockfd << std::endl;
    std::cout << "连接关闭了..." << std::endl;
    http_conn::m_user_count--;
}

void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
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
    std::cout << "listen socket " << listenfd << std::endl;

    // 服务端的ip和端口
    int                ret = -1;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);  // 转换为网络大端口

    // 端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定监听
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    ret = listen(listenfd, 5);

    //  epoll事件数组
    epoll_event events[MAX_EVENTS_NUMBER];

    // 创建eopll对象 通过epollfd可以找到这个实例
    epollfd = epoll_create(5);

    // 添加监听用的文件描述符
    epoll_event event;
    event.data.fd = listenfd;

    // 过EPOLLRDHUP属性，来判断是否对端已经关闭，
    // 这样可以减少一次系统调用。在2.6.17的内核版本之前，只能再通过调用一次recv函数来判断
    event.events = EPOLLRDHUP | EPOLLIN;

    // 添加事件
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);
    http_conn::m_epollfd = epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);  //非阻塞写
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_never = false;

    client_data* users_timer = new client_data[MAX_FD];

    bool timeout = false;

    // 设置信号传送闹钟，即用来设置信号SIGALRM在经过参数seconds秒数后发送给目前的进程。
    // 如果未设置信号SIGALRM的处理函数，那么alarm()默认处理终止进程.
    alarm(TIMESLOT);

    while (!stop_never) {
        // 等待，-1代表阻塞
        int number = epoll_wait(epollfd, events, MAX_EVENTS_NUMBER, -1);

        // errno是全局变量，表示上一个调用的错误代码，如果成功就为0.
        // EINTER 是系统调用返回错误的信号
        if ((number < 0) && (errno != EINTR)) {
            std::cout << "epoll fail" << std::endl;
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            // 处理新到的客户端连接
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
                std::cout << "建立连接: " << connfd << std::endl;

                if (http_conn::m_user_count >= MAX_FD) {
                    close(connfd);  // 关闭
                    continue;
                }

                users[connfd].init(connfd, client_address);

                // 初始化client_data数据
                // 创建定时器，设置回调函数和超时是按，绑定用户数据，将定时器添加到链表里面
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd  = connfd;
                util_timer* timer           = new util_timer;
                timer->user_data            = &users_timer[connfd];
                timer->cb_func              = cb_func;
                time_t cur                  = time(NULL);
                timer->expire               = cur + 3 * TIMESLOT;
                users_timer[connfd].timer   = timer;
                timer_list.add_timer(timer);
            }
            // 读写关闭或者读关闭或者错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务端关闭连接，移除对应的定时器
                util_timer* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if (timer) {
                    timer_list.del_timer(timer);
                }
            }
            // 处理信号
            else if (sockfd == pipefd[0] && events[i].events & EPOLLIN) {
                int  sig;
                char signals[1024];

                //从管道读端读出信号值，成功返回字节数，失败返回-1
                //正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv(pipefd[0], signals, sizeof(signals), 0);

                if (ret == -1) {
                    continue;
                }
                else if (ret == 0) {
                    continue;
                }
                else {
                    for (int i = 0; i < ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            // SIGTERM信号是用于终止程序的通用信号。
                            // SIGTERM提供了一种优雅的方式来杀死程序。
                            //  kill命令的默认行为是将SIGTERM信号发送到进程。
                            case SIGTERM: {
                                stop_never = true;
                            }
                        }
                    }
                }
            }
            // 有数据可读
            else if (events[i].events & EPOLLIN) {
                util_timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);

                    if (timer) {
                        // 刷新时间，往后延迟三个单位
                        time_t cur    = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                    }
                }
                else {
                    // 读取失败
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            }
            // 有数据可以写
            else if (events[i].events & EPOLLOUT) {
                util_timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].write()) {
                    // 刷新时间，往后延迟三个单位
                    if (timer) {
                        time_t cur    = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                    }
                }
                else {
                    // 读取失败
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            }
        }
        if (timeout) {
            // 处理超时的连接
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users_timer;
    delete[] users;
    delete pool;
    return 0;
}
