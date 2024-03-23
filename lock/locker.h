#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//brief
//提供了对 POSIX 线程库中信号量、互斥锁和条件变量的封装，使得在多线程编程中更方便地使用这些同步机制
//避免了直接调用底层接口可能出现的错误
/*
1、互斥锁和条件变量的关系：
互斥锁 (locker) 和条件变量 (cond) 经常一起使用，用于实现复杂的同步机制。
通常情况下，在使用条件变量时需要配合互斥锁使用，以保证对共享资源的访问是互斥的。
条件变量 (cond) 可以通过等待和唤醒机制来实现线程间的通信，当共享资源的状态发生变化时，线程可以通过条件变量等待或者被唤醒来进行相应的操作。

2、信号量和互斥锁的关系：
信号量 (sem) 和互斥锁 (locker) 都是用于实现线程同步和互斥操作的机制，但在功能和使用场景上略有不同。
互斥锁主要用于实现对共享资源的互斥访问，确保同一时刻只有一个线程可以访问共享资源，从而避免竞态条件。
信号量则可以用于更广泛的场景，除了实现互斥操作外，还可以实现线程间的同步、线程间的通信等，例如通过信号量可以实现生产者-消费者模型、控制线程数量等功能。
*/


//信号量 类
/*
信号量是一种同步机制，用于控制多个线程之间的访问顺序。
通常情况下，当多个线程需要共享一个资源时，需要使用信号量来确保每个线程按照一定的顺序访问该资源，从而避免出现竞争条件和数据不一致等问题
*/
class sem
{
public:
    /*
    //sem构造函数中调用的sem_init是一个用于初始化信号量的库函数
    sem：指向要初始化的信号量的指针。
    pshared：指定信号量是在进程间共享还是在线程间共享的标志。如果pshared为0，则表示信号量将在同一进程内的线程之间共享；如果pshared为非0值，则表示信号量可以在多个进程之间共享。
    value：指定信号量的初始值。
    */
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);//销毁信号量
    }

    // 该方法会对信号量进行等待操作，如果信号量的值为0，则该方法会阻塞当前线程，直到有其他线程通过 post() 方法将信号量的值增加至大于0，才会唤醒该线程
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // 该方法会对信号量进行释放操作，将信号量的值增加1，唤醒可能被阻塞的线程
    // 即有线程进行了释放操作，使得信号量空闲，这时被阻塞的线程可以使用该信号量（即被唤醒）
    bool post()
    {

        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};


//互斥锁 类
/*
信号量是一种同步机制，用于控制多个线程之间的访问顺序。通常情况下，当多个线程需要共享一个资源时，需要使用信号量来确保每个线程按照一定的顺序访问该资源，从而避免出现竞争条件和数据不一致等问题
*/
class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    //lock() 方法调用 pthread_mutex_lock() 函数对互斥锁进行加锁操作。
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    //unlock() 方法调用 pthread_mutex_unlock() 函数对互斥锁进行解锁操作。
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    //get() 方法返回互斥锁的指针。
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


//条件变量 类
/*
信号量是一种同步机制，用于控制多个线程之间的访问顺序。通常情况下，当多个线程需要共享一个资源时，需要使用信号量来确保每个线程按照一定的顺序访问该资源，从而避免出现竞争条件和数据不一致等问题
*/
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    // wait 函数会一直等待，直到被其他线程唤醒
    /*此处详细介绍wait函数的实现与功能，timewait函数同理
      功能：  
        当一个线程调用 wait() 方法时，它会将自己阻塞，并释放掉它所持有的互斥锁，然后等待条件变量的信号。
        在等待期间，该线程会被放入条件变量的等待队列中。
        当其他线程调用 signal() 或 broadcast() 方法发送信号到条件变量时，等待在条件变量上的线程会被唤醒。
        在被唤醒后，该线程会重新获取互斥锁，然后继续执行下去。
      
      实现：
        首先，函数通过调用 pthread_cond_wait() 函数来实现等待操作，该函数会使当前线程进入阻塞状态，并等待条件变量 m_cond 的信号。
        在调用 pthread_cond_wait() 之前，需要先对互斥锁 m_mutex 进行加锁操作，以保护条件变量的操作不受其他线程的影响。
        在函数返回之前，需要对互斥锁 m_mutex 进行解锁操作，以允许其他线程访问共享资源。
    */
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    // timewait 函数会在指定的时间内等待，如果时间到了仍然没有被唤醒，就会返回 false
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    // signal 函数用于唤醒一个正在等待条件变量的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // broadcast 函数用于唤醒所有正在等待条件变量的线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
