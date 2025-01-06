#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <glib.h>
#include <sys/time.h>

#define MAX_CLIENTS 1024
#define LOG_DOMAIN "update_server"
#define LOG_FILE_PATH "../server_log.txt"
#define PORT 4567

typedef struct FileContent
{
  int length;
  char *content;
} FileContent;

typedef struct MESSAGE
{
  int type;
  int length;
  char *content;
} Message;

// 日志
GLogLevelFlags log_level = -1;
char *log_print_mode = NULL;
FILE *log_file = NULL;
GMutex *log_lock = NULL;

// tcp-epoll
int server_fd;
int epoll;
int curFd;
GThreadPool *t_pool;
struct epoll_event epevs[1024];
struct sockaddr_in server_addr;

// 自定义日志处理器函数
static void log_handler(const gchar *log_domain, GLogLevelFlags log_level_tmp, const gchar *message, gpointer user_data)
{

  if (log_level_tmp <= log_level)
  {
    // log_level打印日志级别 debug 1 << 7

    // 获取当前时间
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // 获取当前日期和时间
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char time_str[64]; // 用于存储格式化后的日期和时间字符串

    // 格式化日期和时间
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time);
    // 格式化日期和时间，包括微秒
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d.%ld",
             local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday,
             local_time->tm_hour, local_time->tm_min, local_time->tm_sec,
             tv.tv_usec); // 将微秒转换为两位数

    g_mutex_lock(log_lock);

    if (g_str_match_string("console", log_print_mode, TRUE))
    {

      printf("[%s/%s]:%s\n", time_str, LOG_DOMAIN, message);
    }
    if (g_str_match_string("file", log_print_mode, TRUE))
    {

      fprintf(log_file, "[%s/%s]:%s\n", time_str, LOG_DOMAIN, message);
    }
    g_mutex_unlock(log_lock);
  }
}
// 初始化日志
void log_init(char *logFileName, char *logPrintMode, GLogLevelFlags logLevel)
{

  // 初始化日志级别
  log_level = logLevel;
  log_file = fopen(LOG_FILE_PATH, "a");
  // 添加自定义的日志处理器
  g_log_set_handler(LOG_DOMAIN, log_level, log_handler, NULL);
  // 设置日志的默认输出函数（可选）
  g_log_set_default_handler(log_handler, NULL); // 如果不设置输出拿到控制台
  // 初始化日志锁
  log_lock = g_mutex_new();
  log_print_mode = logPrintMode;
}
// 连接客户端
void serverTcpConnect(int *server_fd, struct sockaddr_in *server_addr, int port)
{
  printf("等待客户端连接\n");
  // 起手式：套接字、地址、二者绑定
  *server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (*server_fd == -1)
  {
    perror("ERROR socket");
    exit(1);
  }

  int addr_size = sizeof(*server_addr);
  server_addr->sin_family = AF_INET;
  server_addr->sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr->sin_port = htons(port);

  int ret_bind = bind(*server_fd, (struct sockaddr *)server_addr, addr_size);
  if (ret_bind == -1)
  {
    perror("ERROR bind");
    exit(1);
  }
  int ret_listen = listen(*server_fd, 10); // 10 最大连接
  if (ret_listen == -1)
  {
    perror("监听失败！");
    exit(1);
  }
}
// 线程池处理函数
void *recvMessage(gpointer data, gpointer user_data);
// 接收版本信息
Message *receive_message(int client_socket, int *epoll)
{
  Message *msg = calloc(1, sizeof(Message));
  // 接收头
  int ret_recv = recv(client_socket, msg, sizeof(Message), 0);
  if (ret_recv < 0)
  {
    g_warning("Error recv messag header");
    return NULL;
  }
  if (ret_recv == 0)
  {
    epoll_ctl(*epoll, EPOLL_CTL_DEL, client_socket, NULL); // 在红黑树中删除
    return NULL;
  }
  int bytes_sent = -1;
  msg->content = calloc(1, msg->length);
  int remaining_length = msg->length;
  while (remaining_length > 0)
  {
    bytes_sent = recv(client_socket, msg->content + (msg->length - remaining_length), remaining_length, 0);
    if (bytes_sent < 0)
    {
      g_warning("Error recv messag content");
      return NULL;
    }
    remaining_length -= bytes_sent;
  }
  return msg;
}
// 发送版本消息
int send_message(int client_socket, Message *msg)
{
    //发送消息头 type,length
    int bytes_sent = send(client_socket,msg,sizeof(Message),0);
    if (bytes_sent < 0)
    {
        perror("Error sending messag header");
        return -1;
    }
    //发内容
    int remaining_length = msg->length;
    while (remaining_length>0)
    {
        bytes_sent = send(client_socket,msg->content+(msg->length-remaining_length),remaining_length,0);
        if (bytes_sent < 0)
        {
            perror("Error sending messag content");
            return -1;
        }
       remaining_length -= bytes_sent;
    }
    return 0;
}
//读取ini文件
FileContent* readFile(char* filePath){
  FileContent* result = calloc(1,sizeof(FileContent));
  FILE *file = fopen(filePath,"rb");
  //获取文件大小
  fseek(file,0,SEEK_END);
  int length = ftell(file);
  result->length = length;
  fseek(file,0,SEEK_SET);
  //读取文件内容
  result->content = calloc(1,length);
  fread(result->content, sizeof(char), length, file);
  fclose(file);
  //返回FIlecontent
  return result;
}
// ctrl+c关闭进程释放资源
void signalHandler(int signum)
{
  printf("Received Ctrl+C, cleaning up...\n");
  close(server_fd);
  fclose(log_file);
  g_mutex_free(log_lock);
  exit(0);
}

int main(int argc, char const *argv[])
{
  // ctrl+c释放资源
  signal(SIGINT, signalHandler);
  // 日志
  log_init(LOG_FILE_PATH, "file", G_LOG_LEVEL_DEBUG);
  g_debug("test");
  // 连接客户端并作监听
  serverTcpConnect(&server_fd, &server_addr, PORT);
  // 线程池初始化
  t_pool = g_thread_pool_new((GFunc)recvMessage, NULL, 10, FALSE, NULL);
  // 用epoll管理连接的服务器
  epoll = epoll_create(100);
  if (epoll == -1)
  {
    perror("ERROR epoll_create");
    return -1;
  }
  struct epoll_event event = {
      .events = EPOLLIN,
      .data.fd = server_fd};
  epoll_ctl(epoll, EPOLL_CTL_ADD, server_fd, &event);

  while (1)
  {
    int retVal = epoll_wait(epoll, epevs, 1024, 3000); // 每3秒把监测到的客户端连接状态处理一次

    if (retVal == -1)
    {
      perror("ERROR epoll_wait");
      return -1;
    }
    else if (retVal == 0)
    {
      continue;
    }
    else if (retVal > 0)
    {
      for (int i = 0; i < retVal; i++)
      {
        curFd = epevs[i].data.fd;
        if (server_fd == curFd)
        {
          // 监听的文件描述符有数据达到，有客户端连接
          struct sockaddr_in cliaddr;
          int len = sizeof(cliaddr);
          int cfd = accept(server_fd, (struct sockaddr *)&cliaddr, &len);

          struct epoll_event epev;
          epev.events = EPOLLIN; // 后面可能会变成 EPOLLOUT
          epev.data.fd = cfd;
          epoll_ctl(epoll, EPOLL_CTL_ADD, cfd, &epev);
        }
        else
        {
          if (epevs[i].events & EPOLLOUT)
          {
            continue;
          }
          if (!g_thread_pool_push(t_pool, GINT_TO_POINTER(curFd), NULL))
          {
            perror("ERROR g_thread_pool_push");
          }
        }
      }
    }
  }
  return 0;
}

// 线程池处理函数
void *recvMessage(gpointer data, gpointer user_data)
{
  int client_fd = GPOINTER_TO_INT(data);
  // 读取最新版本信息，比较客户端版本
    GKeyFile *keyFile = g_key_file_new();
    if (!g_key_file_load_from_file(keyFile, "../server.ini", G_KEY_FILE_NONE, NULL))
    {
      g_warning("load update.ini error!");
      return NULL;
    }
    gchar *version = g_key_file_get_string(keyFile, "check", "version", NULL);
    gchar *description = g_key_file_get_string(keyFile, "check", "description", NULL);
  Message *msg = receive_message(curFd, &epoll);
  if (msg != NULL)
  {
    g_print("serfver->type:%d,length:%d,content:%s\n", msg->type, msg->length, msg->content);
    g_debug("serfver->type:%d,length:%d,content:%s", msg->type, msg->length, msg->content);
    
    // 把最新的版本信息发送给客户端
    Message *sendMsg = calloc(1, sizeof(Message));
    //接收客户端版本信息
    if (msg->type == 0 && msg->length > 0)
    {
      g_debug("recy check update msg!");
      sendMsg->type = 0;
      char *versionFromClient = msg->content;
      if (g_strcmp0(version, versionFromClient) != 0)// 不是最新，回复消息
      {
        FileContent *fileContent = readFile("../server.ini");
        // 原来只是相应详情信息，现在变更位置文件的内容
        sendMsg->content = fileContent->content; // description;
        sendMsg->length = fileContent->length;
        g_debug("check version result: can update!");
      }else//最新版无需跟新
      {
        sendMsg->length = 0;
        g_debug("check version result: latest!");
      }
      if (send_message(curFd, sendMsg) == -1)
      {
        g_warning("reply check verion msg error!");
      }
      else
      {
        g_debug("reply check verion msg success!");
      }
    }
    //接收客户端更新请求
    if (msg->type == 1)
    {
      printf("recv updata msg\n");
      g_debug("recv update msg!..........................");
      sendMsg->type = 1;
      FileContent *fileContent = readFile("../project.zip");
      // 原来只是相应详情信息，现在变更位置文件的内容
      sendMsg->content = fileContent->content; // description;
      sendMsg->length = fileContent->length;
      if (send_message(curFd, sendMsg) == -1)
      {
        g_warning("reply update msg error!");
      }
      else
      {
        g_debug("reply update msg success!...................");
      }
    }
  }
return 0;
}
