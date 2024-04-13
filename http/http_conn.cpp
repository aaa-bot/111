#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

void http_conn::initmysql_result(connection_pool *connPool)
{
    // 从连接池中获取一个连接，这里使用了 RAII（Resource Acquisition Is Initialization）技术
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    /*
        
        int mysql_query(MYSQL *mysql, const char *query);
        mysql 是一个指向已建立连接的 MySQL 对象的指针，用于指定要执行查询的连接。
        query 是一个字符串，表示要执行的 SQL 查询语句。
        该函数返回一个整数值，表示执行查询的结果。如果查询执行成功，则返回0；如果查询执行失败，则返回非0值，通常为一个错误代码。
    */
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        // 如果执行 SQL 查询出错，记录错误日志
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 将查询结果存储到 MYSQL_RES 结构体中
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))//循环读取每一行
    {
        string temp1(row[0]);//账户
        string temp2(row[1]);//密码
        users[temp1] = temp2;//存储在用户map中
    }
}

//对文件描述符设置非阻塞，与工具类中的setnonblocking相同
//(在非阻塞模式下，当读取或写入数据时，如果没有数据可用或者缓冲区已满，程序会立即返回，而不是等待数据或空间被释放)
//本文件最后解释了fcntl的作用
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);//获取套接字的文件描述符的当前状态标志
    int new_option = old_option | O_NONBLOCK;//按位或运算
    fcntl(fd, F_SETFL, new_option);//设置新的状态标志
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT。与工具类中的addfd相同，向epoll内核事件表中注册文件描述符（套接字）用来监听该套接字
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    //根据给定的触发模式TRIGMode设置事件的属性
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); //调用epoll_ctl把该事件注册到epoll内核事件表中
    setnonblocking(fd);
}

//从内核时间表删除描述符。与addfd执行相反操作
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT，修改事件属性，需要把要修改的套接字及其事件作为参数传进来
/*
    这段代码是用于修改 epoll 事件的函数。具体作用如下：

    参数 epollfd 是 epoll 描述符，用于标识 epoll 实例。
    参数 fd 是需要修改事件的文件描述符。
    参数 ev 是新的事件类型，包括读、写、异常等。
    参数 TRIGMode 是触发模式，如果为 1，表示使用边缘触发模式，否则为水平触发模式。
*/
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))//此判断表明这个套接字正在使用，链接没断开
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);//与Utils中的cb_func函数类似，先把要关闭的套接字从内核池中删掉，然后再关闭该套接字
        m_sockfd = -1;// 设置为-1的目的是说明该套接字已经不存在，链接被关闭
        m_user_count--;//用户数减一
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);//使用addfd把初始化的套接字注册到内核事件表中
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接，都设置为默认值
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    // 清空读缓冲区和写缓冲区，并且关闭MySQL连接等操作
    //memset：复制字符 c（一个无符号字符）到参数 str 所指向的字符串的前 n 个字符。
    //作用：是在一段内存块中填充某个给定的值，它是对较大的结构体或数组进行清零操作的一种最快方法
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
//对HTTP请求报文中每一行数据的解析，判断该行数据是否符合HTTP协议规范，是否完整且正确
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];//从读缓冲区中一行一行的读出来

        /*
        if:
        如果遇到 \r（回车符），则需要判断下一个字符是不是 \n（换行符）。
        如果下一个字符是 \n，表示这是一个完整的一行，于是将 \r 替换为 \0，\n 替换为 \0，表示将该行结束标记为字符串结束符。
        
        else if :
        如果下一个字符不是 \n，则表示这一行不符合 HTTP 报文的格式，返回 LINE_BAD 表示解析出错。
        如果遇到 \n（换行符），则同样需要判断前一个字符是不是 \r（回车符），如果是，同样将 \r 替换为 \0，\n 替换为 \0，表示将该行结束标记为字符串结束符。
        */
        if (temp == '\r')//此行为换行的话读取下一行
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//用于从套接字中读取数据到读缓冲区中。
bool http_conn::read_once()
{
    // 检查读缓冲区是否已满，如果已满则无法继续读取
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据，非阻塞一次性读取
    if (0 == m_TRIGMode)
    {
        //从套接字中读取数据，READ_BUFFER_SIZE - m_read_idx 为读取的数据大小
        //使用了 recv 函数从套接字 m_sockfd 中读取数据，并将读取的数据存储到 m_read_buf 缓冲区中。
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;//更新读取位置

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    //ET读数据，循环读取
    else
    {
        while (true)
        {
            //反复调用recv()函数尝试读取数据，直到recv()返回-1或者返回0，或者读取到的数据填满了读缓冲区
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                // 如果 errno 是 EAGAIN 或者 EWOULDBLOCK，则表示读取到末尾或者暂时无数据可读，跳出循环
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                // 如果返回值为 0，则表示对端关闭了连接，跳出循环
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
//请求行的数据格式： GET /index.html HTTP/1.1\r\n
/*
GET为请求方式，请求方式分为：Get（默认）、POST、DELETE、HEAD等
GET：明文传输 不安全，数据量有限，不超过1kb
POST：暗文传输，安全。数据量没有限制。
/test/test.html为URI，统一资源标识符
HTTP/1.1为协议版本
*/
// 这个函数是用来解析 HTTP 请求行的
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //m_url  /index.html
    // 在请求行中找到第一个出现空格或制表符的位置，将其作为 URL 的起始位置
    m_url = strpbrk(text, " \t");  //strpbrk()函数在请求行中找到第一个出现空格或制表符的位置，将其作为URL的起始位置 m_url
    // 如果没有找到空格或制表符，返回 BAD_REQUEST 错误
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // 将 URL 的起始位置设置为字符串结束符 '\0'，并将指针移动到 URL 的实际内容
    *m_url++ = '\0';

     // 提取请求方法并进行比较，判断是 GET 还是 POST
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        // 如果是 POST 方法，设置标志位 cgi 为 1，表示需要处理 CGI 请求
        cgi = 1;
    }
    else
        return BAD_REQUEST;

    // 跳过 URL 中的空格或制表符，将指针移动到 URL 的实际内容
    m_url += strspn(m_url, " \t");//strspn() 函数跳过 URL 中的空格或制表符
    // 在 URL 中找到第一个出现空格或制表符的位置，将其作为 HTTP 版本的起始位置
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;

    //用字符串比较函数获取http版本
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    // 检查 HTTP 版本是否为 "HTTP/1.1"，如果不是则返回 BAD_REQUEST 错误
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 检查 URL 是否以 "http://" 或 "https://" 开头，如果是，则将指针移动到路径部分
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    //设置解析状态为CHECK_STATE_HEADER，表示请求行解析完毕，需要进一步解析HTTP头部
    m_check_state = CHECK_STATE_HEADER;
    // 返回 NO_REQUEST，表示解析请求行成功
    return NO_REQUEST;
}

//解析http请求的一个头部信息
// 该函数接收一个字符串指针text，该指针指向 HTTP 报文头部的第一行（即请求行后面的第一行）
/*
GET /index.html HTTP/1.1   请求 www.example.com 的 /index.html 页面。请求头部分包含了一些关于浏览器和所需数据类型的信息
Host: www.example.com
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)
Accept: text/html,application/xhtml+xml,application/xml;
Accept - Language: en - US, en; q = 0.5
Connection: keep - alive
*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')//判断第一行是否为空（头部是可以为空的）
    {
        // 如果为空则判断请求报文中是否携带了消息体，如果有则将状态切换到解析消息体
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //如果该行不为空，则根据首部字段的名称进行解析，首先判断是否为 Connection 首部字段，如果是则取出其值
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            //判断是否为 "keep-alive"，如果是则将m_linger设置为true，表示连接保持活跃
            m_linger = true;
        }
    }
    //判断是否为 Content-length 字段，如果是则取出消息体的长度
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    //如果是Host字段，则将其值保存在类成员变量m_host中
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
/*
    这个函数是用来解析 HTTP 请求消息体的内容的。
    在 HTTP 协议中，如果是 POST 请求，请求消息体中通常会包含客户端发送的数据，比如表单提交时用户填写的信息。
    这个函数的作用是判断是否已经接收到了完整的请求消息体，并将消息体内容保存到 m_string 成员变量中。

*/
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;// 表示当前解析出来的行状态
    HTTP_CODE ret = NO_REQUEST;// 表示当前解析出来的方法执行结果
    char *text = 0;// 定义一个指向当前读取行缓冲区的指针text初始化为0

    //可以进行处理的两种条件：
    //1、解析状态为解析请求消息体，且行解析状态正常
    //2、解析方式为一行行解析，且该行解析正确
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();//一行行的进行读取
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE://解析请求行
        {
            //具体解析http请求行的函数
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            //具体解析请求头部的函数
            ret = parse_headers(text);//解析请求头部
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT://解析消息体
        {
            //具体解析消息体的函数
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}


//根据URL中的内容进行相关处理，并返回具体的错误码或成功标识
http_conn::HTTP_CODE http_conn::do_request()
{
    //1、确定实际请求的文件路径
    /*
    服务器的根目录 doc_root 复制到 m_real_file 中
    计算根目录的长度
    根据请求的 URL 寻找文件名（p指向URL中最后一个斜杠），确定实际请求的文件路径
    */
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    //2、如果是CGI请求，则解析出用户名和密码，并根据请求的不同类型（注册、登录）进行相应的处理
    //详细见文件结尾注释部分
    //p指向URL中最后一个斜杠
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())//没找到重名则插入
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    //不是cgi请求，即静态页面请求
    if (*(p + 1) == '0')//注册页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')//登录页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')//图片页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')//视频页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')//粉丝页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //通过调用stat()函数获取m_real_file所指向的文件状态，并对文件的读取权限进行判断

    if (stat(m_real_file, &m_file_stat) < 0)// 如果stat()操作失败，则返回NO_RESOURCE错误代码
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))// 如果该文件不可读，则返回FORBIDDEN_REQUEST错误码
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))// 如果该文件为目录，则返回BAD_REQUEST错误码
        return BAD_REQUEST;

    //都通过后 函数会通过open()函数打开该文件，并通过mmap()函数将文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    //然后关闭套接字,此时不再需要进行通信，而是直接可以通过内存访问
    /*
    关闭文件描述符的目的是确保在文件内容映射到内存后，不再需要通过文件描述符来操作文件
    这样可以释放文件描述符资源，避免资源泄漏，并且不影响后续对内存映射区域的访问。
    因为一旦文件映射到了内存中，后续的访问将直接通过内存访问，而不再需要通过文件描述符。
    */
    close(fd);
    return FILE_REQUEST;//返回 FILE_REQUEST 表示请求处理成功，可以进行文件传输,以便后续对文件内容的处理
}

//与mmap执行相反的操作，即文件内容与内存进行解映射
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//用于向客户端发送HTTP响应报文，write() 方法实现了向客户端发送HTTP响应报文的功能
//采用了多次写入以及事件类型修改等措施，以确保能够将数据完整地发送出去
bool http_conn::write()
{
    int temp = 0;

    // 如果没有待发送的数据
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);//修改文件描述符的事件类型为 EPOLLIN，即监听读事件
        init();//重置HTTP连接对象
        return true;// 返回 true 表示写入成功
    }

    // 进入循环，持续写入数据
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);//将数据从多个缓冲区中写入到套接字中

        if (temp < 0)//如果写入失败
        {
            if (errno == EAGAIN)//表示当前套接字的发送缓冲区已满，无法立即发送数据，需要等待下一次写事件的触发，以便继续发送剩余的数据
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);//表示当前套接字不可写，这时候调用 modfd 函数修改文件描述符的事件类型为 EPOLLOUT，即监听写事件
                return true;// 返回 true 表示等待下次写事件触发后继续写入
            }
            unmap();//解映射
            return false;// 返回 false 表示写入失败
        }

        bytes_have_send += temp;// 更新已发送字节数
        bytes_to_send -= temp;// 更新待发送字节数
        if (bytes_have_send >= m_iv[0].iov_len)// 如果已发送字节数大于等于当前缓冲区的长度
        {
            m_iv[0].iov_len = 0;// 将当前缓冲区长度置零
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);//更新第二个缓冲区的起始位置
            m_iv[1].iov_len = bytes_to_send;// 更新第二个缓冲区的长度
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;//更新当前缓冲区的起始位置
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;//更新当前缓冲区的长度
        }

        if (bytes_to_send <= 0)//全部发送完毕
        {
            unmap();//解映射
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);//全部写完之后继续监听读事件，等待请求接入

            if (m_linger)//保持链接，即还存在着链接
            {
                init();//重设链接对象，用该链接去接入其他请求对象
                return true;
            }
            else//不保持则返回false直接关闭链接
            {
                return false;
            }
        }
    }
}

//用于向响应报文中添加不同的内容信息，以构造完整的HTTP响应
//参数可变，format为格式化字符串
//格式化字符串是指包含了格式占位符的字符串，这些占位符可以在运行时被具体的值替换。
//在C和C++中，常见的格式化字符串是使用类似于printf和sprintf函数中的格式化控制符的字符串，例如：%d表示一个整数，%s表示一个字符串，%f表示一个浮点数等等
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)//超过了写数据的缓存大小
        return false;

    va_list arg_list;
    va_start(arg_list, format);
    //使用了vsnprintf函数而不是sprintf，是为了避免缓冲区溢出的风险
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;//写入的字符数超出了缓冲区剩余的空间
    }

    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

//下面的add函数，都是向写入的数据中添加内容
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

//用于根据不同的 HTTP 返回码（HTTP_CODE ret）生成对应的 HTTP 响应
bool http_conn::process_write(HTTP_CODE ret)
{
    //根据HTTP_CODE调用不同的添加内容的函数（add_）进行数据写入
    switch (ret)
    {
    case INTERNAL_ERROR:// 服务器内部错误
    {
        // 添加状态行、头部和内容
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:// 客户端请求错误
    {
        // 添加状态行、头部和内容
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:// 客户端请求被禁止
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:// 请求文件
    {
        add_status_line(200, ok_200_title);// 添加状态行
        if (m_file_stat.st_size != 0)// 如果文件大小不为0
        {
            add_headers(m_file_stat.st_size);// 添加头部
            // 设置 IO 向量（m_iv），用于描述待发送数据的位置和长度
            m_iv[0].iov_base = m_write_buf;// 第一个元素设置为写缓冲区的位置和长度
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address; // 第二个元素设置为文件内容的位置和长度
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;// IO 向量的数量
            bytes_to_send = m_write_idx + m_file_stat.st_size; // 待发送的字节数
            return true;// 返回 true 表示可以继续发送数据
        }
        else // 如果文件大小为0
        {
            const char *ok_string = "<html><body></body></html>";// 空文件内容
            add_headers(strlen(ok_string));//添加头部
            if (!add_content(ok_string))//添加内容
                return false;
        }
    }
    default:
        return false;
    }

    //根据实际写入的内容设置IO向量 m_iv
    /*
    在处理写操作时，m_iv 是一个iovec结构体数组（见文件末尾注释），用于描述待发送数据的位置和长度。
    根据不同的情况，m_iv 被设置为不同的值，以便在调用writev()函数时正确地发送数据。
    
    具体地说：
    当处理 FILE_REQUEST 时，如果文件大小不为0，则需要发送文件内容给客户端。
    因此，m_iv 的第一个元素设置为写缓冲区的位置和长度，第二个元素设置为文件内容的位置和长度。
    这样，在调用writev()函数时，就可以一次性发送写缓冲区中的内容和文件内容给客户端。
    这种做法可以利用操作系统的零拷贝特性，减少数据在内核和用户空间之间的复制次数，提高网络传输效率。
    */
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1; 
    bytes_to_send = m_write_idx;
    return true;
}

//处理HTTP请求，根据http码判断读写操作，进而调用process_read或者process_write函数
void http_conn::process()
{
    // 调用 process_read 函数处理读取请求的逻辑
    HTTP_CODE read_ret = process_read();
    // 如果读取请求返回 NO_REQUEST，表示没有完整的 HTTP 请求，需要继续等待数据到达
    if (read_ret == NO_REQUEST)
    {
        // 将套接字注册到 epoll 实例中，监听 EPOLLIN 事件，以便下次继续读取数据
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    // 调用 process_write 函数处理写入响应的逻辑
    bool write_ret = process_write(read_ret);
    // 如果写入响应失败，则关闭连接
    if (!write_ret)
    {
        close_conn();
    }
    // 将套接字注册到 epoll 实例中，监听 EPOLLOUT 事件，以便下次继续写入响应
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

//注释信息
/*
-----------------------fcntl----------------------------------
fcntl(fd, F_GETFL) 是一个系统调用，用于获取文件描述符 fd 的状态标志（即文件描述符的文件状态标志）

Linux：
    在 Linux 系统中，fcntl() 函数用于对文件描述符进行各种操作，包括获取和设置文件状态标志。
    F_GETFL 参数指示 fcntl() 函数进行获取操作，获取文件描述符的状态标志。
    fcntl() 函数会将文件描述符 fd 对应的状态标志返回给调用者。

Windows：
    在 Windows 系统中，fcntl() 函数的功能由一系列 Win32 API 函数来实现，如 GetFileInformationByHandle() 等。
    通过相应的 Win32 API 函数，可以获取文件句柄（即文件描述符）的属性信息，其中包括文件状态标志。
   
复制文件描述符（F_DUPFD）
获取和设置文件状态标志（F_GETFL、F_SETFL）
获取和设置文件记录锁（F_GETLK、F_SETLK、F_SETLKW）


------------------------cgi---------------------------------
CGI（Common Gateway Interface）是一种通用网关接口，用于在Web服务器与外部程序之间传递数据。
通过CGI，Web服务器可以调用外部程序处理客户端发送的请求，并将处理结果返回给客户端。
根据请求的URL中的特定标识（如 '2'、'3'），判断是否需要进行CGI处理。
如果是CGI请求，则需要解析出用户名和密码，并根据具体的业务逻辑进行处理，比如注册、登录等操作。
然后将处理结果返回给客户端。

------------------------iovec---------------------------------

iovec 结构体数组（又称为 I/O 向量）用于在进行 I/O 操作时指定多个缓冲区的位置和长度。
每个 iovec 结构体描述了一个缓冲区的地址和长度，这样可以在一次 I/O 操作中传输多个不连续的数据块，而无需将它们合并到一个连续的内存块中。

iovec 结构体的定义通常如下所示：

struct iovec {
    void  *iov_base; // 缓冲区的起始地址
    size_t iov_len;  // 缓冲区的长度
};
iovec 结构体数组则是由多个iovec结构体组成的数组
每个iovec结构体描述了一个待发送或待接收的缓冲区
在进行 I/O 操作时，可以指定一个iovec结构体数组，其中每个iovec元素描述了一个要传输的数据块。

使用iovec结构体数组可以实现高效的I/O操作，特别是在需要传输多个不连续数据块的情况下
例如在发送文件内容时，可以一次性传输多个文件块，而无需将它们合并到一个连续的内存块中。

*/