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
 
int flag_upload = 0;
int flag_exist = 0;
char *file_name=NULL;
 
int cmd_handler(int fd, char readbuf[10000])
{
	int fd_file; 判断为get命令后，用来存放即将发送的文件的文件标识符
	int size; //判断为get命令后，用来存放即将发送的文件的大小
	char *file_buf = NULL; //判断为get命令后，用来存放即将发送的文件的内容
	int ret;
	int i = 0;
	char result[4096] = {0}; //用来存放popen执行指令的结果
	FILE *fp; //popen返回的文件流标识符
	char str[10000]; //将读到的数据备份在这里
	char* stat[5] ={NULL,NULL,NULL,NULL,NULL}; //用来存放拆分后的字符串
	char cmd[128]; //用来存放加工后的字符串，比如去除“\n”，比如再加上“./”
 
	strcpy(str,readbuf); //由于字符串的首地址是字符串的名字，所以此时相当于传入的地址，所有对字符串的操作都会影响它，所以需要进行备份，先备份再对备份的数据进行数据处理就不会影响原数据了
 
	char* ptr = strtok(str, " "); //通过空格符号分隔字符串
	for(; ptr != NULL; )
	{
		stat[i] = ptr;
		//printf("%s\n", stat[i]);
		i++;
		ptr = strtok(NULL, " ");
	}
 
	if(strcmp(stat[0],"get")==0){ //如果是get命令
		sprintf(cmd, "find . -name %.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		fp = popen(cmd,"r"); //运行指令查看文件是否存在
		fread(&result, sizeof(char), sizeof(result), fp);
		pclose(fp);
		if(strlen(result)==0){
			ret = write(fd,"file not exist\n",20); //不存在就发送给客户端来通知
			if(ret == -1){
				perror("write");
				return 1;
			}
 
			return 1;
		}else{
			sprintf(cmd, "./%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
			ret = write(fd,cmd,strlen(cmd)); //如果存在，就先将文件名传给客户端
			if(ret == -1){
				perror("write");
				return 1;
			}
		}
		memset(&result,0,sizeof(result));
 
		//sleep(2);
 
		sprintf(cmd, "./%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		fd_file = open(cmd,O_RDWR); //然后打开这个文件，将内容全部复制
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
		ret = write(fd,file_buf,size); //将文件的内容发送给客户端
		if(ret == -1){
			perror("write");
			return 1;
		}
 
	}else if(strcmp(stat[0],"server_cd")==0){ //如果是server_cd命令
		sprintf(cmd, "%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		chdir(cmd); //将目录cd到客户端要求的位置
		fp = popen("pwd","r"); //然后执行pwd，将结果发回给客户端
		fread(&result, sizeof(char), 1024, fp);
		pclose(fp);
		ret = write(fd,&result,strlen(result));
		if(ret == -1){
			perror("write");
			return 1;
		}
 
		memset(&result,0,sizeof(result));
 
	}else if(strcmp(stat[0],"upload")==0){ //如果是upload命令
		flag_upload = 1; //表示准备upload
		sprintf(cmd, "./%.*s",(int)(strlen(stat[1])-1),stat[1]); //remove "\n"
		file_name = cmd; //接收客户端发来的，即将发送过来的文件名
	}else if(strcmp(stat[0],"exist")==0){
		flag_exist = 1;  //和准备upload的标识符配合使用，只有客户端判断用户输入的upload文件存在时，才会发送exist，此时服务端将文件存在的标识符也置1
	}else if(strcmp(stat[0],"server_ls\n")==0){ //如果是server_ls命令
		fp = popen("ls -l","r"); //执行ls-l命令，然后把结果发回客户端
		fread(&result, sizeof(char), 1024, fp);
		pclose(fp);
		ret = write(fd,&result,strlen(result));
		if(ret == -1){
			perror("write");
			return 1;
		}
 
		memset(&result,0,sizeof(result));
	}else if(strcmp(stat[0],"quit\n")==0){ //如果客户端打出了quit
		ret = write(fd,"Bye\n",4); //立刻回发一个Bye，目的是让客户端取消接收阻塞然后成功从FIFO读取到退出信息
		if(ret == -1){
			perror("write");
			return 1;
		}
	}
 
 
	return 0;
}
 
void upload_handler(char readbuf[10000]) //upload指令如果被最终判断为可以执行，则会调用这个函数来通过客户端发来的文件名和文件内容来创建文件
{
	int fd_file;
	int n_write;
	fd_file = open(file_name,O_RDWR|O_CREAT|O_TRUNC,S_IRWXU);
	n_write = write(fd_file, (char *)readbuf, strlen((char *)readbuf));
	printf("create new file %s, %d bytes have been written\n",file_name, n_write);
 
	close(fd_file);
 
}
 
int main(int argc, char **argv) //main函数
{
	int conn_num = 0;
	int flag = 0;
	int sockfd;
	int conn_sockfd;
	int ret;
	int n_read;
	int n_write;
	int len = sizeof(struct sockaddr_in);
	char readbuf[10000];
	char msg[10000];
 
 
	pid_t fork_return;
	pid_t fork_return_1;
 
	struct sockaddr_in my_addr;
	struct sockaddr_in client_addr;
	memset(&my_addr,0,sizeof(struct sockaddr_in));
	memset(&client_addr,0,sizeof(struct sockaddr_in));
 
	if(argc != 3){
		printf("param error!\n");
		return 1;
	}
 
	//socket
	sockfd = socket(AF_INET,SOCK_STREAM,0);
	if(sockfd == -1){
		perror("socket");
		return 1;
	}else{
		printf("socket success, sockfd = %d\n",sockfd);
	}
 
	//bind
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(atoi(argv[2]));//host to net (2 bytes)
	inet_aton(argv[1],&my_addr.sin_addr); //char* format -> net format
 
	ret = bind(sockfd, (struct sockaddr *)&my_addr, len);
	if(ret == -1){
		perror("bind");
		return 1;
	}else{
		printf("bind success\n");
	}
 
	//listen
	ret = listen(sockfd,10);
	if(ret == -1){
		perror("listen");
		return 1;
	}else{
		printf("listening...\n");
	}
 
	while(1){
		//accept
		conn_sockfd = accept(sockfd,(struct sockaddr *)&client_addr,&len);
		if(conn_sockfd == -1){
			perror("accept");
			return 1;
		}else{
			printf("accept success, client IP = %s\n",inet_ntoa(client_addr.sin_addr));
 
		}
 
		fork_return = fork();
 
		if(fork_return > 0){//father keeps waiting for new request
			//do nothing	
		}else if(fork_return < 0){
			perror("fork");
			return 1;
		}else{//son deals with request
			while(1){
				//read
				memset(&readbuf,0,sizeof(readbuf));
				ret = recv(conn_sockfd, &readbuf, sizeof(readbuf), 0);
				if(ret == 0){ //如果recv函数返回0表示连接已经断开
					printf("client has quit\n");
					close(conn_sockfd);
					break;
				}else if(ret == -1){
					perror("recv");
					return 1;
				}
 
				if(flag_upload == 1 && flag_exist == 1){ //当准备upload的标识位和文件存在的标识位同时置1时，才会进入这段代码
					flag_exist = 0; //立刻将两个标识位重新置0
					flag_upload = 0;
					upload_handler(readbuf); //并执行upload所对应的函数
				}
 
				cmd_handler(conn_sockfd, readbuf); //对客户端发来的消息进行判断的总函数
 
				//printf("\nclient: %s",readbuf); //dont need to add"\n"
 
 
			}
			exit(2);
		}
 
	}
 
 
	return 0;
}
