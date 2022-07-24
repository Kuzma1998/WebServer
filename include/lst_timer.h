#ifndef LST_TIMER
#define LST_TIMER

#include <iostream>
#include <netinet/in.h>
#include <time.h>

class util_timer;
struct client_data
{
    sockaddr_in address;
    int         sockfd;
    util_timer* timer;
};

// 定时器
class util_timer {
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    void (*cb_func)(client_data*);
    client_data* user_data;
    util_timer*  prev;
    util_timer*  next;
};

// 基于定时器的升序链表
class sort_timer_list {
public:
    util_timer* prev;
    util_timer* next;

public:
    sort_timer_list() : prev(NULL), next(NULL) {}
    ~sort_timer_list()
    {
        util_timer* tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    // 添加某节点
    void add_timer(util_timer* timer)
    {  // 如果timer为空
        if (!timer) {
            return;
        }
        // 如果head为空
        if (!head) {
            head = timer;
            return;
        }
        // 如果要插入的timer的时间最小，timer插在头节点之前
        if (timer->expire < head->expire) {
            timer->next = head->next;
            head->prev  = timer;
            head        = timer;
            return;
        }
        add_timer(timer, head);
    }

    // 删除某个节点
    void del_timer(util_timer* timer)
    {
        if (!timer) {
            return;
        }
        if (timer == head && timer == tail) {
            delete timer;
            head = nullptr;
            tail = nullptr;
        }
        if (timer == head) {
            head       = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        if (timer == tail) {
            tail       = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    /* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。
    这个函数只考虑被调整的定时器的
    超时时间延长的情况，即该定时器需要往链表的尾部移动。*/
    void adjust_timer(util_timer* timer)
    {
        if (!timer) {
            return;
        }
        // 如果被调整的目标定时器处在链表的尾部，或者该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
        util_timer* tmp = timer->next;
        if (!tmp || (timer->expire < tmp->expire)) {
            return;
        }
        // 如果目标定时器是链表的头节点，则将该定时器从链表中取出并重新插入链表
        if (timer == head) {
            head        = head->next;
            head->prev  = nullptr;
            timer->next = nullptr;
            add_timer(timer, head);
        }
        else {
            // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    /* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick()
     * 函数，以处理链表上到期任务。*/
    void tick()
    {
        if (!head) {
            return;
        }
        std::cout << "timer tick" << std::endl;
        // 获取当前系统时间
        time_t      cur = time(NULL);
        util_timer* tmp = head;
        // 从当前节点开始处理每个定时器，直到遇到一个到期的开始处理
        while (tmp) {
            /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
           比较以判断定时器是否到期*/
            if (cur < tmp->expire) {
                break;  // 没到期，退出
            }
            // 调用定时器的回调函数，以执行定时任务
            tmp->cb_func(tmp->user_data);
            // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
            head = tmp->next;
            if (head) {
                head->prev = nullptr;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    util_timer* head;
    util_timer* tail;

private:
    // 插入一个节点
    void add_timer(util_timer* timer, util_timer* lst_head)
    {
        util_timer* prev = lst_head;
        util_timer* tmp  = prev->next;
        while (tmp) {
            if (timer->expire < tmp->expire) {
                prev->next  = timer;
                timer->next = tmp;
                tmp->prev   = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp  = tmp->next;
        }
        if (!tmp) {
            prev->next  = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail        = timer;
        }
    }
};

#endif