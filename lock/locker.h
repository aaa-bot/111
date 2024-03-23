#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//brief
//�ṩ�˶� POSIX �߳̿����ź����������������������ķ�װ��ʹ���ڶ��̱߳���и������ʹ����Щͬ������
//������ֱ�ӵ��õײ�ӿڿ��ܳ��ֵĴ���
/*
1�������������������Ĺ�ϵ��
������ (locker) ���������� (cond) ����һ��ʹ�ã�����ʵ�ָ��ӵ�ͬ�����ơ�
ͨ������£���ʹ����������ʱ��Ҫ��ϻ�����ʹ�ã��Ա�֤�Թ�����Դ�ķ����ǻ���ġ�
�������� (cond) ����ͨ���ȴ��ͻ��ѻ�����ʵ���̼߳��ͨ�ţ���������Դ��״̬�����仯ʱ���߳̿���ͨ�����������ȴ����߱�������������Ӧ�Ĳ�����

2���ź����ͻ������Ĺ�ϵ��
�ź��� (sem) �ͻ����� (locker) ��������ʵ���߳�ͬ���ͻ�������Ļ��ƣ����ڹ��ܺ�ʹ�ó��������в�ͬ��
��������Ҫ����ʵ�ֶԹ�����Դ�Ļ�����ʣ�ȷ��ͬһʱ��ֻ��һ���߳̿��Է��ʹ�����Դ���Ӷ����⾺̬������
�ź�����������ڸ��㷺�ĳ���������ʵ�ֻ�������⣬������ʵ���̼߳��ͬ�����̼߳��ͨ�ŵȣ�����ͨ���ź�������ʵ��������-������ģ�͡������߳������ȹ��ܡ�
*/


//�ź��� ��
/*
�ź�����һ��ͬ�����ƣ����ڿ��ƶ���߳�֮��ķ���˳��
ͨ������£�������߳���Ҫ����һ����Դʱ����Ҫʹ���ź�����ȷ��ÿ���̰߳���һ����˳����ʸ���Դ���Ӷ�������־������������ݲ�һ�µ�����
*/
class sem
{
public:
    /*
    //sem���캯���е��õ�sem_init��һ�����ڳ�ʼ���ź����Ŀ⺯��
    sem��ָ��Ҫ��ʼ�����ź�����ָ�롣
    pshared��ָ���ź������ڽ��̼乲�������̼߳乲��ı�־�����psharedΪ0�����ʾ�ź�������ͬһ�����ڵ��߳�֮�乲�����psharedΪ��0ֵ�����ʾ�ź��������ڶ������֮�乲��
    value��ָ���ź����ĳ�ʼֵ��
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
        sem_destroy(&m_sem);//�����ź���
    }

    // �÷�������ź������еȴ�����������ź�����ֵΪ0����÷�����������ǰ�̣߳�ֱ���������߳�ͨ�� post() �������ź�����ֵ����������0���Żỽ�Ѹ��߳�
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // �÷�������ź��������ͷŲ��������ź�����ֵ����1�����ѿ��ܱ��������߳�
    // �����߳̽������ͷŲ�����ʹ���ź������У���ʱ���������߳̿���ʹ�ø��ź������������ѣ�
    bool post()
    {

        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};


//������ ��
/*
�ź�����һ��ͬ�����ƣ����ڿ��ƶ���߳�֮��ķ���˳��ͨ������£�������߳���Ҫ����һ����Դʱ����Ҫʹ���ź�����ȷ��ÿ���̰߳���һ����˳����ʸ���Դ���Ӷ�������־������������ݲ�һ�µ�����
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

    //lock() �������� pthread_mutex_lock() �����Ի��������м���������
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    //unlock() �������� pthread_mutex_unlock() �����Ի��������н���������
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    //get() �������ػ�������ָ�롣
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


//�������� ��
/*
�ź�����һ��ͬ�����ƣ����ڿ��ƶ���߳�֮��ķ���˳��ͨ������£�������߳���Ҫ����һ����Դʱ����Ҫʹ���ź�����ȷ��ÿ���̰߳���һ����˳����ʸ���Դ���Ӷ�������־������������ݲ�һ�µ�����
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

    // wait ������һֱ�ȴ���ֱ���������̻߳���
    /*�˴���ϸ����wait������ʵ���빦�ܣ�timewait����ͬ��
      ���ܣ�  
        ��һ���̵߳��� wait() ����ʱ�����Ὣ�Լ����������ͷŵ��������еĻ�������Ȼ��ȴ������������źš�
        �ڵȴ��ڼ䣬���̻߳ᱻ�������������ĵȴ������С�
        �������̵߳��� signal() �� broadcast() ���������źŵ���������ʱ���ȴ������������ϵ��̻߳ᱻ���ѡ�
        �ڱ����Ѻ󣬸��̻߳����»�ȡ��������Ȼ�����ִ����ȥ��
      
      ʵ�֣�
        ���ȣ�����ͨ������ pthread_cond_wait() ������ʵ�ֵȴ��������ú�����ʹ��ǰ�߳̽�������״̬�����ȴ��������� m_cond ���źš�
        �ڵ��� pthread_cond_wait() ֮ǰ����Ҫ�ȶԻ����� m_mutex ���м����������Ա������������Ĳ������������̵߳�Ӱ�졣
        �ں�������֮ǰ����Ҫ�Ի����� m_mutex ���н��������������������̷߳��ʹ�����Դ��
    */
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    // timewait ��������ָ����ʱ���ڵȴ������ʱ�䵽����Ȼû�б����ѣ��ͻ᷵�� false
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    // signal �������ڻ���һ�����ڵȴ������������߳�
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // broadcast �������ڻ����������ڵȴ������������߳�
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
