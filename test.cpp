#include <bits/stdc++.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
using namespace std;

int main()
{
    char* doc_root = "/home/ljxdw/c++/WebServer/WebServer/root/judge.html";
    struct stat m_file_stat;
    if (stat(doc_root, &m_file_stat) < 0)
        cout << "no" << endl;
    else
        cout << "yes" << endl;
}
