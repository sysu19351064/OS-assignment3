#include <stdio.h>
#include <string.h>
#include <pthread.h> // for thread
#include <stdlib.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include<semaphore.h>
#include<math.h>


#define PTHREAD_SIZE 3
#define BUFFER_SIZE 10
//the path of fifo
#define _FIFO_ "./myfifo"

pthread_t ptid[PTHREAD_SIZE];  // 创建3个线程
int fd[PTHREAD_SIZE][2];
double lambda=0.0;

// 随机睡眠事件
double produce_time(double lambda_){
    double x;
    do{
        x = ((double)rand() / RAND_MAX);
    }
    while((x==0)||(x==1));
    return (-1/lambda_ * log(x));
}

struct msg{
    unsigned long src_id;
    char fill[4096];
};

// 消费者执行的任务
void *server_do(void *arg){
    struct msg Msg;
    while(1){
		double gap = produce_time(lambda);
        usleep(1000000*gap);	//产生负指数分布的间隔
        int ret = read(fd[(int)arg][0], &Msg, sizeof(Msg));
		// 注意非阻塞读空的情况
        if(ret > 0){
            printf("consumer %d read %d bytes %lu\n", (int)arg, ret, Msg.src_id);
        }
	}
}

//利用fcntl设置成非阻塞
int setFdNonblocking(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
    {
        perror("fcntl");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if(argc < 2) {
        printf("Usage: ./server lamda\n");
        return EXIT_FAILURE;
    }
    //epoll工作************************************************

    int ret;
    int ep_fd;
    struct epoll_event ev;
    const int MAXEVENTS = 4;                //最大事件数
    struct epoll_event *events;
    /* 创建epoll池 */
    ep_fd = epoll_create1(0);
    for(int i = 0;i < PTHREAD_SIZE; i++){
        if(pipe(fd[i]) < 0){
            printf("pipe error!\n");
        }
        // pipe写、读端非阻塞
        setFdNonblocking(fd[i][1]);
        setFdNonblocking(fd[i][0]);
        // 将检测事件加入epoll池中
//        ev.events = EPOLLOUT | EPOLLET;   /* 监测fd[i][0]可读，且以边沿方式触发 */
        ev.events = EPOLLOUT;   /* 监测fd[i][0]可读，且以水平方式触发 */
        ev.data.fd =fd[i][1];
        epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd[i][1], &ev);
    }


	char lam[10];
	strcpy(lam, argv[1]);
	lambda = atof(lam);
	//printf("%d", lambda);

	//创建消费者线程
	for(int j = 0; j<PTHREAD_SIZE; j++){
		ret = pthread_create(&ptid[j], NULL, &server_do, (void*)(j));
		if(ret != 0) {
        	fprintf(stderr, "pthread_create error: %s\n", strerror(ret));
    	}
    }
	
	//主线程工作
	int fifo;
    char str[8];
    int cnt;

    //创建管道
    if(access(_FIFO_, F_OK) == -1) {
        if(mkfifo(_FIFO_, 0644) != 0) {
            perror("mkfifo()");
            exit(EXIT_FAILURE);
        }
        else {
            printf("new fifo %s named pipe created\n", _FIFO_);
        }
    }

    fifo = open(_FIFO_, O_RDONLY );
    if(fifo == -1)
    {
        printf("read fifo open fail...\n");            
        exit(-1);
        return;
    }

    // 读端非阻塞
    setFdNonblocking(fifo);
    printf("Listen fifo...");

    //将检测事(ep_fd, EPOLL_CTL_ADD, fifo, &event);
//    ev.events = EPOLLIN | EPOLLET;   /* 监测fifo可读，且以边沿方式触发 */
    ev.events = EPOLLIN;   /* 监测fifo可读，且以水平方式触发 */
    ev.data.fd = fifo;
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, fifo, &ev);

    /* events用于存放被触发的事件 */
    events = malloc(sizeof(struct epoll_event) * MAXEVENTS);

    struct msg Buffer[BUFFER_SIZE];
    for(int i = 0; i < BUFFER_SIZE; i++){
        Buffer[i].src_id = 0;
    }
    // 缓冲区指针
    int in = 0;
    int out = 0;
    struct msg Msg;
    /* 阻塞等待监测事件触发 */
    while(1){
        cnt = epoll_wait(ep_fd, events, MAXEVENTS, -1);
//        printf("cnt = %d\n", cnt);
        //printf("1\n");
        for(int i=0;i<cnt;i++){
            if(events[i].events & EPOLLIN){
                //从管道中读出slot
//                ret = read(fifo, read_buf, BUFFER_SIZE);
                ret = read(fifo, &Msg, sizeof(Msg));
                printf("FIFO get %d bytes %lu\n", ret, Msg.src_id);
                // 缓冲区填充，一般情况不再需要考虑消息对齐。
                Buffer[in].src_id = Msg.src_id;
                strcpy(Buffer[in].fill, Msg.fill);
                in = (in+1) % BUFFER_SIZE;
            }
            else if (events[i].events & EPOLLOUT){
//                printf("YES!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
//                ret = write(events[i].data.fd, read_buf, BUFFER_SIZE);
                if(Buffer[out].src_id != 0){
                    ret = write(events[i].data.fd, &Buffer[out], sizeof(Buffer[out]));
                    Buffer[out].src_id = 0;
                    out = (out+1) % BUFFER_SIZE;
                    printf("fd = %d, send %d bytes\n", events[i].data.fd, ret);
                }
            }
        }
    }
    free(events);
    close(ep_fd);
    close(fifo);

	//回收线程
    for(int i=0;i<PTHREAD_SIZE;i++){
        ret = pthread_join(ptid[i],NULL);
		if(ret != 0) {
        	perror("pthread_join()");
    	}
    }
    return 0;
}

