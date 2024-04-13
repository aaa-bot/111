#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径，设置根目录路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器,与客户端相对应，每个用户都有一个定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    //关闭套接字（可以理解为通信结束，退出登录），删除用户、定时器以及线程池，防止内存泄漏
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;//端口号
    m_user = user;//用户名
    m_passWord = passWord;//密码
    m_databaseName = databaseName;//数据库名字
    m_sql_num = sql_num;//数据库连接池数量
    m_thread_num = thread_num;//线程池数量
    m_log_write = log_write;//日志写入方式，0默认同步
    m_OPT_LINGER = opt_linger;//优雅关闭连接，0默认不使用
    m_TRIGMode = trigmode;//listenfd和connfd的模式组合，0默认使用LT + LT
    m_close_log = close_log;//关闭日志，0默认不关闭
    m_actormodel = actor_model;//并发模式，0默认proactor
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志，日志使用单例模式，Log::get_instance()拿到唯一的日志对象并进行初始化
        if (1 == m_log_write)
            //异步包括初始化一个阻塞队列，并创建日志文件并打开
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);//异步写入
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);//同步写入
    }
}

void WebServer::sql_pool()
{
    // 初始化数据库连接池，获取 connection_pool 的单例实例
    m_connPool = connection_pool::GetInstance();
    // 初始化数据库连接池，传入连接信息和其他参数，分配一定数量的连接
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 使用数据库连接池中的连接执行数据库查询，初始化用户信息，记录用户名密码对应关系
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //threadpool<http_conn>: 这是一个模板类，用于创建一个线程池，模板参数 http_conn 可能指定了线程池中任务的类型。
    //m_actormodel：并发模型选择
    //m_connPool：这是一个指向数据库连接池的指针，用于线程池中的任务需要访问数据库时获取数据库连接。
    //m_thread_num：线程池中的线程数。
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}


//brief 创建套接字、绑定端口、监听连接请求，并初始化epoll内核事件表
void WebServer::eventListen()
{
    //网络编程基础步骤（通用且必须）,创建套接字
    //PF_INET  代表了 IPv4 网络协议族。在网络编程中，常用的协议族有 PF_INET（IPv4）和 PF_INET6（IPv6）等
    //SOCK_STREAM  代表了面向连接的、可靠的、基于字节流的传输方式
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);

    //函数assert()用于断言m_listenfd大于等于0，如果不满足条件，即创建套接字失败，则程序会终止执行,
    assert(m_listenfd >= 0);

    //优雅关闭连接，此 if else语句是设置连接选项，m_OPT_LINGER为0则延迟关闭，等数据发送完毕；否则立即关闭
    
    //linger结构体   linger{l_onoff, l_linger}
    //l_onoff：一个整数值，用于指示是否启用了延迟关闭选项。如果 l_onoff 的值为非零，则表示启用延迟关闭；如果为零，则表示禁用延迟关闭。
    //l_linger：一个整数值，指定了延迟关闭的时间（以秒为单位）
    
    /*setsockopt函数原型
    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
        sockfd：需要设置选项的套接字文件描述符，即m_listenfd
        level：选项所在的协议层或者套接字类型。常见的有 SOL_SOCKET、IPPROTO_TCP、IPPROTO_IP 等。
        SOL_SOCKET 用于设置套接字级别的选项，而 IPPROTO_TCP 和 IPPROTO_IP 分别用于 TCP 和 IP 协议的选项。
        optname：要设置的选项名。
        optval：指向包含新选项值的缓冲区的指针。
        optlen：optval 缓冲区的大小。
    */
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    /*sockaddr_in 结构体，通常与其他函数一起使用，例如 bind()、connect()、accept() 等函数，用于指定地址和端口信息
    sin_family：一个整数值，指定地址族，通常设置为 AF_INET，表示 IPv4 地址。
    sin_port：一个无符号短整型值，表示端口号。使用 htons() 函数进行字节序转换，将主机字节序转换为网络字节序（大端字节序）。
    sin_addr：一个 in_addr 结构体类型的变量，用于表示 IP 地址。in_addr 结构体包含一个无符号 32 位整数，表示 IPv4 地址。
    sin_zero：一个填充字段，用于填充结构体大小，以使得 sockaddr_in 结构体的大小与 sockaddr 结构体的大小相同。
    */
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));//将指定内存区域的内容全部置零
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    //允许地址重用。这样做可以避免因为网络延迟导致的 TIME_WAIT 状态而无法立即重启服务器
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    //绑定监听套接字到服务器地址，使用 bind() 函数。
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    //开始监听连接请求，使用 listen() 函数。
    ret = listen(m_listenfd, 5);// 参数 5 表示待处理连接队列的最大长度，操作系统内核为相应的套接字设置的等待连接的最大数量
    assert(ret >= 0);

    //---------------以上全是在设置socket通信基础信息----------------------

    utils.init(TIMESLOT);//设置定时器

    //epoll创建内核事件表,用来监听连接事件
    epoll_event events[MAX_EVENT_NUMBER];

    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //将监听套接字添加到 epoll 事件表中，以便监听连接请求
    //通俗点讲，epoll是一个监听器，listenfd是一个通讯设备，此处就是把要监听的设备添加倒监听器中
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);//传入false表示设置为阻塞模式
    http_conn::m_epollfd = m_epollfd;//让 http_conn 类的其他成员函数或者静态函数能够使用这个 epoll 文件描述符，以进行相关的事件监听和处理工作


    //创建管道，用于进程间通信。
    //使用 socketpair() 函数创建一对 UNIX 域套接字，并将其中一个套接字添加到 epoll 事件表中，以便监听管道上的事件
    /*socketpair() 是一个用于创建一对连接的 UNIX 域套接字的系统调用。它允许在同一台计算机上的两个进程之间进行通信，这两个进程之间的通信是双向的。
    int socketpair(int domain, int type, int protocol, int sv[2]);

    domain：指定套接字的协议族，通常设置为 PF_UNIX 或者 PF_LOCAL，表示 UNIX 域套接字。在某些系统中，还可以设置为 PF_INET，表示将使用网络套接字，但在实际应用中常用于本地通信。
    type：指定套接字的类型，通常设置为 SOCK_STREAM（流式套接字）或者 SOCK_DGRAM（数据报套接字）。SOCK_STREAM 类型的套接字提供面向连接的、可靠的、双向的字节流通信，而 SOCK_DGRAM 类型的套接字提供无连接的、不可靠的数据报通信。
    protocol：指定套接字所使用的协议，通常设置为 0，表示由操作系统自动选择合适的协议。
    sv[2]：一个整型数组，用于存放创建的套接字的文件描述符。创建成功后，sv[0] 存放一端的文件描述符，sv[1] 存放另一端的文件描述符
    */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);


    utils.setnonblocking(m_pipefd[1]);//将m_pipefd[1]设置为非阻塞模式。该函数使用fcntl()系统调用设置文件描述符的状态为非阻塞模式，并返回原先的文件描述符状态。
    utils.addfd(m_epollfd, m_pipefd[0], false, 0); //将m_pipefd[0]添加到epoll事件监听器中

    //注册一个信号处理函数，当指定的信号被触发时，将调用相应的处理函数进行处理。
    //如果设置了 restart 参数为 true，则表示在信号处理函数执行完毕后自动重启被中断的系统调用
    utils.addsig(SIGPIPE, SIG_IGN);//进程通信管道信号。当向已关闭写端的管道（或者 FIFO）中写入数据时，会触发 SIGPIPE 信号。
    //默认情况下，进程会收到 SIGPIPE 信号而终止，因此，如果不希望进程因为 SIGPIPE 信号而终止，可以忽略该信号（使用 SIG_IGN）或者自定义处理函数。
    
    utils.addsig(SIGALRM, utils.sig_handler, false);//定时器到期信号的处理
    utils.addsig(SIGTERM, utils.sig_handler, false);//终止信号的处理

    alarm(TIMESLOT);//超时

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //初始化用户数据，使用连接套接字connfd和客户端地址client_address初始化一个User对象，并将其保存在全局数组users中，以便后续使用
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    // client_data 是一个结构体。结构体中包含了 地址、套接字号以及util_timer类，其中util_timer中封装了 定时器的超时时间和超时要执行的回调函数
    // 具体见util_time的类声明
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;//新定义一个定时器
    timer->user_data = &users_timer[connfd];//设置定时器的用户数据
    timer->cb_func = cb_func;//回调函数为cb_func，即当超时后程序为执行cb_func函数
    time_t cur = time(NULL);//记录当前时间
    timer->expire = cur + 3 * TIMESLOT;//设置超时时间为三个间隙
    users_timer[connfd].timer = timer;//把这个定时器作为成员变量塞到用户数据结构中
    utils.m_timer_lst.add_timer(timer);//把这个定时器插入到存储的链表中
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);//此处是为了维持定时器链表“先来后到”的准则，快到时间的理应在链表前面，即保证时间顺序

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    /*cb_func函数体
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);//定时器超时则从监听器中删除该套接字，即不再监听
    assert(user_data);//主要是为了空指针检查
    close(user_data->sockfd);//关闭用户的套接字，可以理解为推出登录，释放资源
    http_conn::m_user_count--;
    */
    if (timer)//关闭了套接字（退出登录）后用户数量就减少，该用户对应的定时器如果还存在也要相应删除
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//处理客户端数据，在没有数据到达时，该函数会一直阻塞等待客户端连接
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;//用于存储客户端的 IPv4 地址信息
    socklen_t client_addrlength = sizeof(client_address);//计算大小
    if (0 == m_LISTENTrigmode)//m_LISTENTrigmode是监听socket的触发模式，0表示LT模式
    {
        //接受客户端的连接，传入参数m_listenfd作为“监听套接字”，与bind相对应，accept会创建一个新的套接字（“连接套接字”）在服务器端使用
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        //连接失败
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        //超过了最大连接数
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        //接受连接，并开启一个定时器，并将套接字和客户端地址作为参数传入
        //注意：传入的是accept生成的“连接套接字”
        timer(connfd, client_address);
    }

    else
    {
        //使用while循环接受所有连接请求，内部实现同上
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    //从管道的读端 m_pipefd[0] 接收信号，并将其存储在 signals 数组中
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        //遍历字符数组并根据不同的信号类型执行相应的操作
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;//超时
                break;
            }
            case SIGTERM:
            {
                stop_server = true;//停止服务器
                break;
            }
            }
        }
    }
    return true;
}

//用于处理读事件的函数。当服务器监测到套接字上有可读事件时，该函数将被调用以处理数据
void WebServer::dealwithread(int sockfd)
{
    //拿到套接字（对应用户）的定时器
    util_timer *timer = users_timer[sockfd].timer;

    // m_actormodel为服务器的模式，根据m_actormodel的值执行不同的操作，模式不同决定了下面的操作不同
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)//调整定时器
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            //使用while循环等待其他线程处理请求并更新用户状态，直到发现用户状态已经被改变为“improv = 1”才退出循环
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)//超时
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())//只读取一次，但是需要直到把客户端数据读完或关闭连接
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//用于处理写事件的函数（与读函数实现相同）。当服务器检测到套接字上有可写事件时，该函数将被调用以向客户端发送数据
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//循环读取时间的函数，是服务器处理客户端连接请求和数据的主函数
void WebServer::eventLoop()
{
    bool timeout = false;//是否超时
    bool stop_server = false;//是否停止服务器

    while (!stop_server)//只要服务器没停，就持续处理监听器中监听到的事件
    {
        // 调用 epoll_wait 函数等待事件就绪
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // epoll_wait 返回值小于 0，且错误码不是中断错误 EINTR
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        //遍历处理就绪事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //如果处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                //处理新连接
                bool flag = dealclientdata();//监听到了新用户，用accept函数接受连接
                if (false == flag)
                    continue;
            }
            //否则继续处理监听器事件池中监听到的事件

            //如果事件类型是客户端断开、服务端关闭连接或出现错误，则移除对应的定时器
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //如果事件类型是信号，则调用 dealwithsignal 函数进行处理
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据（读），即客户端发来数据，进行处理
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            //处理客户连接上接收到的数据（写），即给客户端发送数据
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        // 如果timeout变量为 true，则表明当前轮询已经超时，需要处理定时器事
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
