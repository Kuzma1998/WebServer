server: main.cpp ./threadPool/threadPool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h 
	g++ -o server main.cpp ./threadPool/threadPool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h -lpthread 


clean:
	rm  -r server