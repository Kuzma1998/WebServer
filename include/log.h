#ifndef LOG_H
#define LOG_H

#include "block_queue.h"
#include <iostream>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>

class Log {
private:
    char      dir_name[128];   //路径名
    char      log_name[128];   // log文件名
    int       m_split_lines;   //日志最大行数
    int       m_log_buf_size;  //日志缓冲区大小
    long long m_count;         //日志行数记录
    int       m_today;  //因为按天分类,记录当前时间是那一天
    FILE*     m_fp;     //打开log的文件指针
    char*     m_buf;
    block_queue<std::string>* m_log_queue;  //阻塞队列
    bool                      m_is_async;   //是否同步标志位
    locker                    m_mutex;

public:
    // c++11之后，局部变量懒汉不需要加锁
    static Log* get_instance()
    {
        static Log instance;
        return &instance;
    }

    // 异步写入日志
    static void *flush_log_thread(void* args)
    {
        Log::get_instance()->async_write_log();
    }

    bool init(
        const char* filename,
        int         log_buffer_size = 8192,
        int         split_lines     = 5000000,
        int         max_queue_size  = 0);  // 为0代表同步

    void write_log(int level, const char* format, ...);
    void flush(void);

    

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        std::string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            // str，一个数组，包含了要写入的以空字符终止的字符序列。
            // stream，指向FILE对象的指针，该FILE对象标识了要被写入字符串的流。
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
};

// 可变参数宏
#define LOG_DEBUG(format, ...)                                                 \
    Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)                                                  \
    Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)                                                  \
    Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)                                                 \
    Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif