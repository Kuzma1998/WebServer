#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "lock.h"
#include "sql_connection_pool.h"
#include <iostream>
#include <list>
#include <pthread.h>

// 线程池类，定义为模板类为了代码的复用 模板参数T是任务类
template <typename T> class threadPool {
public:
    threadPool(
        connection_pool* connPool,
        int              thread_number = 8,
        int              max_requests  = 10000);
    ~threadPool();
    bool append(T* request);  //添加任务

private:
    static void* worker(void* args);
    void         run();

private:
    int              m_thread_number;  //  线程池里面线程的数量
    pthread_t*       m_threads;        // 线程池数组
    int              m_max_requests;   // 线程池里面最多的请求数
    std::list<T*>    m_workerqueue;    // 任务队列，生产者进程
    locker           m_queuelocker;    // 互斥锁
    sem              m_queuestat;  // 信号量判断是否有任务需要处理
    bool             m_stop;       //是否结束线程
    connection_pool* m_connPool;   //数据库
};

template <typename T>
threadPool<T>::threadPool(
    connection_pool* connPool,
    int              thread_number,
    int              max_requests)
    : m_thread_number(thread_number), m_max_requests(max_requests),
      m_stop(false), m_threads(nullptr),m_connPool(connPool)
{
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }
    // 线程数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建thread_number线程，并设置为线程分离

    for (int i = 0; i < thread_number; ++i) {
        std::cout << "create the " << i << "th thread" << std::endl;
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 线程分离，当线程退出
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T> threadPool<T>::~threadPool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T> bool threadPool<T>::append(T* request)
{
    m_queuelocker.lock();                         // 临界区之前加锁
    if (m_workerqueue.size() > m_max_requests) {  //  任务队列大于最大值，失败
        m_queuelocker.unlock();                   // 先解锁
        return false;                             // 返回失败
    }
    m_workerqueue.push_back(request);  // 任务入队
    m_queuelocker.unlock();            // 解锁
    m_queuestat.post();  // 任务多了一个，信号量代表任务的个数，执行p操作+1
    return true;
}

template <typename T> void* threadPool<T>::worker(void* args)
{
    threadPool* pool = (threadPool*)args;
    pool->run();
    return pool;
}

// 线程池运行
template <typename T> void threadPool<T>::run()
{
    while (!m_stop) {
        m_queuestat.wait();  // 资源减一，如果小于0线程阻塞在任务队列上面
        m_queuelocker.lock();         // 有的话先上锁
        if (m_workerqueue.empty()) {  // 任务队列空
            m_queuelocker.unlock();   // 解锁。下次循环
            continue;
        }
        T* request = m_workerqueue.front();  // 获取队首任务
        m_workerqueue.pop_front();           // 弹出
        m_queuelocker.unlock();              // 解锁

        if (!request) {
            continue;
        }
        connectionRAII mysqlcon(&request->mysql,m_connPool);
        request->process();  // 处理任务
    }
}

#endif