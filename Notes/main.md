# 主要功能

# 函数接口

```C++
sigaction () 函数和 signal () 函数的功能是一样的，用于捕捉进程中产生的信号，并将用户自定义的信号行为函数（回调函数）注册给内核，内核在信号产生之后调用这个处理动作。

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

参数:
signum: 要捕捉的信号
act: 捕捉到信号之后的处理动作
oldact: 上一次调用该函数进行信号捕捉设置的信号处理动作，该参数一般指定为 NULL
返回值：函数调用成功返回 0，失败返回 - 1
该函数的参数是一个结构体类型，结构体原型如下：

struct sigaction {
	void     (*sa_handler)(int);    // 指向一个函数(回调函数)
	void     (*sa_sigaction)(int, siginfo_t *, void *);
	sigset_t   sa_mask;             // 初始化为空即可, 处理函数执行期间不屏蔽任何信号
	int        sa_flags;	        // 0
	void     (*sa_restorer)(void);  //不用
};
结构体成员介绍：
sa_handler: 函数指针，指向的函数就是捕捉到的信号的处理动作
sa_sigaction: 函数指针，指向的函数就是捕捉到的信号的处理动作
sa_mask: 在信号处理函数执行期间，临时屏蔽某些信号 , 将要屏蔽的信号设置到集合中即可
当前处理函数执行完毕，临时屏蔽自动解除
假设在这个集合中不屏蔽任何信号，默认也会屏蔽一个（捕捉的信号是谁，就临时屏蔽谁）
sa_flags：使用哪个函数指针指向的函数处理捕捉到的信号
	0：使用 sa_handler (一般情况下使用这个)
	SA_SIGINFO：使用 sa_sigaction (使用信号传递数据 == 进程间通信)
	sa_restorer: 被废弃的成员

// 将set集合中所有的标志位设置为0
int sigemptyset(sigset_t *set);
// 将set集合中所有的标志位设置为1
int sigfillset(sigset_t *set);
// 将set集合中某一个信号(signum)对应的标志位设置为1
int sigaddset(sigset_t *set, int signum);
// 将set集合中某一个信号(signum)对应的标志位设置为0
int sigdelset(sigset_t *set, int signum);
// 判断某个信号在集合中对应的标志位到底是0还是1, 如果是0返回0, 如果是1返回1
int sigismember(const sigset_t *set, int signum);


SIG_IGN忽略,内核把僵尸进程交给init去回收
SIG_DFL默认，这俩宏也可以作为信号处理函数。
同时SIGSTOP/SIGKILL这俩信号无法捕获和忽略。

```




```c++
// 联合体, 多个变量共用同一块内存        
typedef union epoll_data {
 	void        *ptr;
	int          fd;	// 通常情况下使用这个成员, 和epoll_ctl的第三个参数相同即可
	uint32_t     u32;
	uint64_t     u64;
} epoll_data_t;

struct epoll_event {
	uint32_t     events;      /* Epoll events */
	epoll_data_t data;        /* User data variable */
};

```


```c++
int epoll_wait(int epfd, struct epoll_event * events, int maxevents, int timeout);
```