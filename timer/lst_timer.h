#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

/*
util_timer 类：
该类表示一个定时器，用于管理定时事件。
    成员变量 expire 表示定时器的超时时间。
    成员变量 cb_func 是一个函数指针，指向定时器超时时要执行的回调函数。
    成员变量 user_data 是一个指向用户数据的指针，用于传递给回调函数。
    成员变量 prev 和 next 分别指向前一个和后一个定时器，用于组成定时器链表
*/
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;
    
    void (* cb_func)(client_data *);
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};


/*
sort_timer_lst 类：
该类表示一个定时器链表，用于管理多个定时器。
    成员函数 add_timer 用于向链表中添加定时器。
    成员函数 adjust_timer 用于调整定时器的超时时间。
    成员函数 del_timer 用于从链表中删除定时器。
    成员函数 tick 用于处理定时事件，即检查链表中的定时器是否超时并执行相应的操作
*/
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};


/*
Utils 类：
该类包含了一些常用的工具函数和全局变量。
    成员函数 setnonblocking 用于将文件描述符设置为非阻塞模式。
    成员函数 addfd 用于向 epoll 事件监听器中添加文件描述符，并设置监听模式。
    成员函数 sig_handler 是一个静态成员函数，用于处理信号。
    成员函数 addsig 用于注册信号处理函数。
    成员函数 timer_handler 用于处理定时器事件。
    成员函数 show_error 用于显示错误信息。
    静态成员变量 u_pipefd 用于保存管道文件描述符。
    静态成员变量 u_epollfd 用于保存 epoll 文件描述符。
    成员变量 m_timer_lst 是一个 sort_timer_lst 类型的对象，用于管理定时器链表。
    成员变量 m_TIMESLOT 表示定时器时间槽的间隔。
*/
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
