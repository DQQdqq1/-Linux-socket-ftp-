#include <sys/types.h>     
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/in.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
 
int flag_quit = 0;
 
int cmd_handler(int fd,char msg[10000]) 
{
	int fd_file; //如果判断为upload,则用来存放即将发送的文件的标识符
	int size; //如果判断为upload,则用来存放即将发送的文件的大小
	int fd_fifo; //用来存放fifo的标识符
	char *fifo_msg = "quit";
	char *fifo_msg_get = "get";
	char *file_buf = NULL; //如果判断为upload,则用来存放即将发送的文件的内容
	char result[4096] = {0}; //用来存放popen函数调用指令后的结果
	FILE *fp; //用来存放popen返回的标识符
	int ret;
	int i = 0;
	char str[10000]; //用来存放客户端读取数据的备份，方便进行数据操作
	char* stat[5] ={NULL,NULL,NULL,NULL,NULL}; //用来存放分隔后的字符串
	char cmd[128]; //用来存放进行加工后的字符串
 
	strcpy(str,msg); //so split will not affect original data，原因详见server.c
 
	char* ptr = strtok(str, " "); //用空格分隔字符串
	for(; ptr != NULL; )
	{
		stat[i] = ptr;
		//printf("%s\n", stat[i]);
		i++;
		ptr = strtok(NULL, " ");
	}
 
	if(strcmp(stat[0],"client_ls\n")==0){ //如果是client_ls指令
		system("ls -l"); //执行ls-l
	}else if(strcmp(stat[0],"client_cd")==0){ //如果是client_cd指令
		sprintf(cmd, "%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		chdir(cmd); //cd到用户指定的目录
		system("pwd"); //然后执行pwd
	}else if(strcmp(stat[0],"get")==0){ //如果是get指令
		fd_fifo = open("./fifo",O_WRONLY); //阻塞的只写打开fifo，阻塞直到fifo被只读打开
		write(fd_fifo,fifo_msg_get,strlen(fifo_msg_get)); //向fifo中写信息，告诉负责读的子进程：用户调用了get指令
		close(fd_fifo);
	}else if(strcmp(stat[0],"quit\n")==0){ //如果是quit指令
		//printf("quit detected!\n");
		fd_fifo = open("./fifo",O_WRONLY); //阻塞的只写打开fifo，阻塞直到fifo被只读打开
		write(fd_fifo,fifo_msg,strlen(fifo_msg)); //向fifo中写信息，告诉负责读的子进程：用户调用了quit指令
		close(fd_fifo);
		close(fd); //关闭套接字
		wait(NULL); //等待子进程退出
		flag_quit = 1; //将退出标识符置1
 
	}else if(strcmp(stat[0],"upload")==0){ //如果是upload指令
		sprintf(cmd, "find . -name %.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		fp = popen(cmd,"r"); //通过调用find查看要上传的文件是否存在
		fread(&result, sizeof(char), sizeof(result), fp);
		pclose(fp);
		if(strlen(result)==0){
			printf("file not exit!\n");
			return 1;
		}else{
			ret = write(fd,"exist",20); //只有文件存在，才向服务器发送exist
			if(ret == -1){
				perror("write");
				return 1;
			}
		}
 
		memset(&result,0,sizeof(result));
 
		sprintf(cmd, "./%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		fd_file = open(cmd,O_RDWR); //打开要发送的文件，将内容全部复制，发送给服务器
		size = lseek(fd_file, 0L, SEEK_END);
		printf("file size = %d\n",size);
		file_buf = (char *)malloc(sizeof(char)*size + 1);
		lseek(fd_file, 0, SEEK_SET);
		ret = read(fd_file,file_buf,size);
		if(ret == -1){
			perror("read2");
			return 1;
		}
		close(fd_file);
		sleep(2); //ensure the "flag_upload" is set to 1 in server
		ret = write(fd,file_buf,size);
		if(ret == -1){
			perror("write");
			return 1;
		}
 
	}
 
	return 0;
}
 
void get_handler(char *file_name, char readbuf[10000]) //当确定要执行get命令时，会调用这个函数，通过服务器发来的文件名和文件内容创建文件
{
	int fd_file;
	int n_write;
	fd_file = open(file_name,O_RDWR|O_CREAT|O_TRUNC,S_IRWXU);
	n_write = write(fd_file, (char *)readbuf, strlen((char *)readbuf));
	printf("\ncreate new file %s, %d bytes have been written\n",file_name, n_write);
 
	close(fd_file);
 
}
 
 
 
int main(int argc, char **argv) //main函数
{
	int cnt = 0;
	char file_name[20]={0};
	int no_print = 0;
	int sockfd;
	int ret;
	int n_read;
	int n_write;
	char readbuf[10000];
	char msg[10000];
 
	int fd; //fifo
	char fifo_readbuf[20] = {0};
	int fd_get;
 
 
	pid_t fork_return;
 
	if(argc != 3){
		printf("param error!\n");
		return 1;
	}
 
 
	struct sockaddr_in server_addr;
	memset(&server_addr,0,sizeof(struct sockaddr_in));
 
	//socket
	sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(sockfd == -1){
		perror("socket");
		return 1;
	}else{
		printf("socket success, sockfd = %d\n",sockfd);
	}
 
	//connect
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[2]));//host to net (2 bytes)
	inet_aton(argv[1],&server_addr.sin_addr); 
	ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
	if(ret == -1){
		perror("connect");
		return 1;
	}else{
		printf("connect success!\n");
	}
 
	//fifo
	if(mkfifo("./fifo",S_IRWXU) == -1 && errno != EEXIST)
	{
		perror("fifo");
	}
 
	//fork
	fork_return = fork();
 
	if(fork_return > 0){//father keeps writing msg
		while(1){
			//write
			memset(&msg,0,sizeof(msg));
			fgets(msg,sizeof(msg),stdin);
			n_write = write(sockfd,&msg,strlen(msg));
 
			cmd_handler(sockfd,msg); //负责处理并判断用户输入的总函数
			if(flag_quit == 1){ //如果退出标识符被置位
				flag_quit = 0;
				break; //则父进程退出循环，然后退出
			}
 
			if(n_write == -1){
				perror("write");
				return 1;
			}else{
				//printf("%d bytes msg sent\n",n_write);
			}
		}
	}else if(fork_return < 0){
		perror("fork");
		return 1;
	}else{//son keeps reading 
		while(1){
			fd = open("./fifo",O_RDONLY|O_NONBLOCK); //非阻塞的只读打开FIFO
			lseek(fd, 0, SEEK_SET); //光标移到最前
 
			read(fd,&fifo_readbuf,20); //从FIFO读取数据
			if(strcmp(fifo_readbuf,"quit")==0){ //如果FIFO中是quit
				exit(1); //则子进程立刻退出
			}else if(cnt == 1){ //如果判断为get指令，服务器将发送两次消息，第一次为文件名，第二次为文件内容，使用cnt和fifo消息的读取来配合，第一次将读来的值赋值给文件名，第二次将读来的值作为文件内容传入get_handler用来创建文件
				get_handler(file_name, readbuf);
				cnt = 0;
				no_print = 0; //将静止打印标识符重新归0
			}else if(strcmp(fifo_readbuf,"get")==0){ //如上一个else if的注释所言，能进入这个循环，说明是get指令后服务器发送的第一次消息
				strcpy(file_name,readbuf); //重要！字符串的赋值要用strcpy!!
				if(strcmp(file_name,"file not exist\n")==0){ //如果服务器第一次发来的消息是文件不存在，则啥都不用干，就当这次get没发生
					//do nothing
				}else{ //此时，将cnt++，使得服务器下一次发送的消息会被准确的判断为文件的内容
					cnt++;
					no_print = 1; //并且使得静止打印的标识符为1，防止在界面中打印文件内容
				}
			}
			memset(&fifo_readbuf,0,sizeof(fifo_readbuf)); //重要，不要忘记
 
			//read
			memset(&readbuf,0,sizeof(readbuf));
			n_read = read(sockfd,&readbuf,sizeof(readbuf));
			if(ret == -1){
				perror("read1");
				return 1;
			}else if(ret != -1 && no_print == 0){ //只有静止打印标识符为0时，才打印服务器发来的消息，为了防止当get指令生效时，将服务器发来的大量文件内容打在屏幕上影响观感
				printf("\nserver: %s",readbuf); //dont need to add"\n"
			}
 
		}
 
	}
 
	return 0;
}
