#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
// 线程同步机制封装类
// 多线程同步，确保任一时刻只能有一个线程能进入关键代码段.
// 信号量
// 互斥锁
// 条件变量
// 互斥锁类

class locker {
public:
    locker()  // 构造函数， // 初始化互斥锁
    {
        // restrict: 是一个关键字, 用来修饰指针,
        // 只有这个关键字修饰的指针可以访问指向的内存地址, 其他指针是不行的
        // int pthread_mutex_init(pthread_mutex_t *restrict mutex,
        //    const pthread_mutexattr_t *restrict attr);
        //         参数:
        // mutex: 互斥锁变量的地址
        // attr: 互斥锁的属性，一般使用默认属性即可，这个参数指定为 NULL
        //如果函数调用成功会返回 0，调用失败会返回相应的错误号：
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);  // 释放锁资源，传入一个地址
    }

    // 这个函数被调用，首先会判断参数 mutex 互斥锁中的状态是不是锁定状态:
    // 没有被锁定，是打开的，这个线程可以加锁成功，这个这个锁中会记录是哪个线程加锁成功了
    // 如果被锁定了，其他线程加锁就失败了，这些线程都会阻塞在这把锁上
    // 当这把锁被解开之后，这些阻塞在锁上的线程就解除阻塞了，并且这些线程是通过竞争的方式对这把锁加锁，没抢到锁的线程继续阻塞
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;  // 加锁
        // 修改互斥锁的状态, 将其设定为锁定状态, 这个状态被写入到参数 mutex 中
    }

    // 对互斥锁解锁
    // int pthread_mutex_unlock(pthread_mutex_t *mutex);
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // 返回锁的地址
    pthread_mutex_t* get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;  // 创建一个互斥锁类型
};

// 条件变量类
class cond {
public:
    // 构造函数
    cond()
    {  // 第一个参数为条件变量的地址，第二个参数一般指定为null
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    // 析构函数
    ~cond()
    {  // 释放条件变量资源
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t* m_mutex)
    {
        int ret = 0;
        ret     = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t)
    {
        int ret = 0;
        ret     = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

class sem  // 信号量类的包装  信号量表示资源的个数
{
public:
    sem()
    {  // 初始化信号量
        // int sem_init(sem_t *sem, int pshared, unsigned int value);
        //         参数:
        // sem：信号量变量地址
        // pshared：
        // 0：线程同步
        // 非 0：进程同步
        // value：初始化当前信号量拥有的资源数（>=0），如果资源数为
        // 0，线程就会被阻塞了。

        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem()
    {  //释放资源
        sem_destroy(&m_sem);
    }
    // p操作  // 函数被调用sem中的资源就会被消耗1个, 资源数-1
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    // v操作   调用该函数给sem中的资源数+1
    // 当信号量的值为负数的时候，这个值就是等待的线程个数 同时也会唤醒一个线程
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

#endif