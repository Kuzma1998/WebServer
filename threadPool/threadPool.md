# 函数接口

```c++
在一个进程中调用线程创建函数，就可得到一个子线程，和进程不同，需要给每一个创建出的线程指定一个处理函数，否则这个线程无法工作。  
#include <pthread.h>
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg);
// Compile and link with -pthread, 线程库的名字叫pthread, 全名: libpthread.so libptread.a
参数:
thread: 传出参数，是无符号长整形数，线程创建成功，会将线程 ID 写入到这个指针指向的内存中
attr: 线程的属性，一般情况下使用默认属性即可，写 NULL
start_routine: 函数指针，创建出的子线程的处理动作，也就是该函数在子线程中执行。
arg: 作为实参传递到 start_routine 指针指向的函数内部
返回值：线程创建成功返回 0，创建失败返回对应的错误号

```