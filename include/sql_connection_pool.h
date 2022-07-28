#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include "lock.h"
#include <error.h>
#include <iostream>
#include <list>
#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>
#include <string>

class connection_pool {
public:
    MYSQL* GetConnection();      // 获取数据库的连接
    bool   ReleaseConnection(MYSQL* con);  // 释放连接
    int    GetFreeConn();        // 获取连接
    void   DestroyPool();        // 销毁所有连接

    // 单例模式
    static connection_pool* GetInstance();

    void init(
        std::string  url,
        std::string  User,
        std::string  PassWord,
        std::string  DataBaseName,
        int          Port,
        unsigned int MaxConn);

    connection_pool();
    ~connection_pool();

private:
    std::string url;           //主机的地址
    std::string Port;          // 数据库端扣
    std::string User;          //登录数据库的用户名
    std::string PassWord;      // 登录数据库密码
    std::string DataBaseName;  // 登录的数据库名字

    locker            lock;
    std::list<MYSQL*> connList;
    sem               reserve;

    unsigned int MaxConn;   // 最大连接数
    unsigned int CurConn;   // 当前连接数
    unsigned int FreeConn;  //空闲的连接数
};

class connectionRAII {
public:
    connectionRAII(MYSQL** con, connection_pool* connPool);
    ~connectionRAII();

private:
    MYSQL*           conRAII;
    connection_pool* poolRAII;
};

#endif