/*************************************************************
 *循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
 *线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 **************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "lock.h"
#include <iostream>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>

template <class T> class block_queue {
private:
    locker m_mutex;
    cond   m_cond;
    T*     m_array;
    int    m_size;
    int    m_max_size;
    int    m_front;
    int    m_back;

public:
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size = max_size;
        m_array    = new T[max_size];
        m_size     = 0;
        m_front    = -1;
        m_back     = -1;
    }

    ~block_queue()
    {
        m_mutex.lock();
        if (!m_array) {
            delete[] m_array;
        }
        m_mutex.unlock();
    }

    void clear()
    {
        m_mutex.lock();
        m_size  = 0;
        m_front = -1;
        m_back  = -1;
        m_mutex.unlock();
    }

    bool full()
    {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 判断是否为空
    bool empty()
    {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    int size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T& item)
    {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back          = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T& item)
    {
        m_mutex.lock();
        while (m_size <= 0) {
            //当重新抢到互斥锁，pthread_cond_wait返回为0
            // pthread_cond_wait函数，用于等待目标条件变量。该函数调用时需要传入
            // mutex参数(加锁的互斥锁)
            // 数执行时，先把调用线程放入条件变量的请求队列，然后将互斥锁mutex解锁，当函数成功返回为0时，表示重新抢到了互斥锁，互斥锁会再次被锁上，
            // 也就是说函数内部会有一次解锁和加锁操作.
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        item    = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理
    bool pop(T& item, int ms_timeout)
    {
        struct timespec t   = {0, 0};
        struct timeval  now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec  = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item    = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }
};

#endif