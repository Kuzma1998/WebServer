#include "http_conn.h"
#include <iostream>

int setnonblocking(int fd)
{
    //返回fd的文件描述符信息
    int old_option = fcntl(fd, F_GETFL);
    // 设置为非阻塞
    int new_option = old_option | O_NONBLOCK;
    // 设置新的文件描述符状态
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 给一个epoll实例对象添加监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    setnonblocking(fd);
    epoll_event event;
    event.data.fd = fd;
    // 过EPOLLRDHUP属性，来判断是否对端已经关闭，
    // 这样可以减少一次系统调用。在2.6.17的内核版本之前，只能再通过调用一次recv函数来判断
    event.events = EPOLLRDHUP | EPOLLIN;
    if (one_shot) {
        // 防止同一个连接被多个不同线程处理
        event.events |= EPOLLONESHOT;
    }
    // 添加事件
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}
//从epoll实例对象删一个文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改已经存在的节点,重置ONESHOT事件(处理过一个注册了ONESHOT事件的fd之后就不可以用了，需要创新设置)
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events  = ev | EPOLLET | EPOLLONESHOT;  // 设置边沿
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//客户端的数量
int http_conn::m_user_count = 0;
// 所有的通信使用的socket都注册到同一个epoll内核事件中
int http_conn::m_epollfd = -1;

// 构造函数
http_conn::http_conn() {}

// 析构函数
http_conn::~http_conn() {}

//初始化连接
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd  = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

void http_conn::init() {}

bool http_conn::read()
{
    std::cout << "一次性读完数据" << std::endl;
    return true;
}

bool http_conn::write()
{
    std::cout << "一次性写完数据" << std::endl;
    return true;
}

// 关闭连接
void http_conn ::close_conn()
{
    if (m_sockfd != -1) {  // accpet调用失败返回-1
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 线程池的工作线程调用 处理HTTP请求的入口函数
void http_conn::process()
{
    //解析http请求
    std::cout << "parse request, create response" << std::endl;
}
