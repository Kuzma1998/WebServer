#include "../include/sql_connection_pool.h"
#include <iostream>
#include <list>
#include <pthread.h>

connection_pool::connection_pool()
{
    this->CurConn  = 0;
    this->FreeConn = 0;
}

connection_pool* connection_pool::GetInstance()
{
    static connection_pool connPool;
    return &connPool;
}

// 初始化
void connection_pool::init(
    std::string  url,
    std::string  User,
    std::string  PassWord,
    std::string  DBName,
    int          Port,
    unsigned int MaxConn)
{
    this->url          = url;
    this->User         = User;
    this->PassWord     = PassWord;
    this->DataBaseName = DBName;
    this->Port         = Port;

    lock.lock();
    for (int i = 0; i < MaxConn; ++i) {
        MYSQL* con = nullptr;
        // 初始化一个连接
        con = mysql_init(con);
        if (con == nullptr) {
            std::cout << "Error:" << mysql_error(con) << std::endl;
            // 异常退出
            exit(1);
        }
        con = mysql_real_connect(
            con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(),
            Port, NULL, 0);

        if (con == nullptr) {
            std::cout << "Error connecting: " << mysql_error(con) << std::endl;
            exit(1);
        }
        connList.push_back(con);
        ++FreeConn;
    }
    // 代表有多少个数据库连接可以使用
    reserve       = sem(FreeConn);
    this->MaxConn = FreeConn;
    lock.unlock();
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection()
{
    MYSQL* con = nullptr;
    if (0 == connList.size()) {
        return nullptr;
    }
    // 资源数减一，如果没有链接池线程则阻塞在这上面
    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();
    --FreeConn;
    ++CurConn;
    lock.unlock();
    return con;
}

// 释放当前的连接
bool connection_pool::ReleaseConnection(MYSQL* con)
{
    if (con == nullptr)
        return false;
    lock.lock();
    connList.push_back(con);
    ++FreeConn;
    --CurConn;
    lock.unlock();
    // 资源+1，如果有则阻塞在上面的线程，那么就唤醒
    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{
    lock.lock();
    if (connList.size() > 0) {
        for (auto it = connList.begin(); it != connList.end(); ++it) {
            MYSQL* con = *it;
            mysql_close(con);
        }
        CurConn  = 0;
        FreeConn = 0;
        connList.clear();
        // lock.unlock();
    }
    lock.unlock();
}

int connection_pool::GetFreeConn()
{
    // 返回可用的连接数
    return this->FreeConn;
}

connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connPool)
{
    *SQL = connPool->GetConnection();

    conRAII  = *SQL;
    poolRAII = connPool;
}

// RAII是Resource Acquisition Is Initialization（wiki上面翻译成
// “资源获取就是初始化”）的简称，是C++语言的一种管理资源、避免泄漏的惯用法。利用的就是C++构造的对象最终会被销毁的原则。
// RAII的做法是使用一个对象，在其构造时获取对应的资源，在对象生命期内控制对资源的访问，使之始终保持有效，
// 最后在对象析构的时候，释放构造时获取的资源。

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}