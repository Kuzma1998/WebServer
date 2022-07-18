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

void http_conn::init()
{
    bytes_to_send   = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;  // 初始状态为检查请求行
    m_linger = false;  // 默认不保持链接  Connection : keep-alive保持连接

    m_method         = GET;  // 默认请求方式为GET
    m_url            = 0;
    m_version        = 0;
    m_content_length = 0;
    m_host           = 0;
    m_start_line     = 0;
    m_checked_idx    = 0;
    m_read_idx       = 0;
    m_write_idx      = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        // 从m_read_buf+m_read_idx开始读取数据,最后一个参数一般设置为0
        bytes_read = recv(
            m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx,
            0);

        // 当errno为EAGAIN或EWOULDBLOCK时，表明读取完毕，接受缓冲为空，
        // 在非阻塞IO下会立即返回-1.若errno不是上述标志，则说明读取数据出错，
        // 因该关闭连接，进行错误处理。
        if (bytes_read == -1) {  // 读取错误
            if (errno == EAGAIN || errno = EWOULDBLOCK) {
                break;
            }
            return false;
        }
        else if (bytes_read == 0) {
            // 断开连接
            return false;
        }
        // 指针偏移
        m_read_idx += bytes_read;
    }
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
    // 解析http请求
    HTTP_CODE = read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    // 生成响应之后让主线程监听EPOLLOUT事件发送出去
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;  //一开始读的行状态初始化为ok
    HTTP_CODE   ret  = NO_REQUEST;      // 一开始http状态码为没有请求
    char*       text = 0;
    while (
        ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) ||
        ((line_status = parse_line()) == LINE_OK)) {
        // 解析一行没有错误才继续循环
        text = get_line();
        // 每次读取完一行之后把text更新为读缓冲区里面位置,从该位置继续往后读
        m_start_line = m_checked_idx;
        std::cout << "get 1 http line: " << text << std::endl;

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {  //分析请求行
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {  // 解析http请求行结果
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    // 如果首部后面没有内容,则已经解析完了 需要do_request
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析一行
http_conn::LINE_STATUS http_conn::parse_line() {}

// http_conn::HTTP_CODE http_conn::