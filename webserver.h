#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    //初始化服务器（包含端口、用户信息、数据库信息、触发模式等）
    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    //基础，定义了一些服务器信息的对象，与配置文件类似，通过配置文件对象的信息来初始化服务器信息
    int m_port;
    char *m_root;// 网站根目录
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_pipefd[2];// 父进程和子进程间通信的管道描述符
    /*
    m_pipefd[0] 和 m_pipefd[1] 是一对 UNIX 域管道的文件描述符，通常用于在同一台计算机上的两个进程之间进行通信。
    m_pipefd[0] 代表管道的读端，而 m_pipefd[1] 代表管道的写端。
    这对文件描述符用于实现进程间的通信，其中一个进程可以向管道的写端写入数据，而另一个进程则可以从管道的读端读取这些数据。

    如果作为一个通信机制，其中一个进程可以向写端写入数据，而另一个进程则可以从读端读取这些数据，用于进程间的通信。
    如果作为一个触发机制，其中一个进程向管道的写端写入某些数据，用于触发另一个进程在 epoll 事件监听器上监听到读端的事件时执行特定的操作。
    */
    
    int m_epollfd;// epoll句柄
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];// 用于存储epoll_wait返回的事件的数组

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;// 监听socket的触发模式
    int m_CONNTrigmode;// 连接socket的触发模式


    //定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif


/*
连接池：
连接池是一种管理和复用数据库连接、网络连接等资源的技术。
在服务器端，连接池通常用于管理数据库连接或者其他网络连接，例如 HTTP 连接等。
连接池在服务器启动时会预先创建一定数量的连接，并将它们保存在池中。当客户端请求到来时，可以从连接池中获取一个空闲的连接，用于处理客户端的请求。
客户端处理完毕后，将连接归还给连接池，而不是关闭连接，以便后续的请求可以重复利用这些连接，避免了频繁地创建和销毁连接的开销。
连接池可以控制连接的数量，防止资源被耗尽，从而提高服务器的稳定性和性能。


线程池：
线程池是一种管理和复用线程的技术，用于处理并发请求。
在服务器端，线程池通常用于处理客户端的请求，每个请求被封装为一个任务，然后由线程池中的线程来执行这些任务。
线程池在服务器启动时会创建一定数量的线程，并将它们保存在池中。当客户端请求到来时，可以从线程池中获取一个空闲的线程，用于处理客户端的请求。
线程池可以控制线程的数量，避免因为线程过多导致系统资源被耗尽，同时也可以提高服务器的性能，因为线程的创建和销毁开销较大。
线程池可以通过任务队列来缓解高并发时的压力，即使线程暂时无法处理新的请求，也可以将请求放入任务队列中等待处理，而不会导致请求被丢弃或者服务器崩溃。

*/
