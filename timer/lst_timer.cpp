#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    //把定时器链表节点一个个删掉
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)//定时器为空
    {
        return;
    }
    if (!head)//链表头节点为空
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)//定时器的预期时间小于头节点的预期时间，则插入链表头部，保证时间顺序从小到大
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}
void sort_timer_lst::adjust_timer(util_timer *timer)//调整定时器链表
{
    if (!timer) // 如果timer指针为空，则直接返回
    {
        return;
    }
    // 检查当前定时器timer的下一个节点tmp是否存在，且是否需要将timer节点调整到tmp前面
    util_timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire))
    {
        // 如果不需要调整(即符合事件顺序)，则直接返回
        return;
    }
    // 如果timer是链表的头节点head，则需要将head指针指向timer的下一个节点，并将timer从链表中移除
    if (timer == head)
    {
        //链表操作，相当于把一个节点拿到最前面
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        // 如果timer不是头节点，则需要将timer从链表中移除
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        //将定时器重新插入到正确的位置上
        add_timer(timer, timer->next);
    }
}
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if ((timer == head) && (timer == tail))
    {
        //删除之后链表空了
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)
    {
        //头节点删除之后更新定时器链表头节点
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    //在尾部
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    //在中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
void sort_timer_lst::tick()//定时器响应函数，处理定时事件
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);
    util_timer *tmp = head;
    //遍历整个定时器链表，查找有没有超时的定时器，并执行相应的回调函数
    while (tmp)
    {
        if (cur < tmp->expire)//当前的时间还没到预定时间
        {
            break;
        }
        tmp->cb_func(tmp->user_data);//当前的到了预定时间，调用超时函数
        head = tmp->next;//更改定时器链表的头节点
        if (head)
        {
            head->prev = NULL;//前一个节点置空，也就是删除上一个超时的定时器
        }
        delete tmp;
        tmp = head;
    }
}

//用于将指定定时器插入到链表中的合适位置，以保持链表有序
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    // 定义了两个指针prev和tmp，分别指向链表头结点和下一个节点
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    //遍历链表找合适位置
    while (tmp)
    {
        // 判断新加入的定时器的超时时间expire是否小于当前节点tmp的超时时间
        if (timer->expire < tmp->expire)
        {
            //插入操作
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)//如果while循环执行完毕后仍然没有找到合适的位置插入新定时器，则新定时器应该插入到链表尾部
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞，与http_conn.cpp中的定义相同
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT，与http_conn.cpp中的定义相同
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    /*
    可重入性（Reentrancy）是指一个函数在被多个线程或上下文重复调用时，能够正确地处理多个实例之间的数据共享和竞争条件，
    而不会导致意外结果或错误行为。在多线程或异步编程环境中，如果一个函数是可重入的，那么多个线程或上下文可以同时调用该函数，而不需要额外的同步措施来保证其正确性。
    */
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);//将信号发送给管道
    errno = save_errno;
}

//设置信号函数，给一些信号设置相应的回调函数
/*
    sig：要注册的信号的编号。
    handler：信号处理函数的指针，即要执行的操作。
    restart：一个布尔值，表示在信号处理函数执行期间是否重新启动被中断的系统调用
*/
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;//sigaction 结构体变量 sa 用于设置信号处理的相关属性
    memset(&sa, '\0', sizeof(sa)); //将sa初始化为0。
    sa.sa_handler = handler;//信号处理函数指针赋值给sa.sa_handler，即将handler设置为要执行的信号处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART; //在信号处理函数执行期间重新启动被中断的系统调用

    sigfillset(&sa.sa_mask); //将所有信号添加到信号屏蔽字中,都设置为阻塞状态，防止在信号处理函数执行期间发生干扰，即不会再收到其他信号
    assert(sigaction(sig, &sa, NULL) != -1);//sigaction(sig, &sa, NULL)  注册信号处理函数，失败会触发assert
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    //发送错误信息
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
//定时器发生超时时执行
void cb_func(client_data *user_data)
{
    //需要先移除监听器中的套接字，再关闭套接字，防止直接关闭套接字后无法删除监听器中的套接字，（类似避免内存泄露）
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);//定时器超时则从监听器中删除该套接字，即不再监听
    assert(user_data);//主要是为了空指针检查
    close(user_data->sockfd);//关闭用户的套接字，可以理解为推出登录，释放资源
    http_conn::m_user_count--;
}
