#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;
    // 创建了指定数量的数据库连接，并将连接添加到connList列表中
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;//创建数据库指针
		con = mysql_init(con);//数据库的init库函数

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);// 如果连接创建失败，则会输出错误信息并立即退出程序
		}
        //建立数据库链接
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;//m_FreeConn表示当前空闲的连接数
	}

	reserve = sem(m_FreeConn);//以当前空闲的连接数作为参数设置信号量，实现线程同步与互斥操作

	m_MaxConn = m_FreeConn;
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())//如果链接列表为空，表示没有可用的链接，返回空
		return NULL;

	reserve.wait(); //信号量进行等待操作，如果信号量为0时会阻塞，也就是不会进行下面的分配操作，直到等待信号量的值大于0
	
	lock.lock();//在给请求分配链接时进行上锁，个人理解是防止多个线程同时请求的话会存在一个链接给多个请求使用的情况

    //弹出列表中第一个链接给这个请求使用
	con = connList.front();
	connList.pop_front();

	--m_FreeConn;//空闲连接数-1
	++m_CurConn;

	lock.unlock();//解锁
	return con;
}

//释放当前使用的连接，与GetConnection做的是相反的操作
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();//有链接释放了代表某线程结束，触发信号量的post操作，使得信号量的值增加
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
    //先上锁
	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);//关闭对应的数据库
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
    //解锁
	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();//析构函数，直接调用该函数对数据库连接池进行销毁操作
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection(); //将从连接池中获取到的一个数据库连接赋值给SQL指向的内存空间
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}