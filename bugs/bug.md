# 遇到的问题

## 监听的socket设置为oenshot

## 输入url 没有界面
- 初始的judge页面打错了

## 注册一直失败
```c++
// 代码写错了
//如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char* sql_insert = (char*)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
```

## 视频发不出来
- 