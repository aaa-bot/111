#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};


/*
brief:
  创建一个线程池，用于处理任务。
  设置线程池的线程数量、最大请求数量等参数。
  创建指定数量的工作线程，每个工作线程会等待任务队列中的任务并执行。

    actor_model：表示选择的并发模式，具体含义可能是不同的并发模型。在这里作为一个参数传递，但没有直接使用。
    connPool：连接池指针，用于处理连接。
    thread_number：线程池中的线程数量，表示可以同时处理的最大任务数。
    max_requests：最大请求数量，表示任务队列中允许的最大任务数量
*/
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    // 检查线程数量和最大请求数量是否合法
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 动态分配存储线程 ID 的内存空间
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)//检查内存分配是否失败
        throw std::exception();
    // 创建工作线程    
    for (int i = 0; i < thread_number; ++i)
    {
        // 创建线程，每个线程调用 worker 函数进行任务处理
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            // 如果线程创建失败，释放已经创建的线程数组内存
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程设置为分离状态，以便在工作线程执行完毕后能够自动释放资源
        if (pthread_detach(m_threads[i]))
        {
            // 如果设置线程分离状态失败，释放已经创建的线程数组内存
            delete[] m_threads;
            throw std::exception();
        }
    }
}


template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

//向线程池工作队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)//T* request 是指向要添加的任务对象的指针；int state 是要为该任务设置的状态值
{
    m_queuelocker.lock();//添加线程时先上锁
    if (m_workqueue.size() >= m_max_requests)//工作队列超过了最大数量，无法添加，此时解锁并返回false
    {
        m_queuelocker.unlock();
        return false;
    }

    request->m_state = state;//设置任务状态
    m_workqueue.push_back(request);//添加至队列中
    m_queuelocker.unlock();//解锁
    m_queuestat.post();//post()对信号量释放，通过m_queuestat对等待的线程进行信号通知，让他们开始执行任务
    return true;
}

//append_p：通用的添加任务方法，可以添加无状态的任务，仅仅是将任务添加到工作队列中，并通知等待的线程开始处理任务。
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}


template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;//将传入参数arg强制转换为线程池指针类型，赋值给pool指针
    pool->run();//调用线程池对象的run()方法，开始执行任务
    return pool;//由于线程池对象需要在多个线程之间共享，因此将当前线程执行完毕后的线程池对象指针作为返回值返回
}

//任务执行函数
template <typename T>
void threadpool<T>::run()
{
    //循环从工作队列中去除任务
    while (true)
    {
        /*
        使用了信号量 m_queuestat 进行线程间的同步，使得当前线程在任务队列为空时能够等待，并在有任务时被唤醒。
        使用互斥锁 m_queuelocker 对任务队列进行加锁保护，防止多个线程同时访问队列导致的数据竞争问题。
        */
        m_queuestat.wait();// 信号量等待状态，使当前线程等待直到任务队列中有任务可供执行
        m_queuelocker.lock();//上锁
        if (m_workqueue.empty())//工作队列为空
        {
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front();//取出第一个进行执行
        m_workqueue.pop_front();//从队列中删除
        m_queuelocker.unlock();//释放任务队列锁
        if (!request)
            continue;


        /*
        并发模式：
            当使用并发模式时，程序会针对每个 HTTP 请求创建一个新的任务，并将其放入线程池中处理。这意味着多个请求可以同时被处理，每个请求都有自己的线程负责处理。这种方式充分利用了多核处理器的并行性，可以提高系统的并发性能。
            在并发模式下，每个请求都会拥有自己的数据库连接，因此数据库操作不会因为其他请求的阻塞而被延迟。这可以提高系统的响应速度和吞吐量。
        不适用并发模式：
            当不适用并发模式时，程序可能会使用单线程或者少量线程来处理所有的 HTTP 请求。这意味着所有的请求都会在同一个线程中按顺序处理，没有并行处理的能力。在这种情况下，如果有一个请求耗时较长，会阻塞其他请求的处理。
            在不适用并发模式下，所有的请求可能会共享同一个数据库连接，因此如果有一个请求在执行数据库操作时阻塞了，其他请求可能也会受到影响，导致整个系统的响应速度变慢。
        */
        if (1 == m_actor_model)//并发模式
        {
            if (0 == request->m_state)//读
            {
                //read_once()作用是从套接字读到读缓冲区
                //返回 true，表示一次性读取数据成功，即从套接字中读取了数据到缓冲区中。
                if (request->read_once())
                {
                    request->improv = 1; //表明该任务被处理
                    //&request->mysql：http_conn对象的数据库成员指针  m_connPool：数据库连接池对象
                    /*
                        创建 connectionRAII 对象 mysqlcon，用于管理数据库连接。
                        这里将 request->mysql（即 http_conn 对象的数据库成员指针）与数据库连接池对象 m_connPool 关联起来
                        确保在离开作用域时释放数据库连接。
                    */
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    //处理 HTTP 请求，可能会涉及数据库的读写操作。
                    request->process(); //调用http_conn的process函数进行向链接的数据库的读写操作
                }
                //返回 false，表示一次性读取数据失败，即没有从套接字中读取到数据。
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;//需要设置定时器
                }
            }
            else//写
            {
                if (request->write())//上面判断了不是读操作，则直接进行写操作
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
