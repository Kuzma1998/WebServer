#include "http_conn.h"
#include <iostream>

//定义HTTP响应的一些状态信息
const char* ok_200_title    = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form =
    "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/ljxdw/c++/WebServer/WebServer/resources";
int         setnonblocking(int fd)
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
    event.events = EPOLLRDHUP | EPOLLET | EPOLLIN;
    if (one_shot) {
        // 防止同一个连接被多个不同线程处理  只触发一次 还想使用需要再次注册
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
    event.events  = EPOLLET | EPOLLONESHOT | ev;  // 设置边沿
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
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        // 读取socket的数据,从m_read_buf+m_read_idx开始保存，最后一个参数一般设置为0
        bytes_read = recv(
            m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx,
            0);

        // 当errno为EAGAIN或EWOULDBLOCK时，表明读取完毕，接收缓冲为空，
        // 在非阻塞IO下会立即返回-1.若errno不是上述标志，则说明读取数据出错，
        // 因该关闭连接，进行错误处理。
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 数据读取完了
                break;
            }
            return false;  // 读取错误i
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
    HTTP_CODE read_ret = process_read();
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
        // 解析一行没有错误才继续循环,第一个条件是当内容检查完了就要退出循环
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
    return NO_REQUEST;  // 请求不完整
}

// 解析一行,判断\r\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;  //返回数据不完整
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (m_read_buf[m_checked_idx] == '\n') {
            if (m_checked_idx > 1 && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx--] = '\0';
                m_read_buf[m_checked_idx--] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法，目标URL(协议+域名+文件),以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // GET /index.html HTTP/1.1
    // 依次检验字符串 str1 中的字符，当被检验字符在字符串 str2
    // 中也包含时，则停止检验，并返回该字符位置。
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;  // 把/index前面制表符置为0，因此text截断为get方法
    // 忽略大小写比较字符串
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");

    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    // strncasecmp()用来比较参数s1和s2字符串前n个字符，比较时会自动忽略大小写的差异。
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    // 一般不会带有http,直接是/资源
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;  //变为请求头
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;  //请求不完整
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        // C 库函数 size_t strspn(const char *str1, const char *str2) 检索字符串
        // str1 中第一个不在字符串 str2 中出现的字符下标。
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        // C 库函数 long int atol(const char *str) 把参数 str
        // 所指向的字符串转换为一个长整数（类型为 long int 型）。
        m_content_length = atol(text);
    }
    else {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{  // 如果缓冲区的大小大于数据的长度+已有的长度，说明数据没有越界
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
// 见大炳
// struct stat {
//     dev_t          st_dev;        	// 文件的设备编号
//     ino_t           st_ino;        	// inode节点
//     mode_t      st_mode;      		// 文件的类型和存取的权限, 16位整形数 ->
//     常用 nlink_t        st_nlink;     	//
//     连到该文件的硬连接数目，刚建立的文件值为1 uid_t           st_uid; //
//     用户ID gid_t           st_gid;       	// 组ID dev_t          st_rdev;
//     // (设备类型)若此文件为设备文件，则为其设备编号 off_t            st_size;
//     // 文件字节数(文件大小)   --> 常用 blksize_t     st_blksize;   	//
//     块大小(文件系统的I/O 缓冲区大小) blkcnt_t      st_blocks;    	//
//     block的块数 time_t         st_atime;     	// 最后一次访问时间 time_t
//     st_mtime;     	// 最后一次修改时间(文件内容) time_t         st_ctime;
//     // 最后一次改变时间(指属性)
// };

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);  //复制最外面的路径
    int len = strlen(doc_root);
    //     C 库函数 char *strncpy(char *dest, const char *src, size_t n)
    // 把 src 所指向的字符串复制到 dest，最多复制 n 个字符。
    // 当 src 的长度小于 n 时，dest 的剩余部分将用空字节填充。
    strncpy(
        m_real_file + len, m_url,
        FILENAME_LEN - len - 1);  //外面的路径+请求资源
    // 文件是否存在
    // 文件信息存储到了结构体
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    // 是否可读
    if (!(m_file_stat.st_mode & S_IROTH)) {  //  其他用户拥有读权限
        return FORBIDDEN_REQUEST;
    }
    // 是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射 返回内存映射区的地址
    m_file_address =
        (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }

        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }

        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }

        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }

        case FILE_REQUEST: {
            // http状态行写入缓冲区
            add_status_line(20, ok_200_title);
            if (m_file_stat.st_size != 0) {
                // http header是写入缓冲区
                add_headers(m_file_stat.st_size);
                // 缓冲区http 状态行和http headers的大小和位置
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len  = m_write_idx;
                // body 的大小和位置
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len  = m_file_stat.st_size;
                m_iv_count       = 2;
                bytes_to_send    = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                //如果请求的资源大小为0，则返回空白html文件
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default: return false;  //
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len  = m_write_idx;
    m_iv_count       = 1;
    bytes_to_send    = m_write_idx;
    return true;
}

// 写http响应 到socket缓冲区/
bool http_conn::write()
{
    int temp = 0;
    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        if (m_linger) {
            init();
            return true;
        }
        else {
            return false;
        }
    }
    while (1) {
        //
        // writev函数用于在一次函数调用中写多个非连续缓冲区，有时也将这该函数称为聚集写。
        //         filedes表示文件描述符
        // iov为前述io向量机制结构体iovec
        // iovcnt为结构体的个数
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 缓冲区满了
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len  = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len  = bytes_to_send;
        }
        //继续发送第一个iovec头部信息的数据
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len  = m_iv[0].iov_len - temp;
        }
        if (bytes_to_send <= 0) {
            // 没有数据发送
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if (m_linger) {
                init();
                return true;
            }
            else {
                return false;
            }
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    // arg_list表示可变参数列表类型，实际上就是一个char指针fmt。
    va_list arg_list;
    // 初始化arg_list
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入写缓冲区，返回写入的数据的长度
    int len = vsnprintf(
        m_write_idx + m_write_buf, WRITE_BUFFER_SIZE - 1 - m_write_idx, format,
        arg_list);
    // 如果写入数据的长度超过剩余缓冲区，报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    // 更新write index的位置
    m_write_idx += len;
    // 清空列表
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_length)
{
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
}

// 添加内容
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length: %d\r\n", content_length);
}

//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response(
        "Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

void http_conn::unmap()
{
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
