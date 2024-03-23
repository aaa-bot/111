#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"

using namespace std;

class Config //可以看作配置文件，通过解析命令行参数来设置server连接信息
{
public:
    Config();
    ~Config(){};

    //读取函数，通过读取命令行参数，根据读取到的信息设置配置文件参数值，具体见cpp
    void parse_arg(int argc, char*argv[]);


    //下面是配置文件包含的内容，即一些设置参数：

    //端口号
    int PORT;

    //日志写入方式
    int LOGWrite;

    //触发组合模式
    int TRIGMode;

    //listenfd触发模式
    int LISTENTrigmode;

    //connfd触发模式
    int CONNTrigmode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型选择
    int actor_model;
};

#endif