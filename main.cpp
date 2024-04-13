#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "qgydb";

    //命令行解析
    Config config; //配置文件
    config.parse_arg(argc, argv);//通过读取命令行参数信息来设置serve的方式

    WebServer server; //创建服务器对象

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //日志，开启日志记录
    /*
        同步：当日志记录器接收到一条日志消息时，它会立即将该消息写入到日志文件中，并在写入完成后返回控制权给调用方。
        异步：异步需要创建一个阻塞队列。当日志记录器接收到一条日志消息时，它不会立即将消息写入到日志文件中，而是将消息放入一个队列或缓冲区中。
    */
   //log_write()作用是区分同步异步并初始化，初始化不同点是异步要初始化一个阻塞队列。
    server.log_write();

    //数据库，配置数据库信息，设置数据库连接池，把数据库信息塞给用户以便连接
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}