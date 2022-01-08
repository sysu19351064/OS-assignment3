#include <stdio.h>
#include <string.h>
#include <pthread.h> // for thread
#include <stdlib.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include<semaphore.h>
#include<math.h>
#include <errno.h>
#include <sys/epoll.h>


#define PTHREAD_SIZE 3			//任务要求线程数
#define BUFFER_SIZE 10
//the path of fifo
#define _FIFO_ "./myfifo"

pthread_t ptid[PTHREAD_SIZE];  // 创建三个线程
int fd[PTHREAD_SIZE][2];
double lambda=0.0;

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


//生产者线程执行的任务
void *client_do(void *arg)
{
    struct msg Msg;
    memset(Msg.fill, 0, 4096);
	while(1){
		double gap = produce_time(lambda);
        usleep(1000000*gap);	//产生负指数分布的间隔
//        printf("sleep %f\n", gap);
//        sleep(gap);	//产生负指数分布的间隔
        Msg.src_id = pthread_self();
        int ret = write(fd[(int)arg][1], &Msg, sizeof(Msg));	//写进管道
		printf("producer %d write %d bytes %lu\n", (int)arg, ret, Msg.src_id);
	}
    pthread_exit(0);
}

//利用fcntl设置成非阻塞
int setFdNonblocking(int fd){
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1){
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1){
        perror("fcntl");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
	struct epoll_event ev;                     //事件临时变量
    const int MAXEVENTS = 4;                //最大事件数
    struct epoll_event *events;      //监听事件数组
    int ep_fd;
    int ret;
    int cnt;
    // 检查参数
	if(argc < 2) {
        printf("Usage: ./client lamda\n");
        return EXIT_FAILURE;
    }
    //epoll工作************************************************
    /* 创建epoll池 */
    ep_fd = epoll_create1(0);

    // 创建pipe
    for(int i = 0; i < PTHREAD_SIZE; i++){
        if(pipe(fd[i]) < 0){
            printf("pipe error!\n");
        }
        // 写、读端非阻塞
        setFdNonblocking(fd[i][1]);
        setFdNonblocking(fd[i][0]);
        // 将检测事件加入epoll池中
//        ev.events = EPOLLIN | EPOLLET;   /* 监测fd[i][0]可读，且以边沿方式触发 */
        ev.events = EPOLLIN;   /* 监测fd[i][0]可读，且以水平方式触发 */
        ev.data.fd =fd[i][0];
		// 添加到监听事件中
        ret = epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd[i][0], &ev);
        // printf("ret: %d", ret);
    }

    //Producer线程工作************************************************
    char lam[10];
    strcpy(lam, argv[1]);
    lambda = atof(lam);
    //printf("%d", lambda);

    //创建线程
    for(int j = 0; j<PTHREAD_SIZE; j++){
        ret = pthread_create(&ptid[j], NULL, &client_do, (void*)(j));
        if(ret != 0) {
            fprintf(stderr, "pthread_create error: %s\n", strerror(ret));
        }
    }


	//主线程工作**************************************************
//	char write_buf[BUFFER_SIZ = 0E];
	int fifo;
	//以只写的方式打开管道fifo文件
    fifo = open(_FIFO_,O_WRONLY);
    if(fifo == -1)
    {
    	printf("write fifo open fail....\n");
        exit(-1);
        return;
    }
    // FIFO写端非阻塞
    setFdNonblocking(fifo);

    //将检测事(ep_fd, EPOLL_CTL_ADD, fifo, &event);
//    ev.events = EPOLLOUT | EPOLLET;   /* 监测fifo1可写，且以边沿方式触发 */
    ev.events = EPOLLOUT;   /* 监测fifo1可写，且以水平方式触发 */
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

    /* 判断监测事件 */
    while(1) {
        /* 阻塞等待监测事件触发 */
        cnt = epoll_wait(ep_fd, events, MAXEVENTS, -1);
//        printf("cnt = %d\n", cnt);
        for (int i = 0; i < cnt; i++) {
            if (events[i].events & EPOLLOUT){
                Msg.src_id = Buffer[out].src_id;
                if (Msg.src_id != 0){
                    Buffer[out].src_id = 0;
                    out = (out+1) % BUFFER_SIZE;
                    ret = write(fifo, &Msg, sizeof(Msg));
                    printf("FIFO, send %d bytes %lu\n", ret, Msg.src_id);
                }
            }
            else if (events[i].events & EPOLLIN) {
                ret = read(events[i].data.fd, &Msg, sizeof(Msg));
                printf("fd = %d, recv %d bytes %lu\n", events[i].data.fd, ret, Msg.src_id);
                // 缓冲区填充，消息对齐
                if(ret == 4104){
                    Buffer[in].src_id = Msg.src_id;
                    strcpy(Buffer[in].fill, Msg.fill);
                    in = (in+1) % BUFFER_SIZE;
                }
                else if(ret == 8){
                    Buffer[in].src_id = Msg.src_id;
                    memset(Buffer[in].fill, 0, sizeof(Buffer[in].fill));
                    in = (in+1) % BUFFER_SIZE;
                }
            }
        }
    }
    free(events);
    close(ep_fd);       /* 注意关闭epoll池的描述符 */
//    close(fd);
    close(fifo);

    //线程回收
    for(int i=0;i<PTHREAD_SIZE;i++){
        ret = pthread_join(ptid[i],NULL);
		if(ret != 0) {
        	perror("pthread_join()");
    	}
    }
    
    return 0;
}

