#include "../include/log.h"
#include <string.h>

Log::Log()
{
    m_count    = 0;      // 记录日志写了多少行，一开始为0
    m_is_async = false;  // 默认同步
}

Log::~Log()
{
    if (!m_fp) {
        fclose(m_fp);
    }
}

// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(
    const char* file_name,
    int         log_buf_size,
    int         split_lines,
    int         max_queue_size)
{
    // 如果设置了max_queue_size，则为异步
    if (max_queue_size >= 1) {
        m_is_async = true;
        // 阻塞队列
        m_log_queue = new block_queue<std::string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf          = new char[m_log_buf_size];  // 日志缓冲区
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);

    /*
    struct tm {
    int tm_sec; /* 秒–取值区间为[0,59]
    int tm_min;  /* 分 - 取值区间为[0,59]
    int tm_hour; /* 时 - 取值区间为[0,23]
    int tm_mday; /* 一个月中的日期 - 取值区间为[1,31]
    int tm_mon; /* 月份（从一月开始，0代表一月） - 取值区间为[0,11]
    int tm_year; /* 年份，其值从1900开始
    int tm_wday; /* 星期–取值区间为[0,6]，其中0代表星期天，1代表星期一，以此类推
    int tm_yday; /*
从每年的1月1日开始的天数–取值区间为[0,365]，其中0代表1月1日，1代表1月2日，以此类推
    int tm_isdst; /*
夏令时标识符，实行夏令时的时候，tm_isdst为正。不实行夏令时的进候，tm_isdst为0；不了解情况时，tm_isdst()为负。
};
*/

    struct tm* sys_tm = localtime(&t);
    struct tm  my_tm  = *sys_tm;
    // C 库函数 char *strrchr(const char *str, int c) 在参数 str
    // 所指向的字符串中搜索最后一次出现字符 c（一个无符号字符）的位置。
    const char* p                  = strrchr(file_name, '/');
    char        log_full_name[256] = {0};  // 全民

    if (p == nullptr) {
        // C 库函数 int snprintf(char *str, size_t size, const char *format,
        // ...) 设将可变参数(...)按照 format 格式化成字符串，并将字符串复制到
        // str 中，size 为要写入的字符的最大数目，超过 size 会被截断。
        // 当前时间形成字符串作为日志全名
        snprintf(
            log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
            my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        // 取得日志名字
        strcpy(log_name, p + 1);
        // 取得路径
        strncpy(dir_name, file_name, p - file_name + 1);
        // 日志全名
        snprintf(
            log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name,
            my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    // 追加到一个文件。写操作向文件末尾追加数据。如果文件不存在，则创建文件。
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL) {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    //     struct timeval
    // {
    //     long tv_sec; /*秒*/
    //     long tv_usec; /*微秒*/
    // };
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t     t      = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm  my_tm  = *sys_tm;
    char       s[16]  = {0};
    switch (level) {
        case 0: strcpy(s, "[debug]:"); break;
        case 1: strcpy(s, "[info]:"); break;
        case 2: strcpy(s, "[warn]:"); break;
        case 3: strcpy(s, "[erro]:"); break;
        default: strcpy(s, "[info]:"); break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        // 新日志
        char new_log[256] = {0};
        // 之前的文件指针内容刷掉
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(
            tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1,
            my_tm.tm_mday);

        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
        }
        else {
            snprintf(
                new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name,
                m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);
    std::string log_str;

    m_mutex.lock();
    //写入的具体时间内容格式
    int n = snprintf(
        m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
        my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour,
        my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    // 可变参数列表写入m_buf
    int m            = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m]     = '\n';
    m_buf[n + m + 1] = '\0';
    log_str          = m_buf;

    m_mutex.unlock();

    // 异步写，加入日志队列
    if (m_is_async && !m_log_queue->full()) {
        m_log_queue->push(log_str);
    }
    else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);  //  写到文件流的缓冲区
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    // 刷盘
    fflush(m_fp);
    m_mutex.unlock();
}
