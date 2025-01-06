#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/stat.h>
#include <strings.h>
#include <pthread.h>
#include <signal.h>
#include <glib.h>
#include <sys/time.h>
#include <zip.h>

#define LOG_DOMAIN "update_server"
#define LOG_FILE_PATH "../client_log.txt"
#define SERVER_IP "127.0.0.1"
#define PORT 4567
#define DBG_SEQ 0
#define LOGI printf
#define LOGD printf
#define LOGE printf

struct sockaddr_in server_addr;
int client_fd;

typedef struct FileContent
{
  int length;
  char *content;
} FileContent;

typedef struct MESSAGE
{
    int type;
    int length;
    char * content;
} Message;

//日志
GLogLevelFlags log_level = -1;
char *log_print_mode = NULL;
FILE *log_file = NULL; 
GMutex *log_lock = NULL;

//读取ini文件
FileContent* readFile(const char* filePath){
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
//创建文件夹
int pmkdir(const char *path) 
{ 
    char name[256]; 
    strcpy(name, path); 
    int i, len = strlen(name); 

    if (name[len-1]!='/') {
        strcat(name, "/");
    } 

    len = strlen(name); 

    for (i=1 ; i<len ; i++) { 
        if (name[i]=='/') { 
            name[i] = 0; 
            if ( access(name, 0) !=0 ) { 
                if (mkdir(name, 0755) == -1) {  
                    LOGE("mkdir error");  
                    return -1;  
                } 
            }
            name[i] = '/'; 
        } 
    } 

    return 0; 
} 
// 自定义日志处理器函数  
static void log_handler(const gchar *log_domain, GLogLevelFlags log_level_tmp, const gchar *message, gpointer user_data) {  
   
   if (log_level_tmp<=log_level)
    {
      //log_level打印日志级别 debug 1 << 7

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
         
        if (g_str_match_string ("console",log_print_mode,TRUE))
        { 
            
            printf("[%s/%s]:%s\n",time_str, LOG_DOMAIN, message);
            
        }
         if (g_str_match_string ("file",log_print_mode,TRUE))
        { 
           
           fprintf(log_file, "[%s/%s]:%s\n",time_str, LOG_DOMAIN, message);
        }
        g_mutex_unlock(log_lock);
    }

} 
//初始化日志
void log_init(char *logFileName,char *logPrintMode,GLogLevelFlags logLevel){

    //初始化日志级别
    log_level = logLevel;
    log_file =  fopen(LOG_FILE_PATH,"a");
    // 添加自定义的日志处理器  
    g_log_set_handler(LOG_DOMAIN, log_level, log_handler, NULL);  
    // 设置日志的默认输出函数（可选）  
    g_log_set_default_handler(log_handler, NULL);   //如果不设置输出拿到控制台
    //初始化日志锁
    log_lock = g_mutex_new();
    log_print_mode = logPrintMode;
}
// 发送消息
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
// 接收版本信息
Message *receive_message(int client_socket)
{
  Message *msg = calloc(1, sizeof(Message));
  // 接收头
  int ret_recv = recv(client_socket, msg, sizeof(Message), 0);
  if (ret_recv < 0)
  {
    g_warning("Error recv messag header");
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
//把服务器发的最新版本信息写在本地
int writeFile(char* path,const char* buffer, int length){
    //打开文件
    FILE *file = fopen(path,"wb");
    //写文件
    fwrite(buffer,1,length,file);
    //关闭
    fclose(file);
}
// 计算md5
const char *getMd5(const char *filepath)
{
    const char *md5 = NULL;
    // 读取文件到缓冲区
    FileContent *fc = readFile(filepath);
    if (fc == NULL)
    {
        g_warning("ReadFile %s error", filepath);
        return NULL;
    }
    // 调用glib计算md5
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_MD5);
    g_checksum_update(checksum, fc->content,fc->length);
    free(fc->content);
    free(fc);
    md5 = g_checksum_get_string(checksum);
    return md5;
}
// 通过md5检测文件完整性
gboolean checkMd5(const char *filepath)
{
    GKeyFile *srvIni = g_key_file_new();
    GError *err = NULL;
    // 加载配置文件
    if (!g_key_file_load_from_file(srvIni, filepath, G_KEY_FILE_NONE, &err))
    {
        g_warning("Load srv ini error:%s", filepath);
        return FALSE;
    }
    //获取文件中hash值
    gchar *fileHash = g_key_file_get_string(srvIni, "update", "hash", NULL);
    g_info("get hash %s", fileHash);
    //对文件产生hash
    const gchar* updata_file_path="../update.zip";
    gchar *genHash = getMd5(updata_file_path);
     g_info("gen hash %s", genHash);
    return g_strcmp0(fileHash,genHash)==0;
}

//解压
gboolean unzip(char *zipPath)
{
    int err = 0;
    char strerr[1024];
    struct zip *z = NULL;

    z = zip_open(zipPath, ZIP_CREATE, &err);
    if (z != NULL)
    {
        zip_int64_t i, c = zip_get_num_entries(z, ZIP_FL_UNCHANGED);
        for (i = 0; i < c; i++)
        {
            const char *name = zip_get_name(z, i, ZIP_FL_ENC_GUESS);
            LOGI("find %s\n", name);
            char *d = strdup(name);
            if (d != NULL)
            {
                char *p = strrchr(d, '/');
                if (p != NULL)
                {
                    *p = '\0';
                    pmkdir(d);
                }
                free(d);
                GString* str=g_string_new("../");
                g_string_append(str,name);
                FILE *fp = fopen(str->str, "w+b");
                struct zip_file *f = zip_fopen(z, name, 0);
                if (f != NULL && fp != NULL)
                {
                    zip_int64_t j, n = 0;
                    char buf[8192] = "";
                    while ((n = zip_fread(f, buf, sizeof(buf))) > 0)
                    {
                        for (j = 0; j < n; j++)
                        {
                            putc(buf[j], fp);
                        }
                    }
                    fclose(fp);
                    zip_fclose(f);
                }
            }
            else
            {
                return FALSE;
                LOGE("memory low\n");
            }
        }
        err = zip_close(z);
    }
    else
    {
        zip_error_to_str(strerr, 1024, err, errno);
        LOGE("operated zip fail for %s\n", strerr);
        return FALSE;
    }
    return TRUE;
}

// 强制复制文件替换应用失败
gboolean copyFile(const char *srcfile, const char *dstfile)
{
    gboolean result = TRUE;
    FILE *fpsrc = fopen(srcfile, "rb");
    if (fpsrc == NULL)
    {
        g_warning("%s读取失败", srcfile);
        return FALSE;
    }
    fseek(fpsrc, 0, SEEK_END);
    int totalsize = ftell(fpsrc);
    rewind(fpsrc);
    FILE *fpdst = fopen(dstfile, "wb");
    if (fpdst == NULL)
    {
        fclose(fpsrc);
        g_warning("%s写入失败", dstfile);
        return FALSE;
    }
    // 循环写入
    char* buffer = malloc(totalsize);
    int bytes_read = 0, bytes_write = 0;
    while ((bytes_read = fread(buffer + bytes_read, sizeof(char), 128, fpsrc)) > 0)
    {
        bytes_write = fwrite(buffer + bytes_write, sizeof(char), bytes_read, fpdst);
        if (bytes_read != bytes_write)
        {
            g_warning("写入%s失败", dstfile);
            result = FALSE;
            break;
        }
    }

    fclose(fpsrc);
    fclose(fpdst);
    if (buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F')
    {
        struct stat stdst;
        // 获取文件权限
        if (stat(dstfile, &stdst) < 0)
        {
            g_warning("stat %s error", dstfile);
            free(buffer);
            return FALSE;
        }
        // 添加可执行权限
        chmod(dstfile, stdst.st_mode | S_IXUSR | S_IRGRP | S_IXOTH);
    }
    free(buffer);
    return TRUE;
}
//替换文件
void replaceFiles(const char *filepath){
  GKeyFile *srvIni = g_key_file_new();
    GError *err = NULL;
    // 加载配置文件
    if (!g_key_file_load_from_file(srvIni, filepath, G_KEY_FILE_NONE, &err))
    {
        g_warning("Load srv ini error:%s", filepath);
    }
  
    //获取文件中hash值
    int fileNums = g_key_file_get_integer(srvIni, "update", "fileNums", NULL);

    //分别替换文件
    for (size_t i = 0; i < fileNums; i++)
    {
      gchar* groupName = g_strdup_printf("file%ld",i);
      gchar* name =  g_key_file_get_string(srvIni, groupName, "name", NULL);
      gchar* srcPath = g_strdup_printf("../project/%s",name);
      gchar* destPath =  g_key_file_get_string(srvIni, groupName, "path", NULL);
      copyFile(srcPath,destPath);
    }
    
}
//ctrl+c释放资源
void signalHandler(int signum){
    printf("Received Ctrl+C, cleaning up...\n");
    close(client_fd);
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
    //套接字、地址
    client_fd=socket(AF_INET,SOCK_STREAM,0);
    if(client_fd==-1){
        perror("ERROR socket");
        return -1;
    }
    int addr_size=sizeof(server_addr);
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=inet_addr(SERVER_IP);
    server_addr.sin_port=htons(PORT);
    //连接server
    int ret_con=connect(client_fd,(struct sockaddr*)&server_addr,addr_size);
    if(ret_con==-1){
        perror("ERROR connect");
        exit(0);
    }else{
        printf("connect success\n");
    }
    //从ini获取版本信息发送给服务端
    GKeyFile *keyFile = g_key_file_new();
    if(!g_key_file_load_from_file(keyFile,"../client.ini",G_KEY_FILE_NONE,NULL)){
      perror("load update.ini error!");
      return -1;
    }
    gchar *version = g_key_file_get_string(keyFile,"check","version",NULL);
    gchar *policy = g_key_file_get_string(keyFile,"update","policy",NULL);
    //发送检测更新消息
    Message *msg = calloc(1,sizeof(Message));
    msg->type = 0;
    msg->content = version;
    msg->length = strlen(msg->content);
    if (send_message(client_fd,msg)==-1)
    {
        perror("send version check message error!");
        return -1;
    }
    free(msg);
    while (1)
    {
        msg = receive_message(client_fd);
        if (msg == NULL)
        {
            continue;
        }
        else
        {
            printf("client MSG:type->%d,length->%d\n", msg->type, msg->length);
            if (msg->type == 0 && msg->length > 0) // 检测更新的回复
            {
                 //g_print ("client MSG:type->%d,length->%d,content->%s\n",msg->type,msg->length,msg->content);
                g_debug("recv version check message!");
                g_print("recv version check message!");
                char *file = msg->content;
                if (file == NULL)
                {
                    g_debug("this vesion is latest!");
                }
                else
                {
                    // 把获取的文件内容写到本地
                    writeFile("../server_update.ini", file, msg->length);
                    // 根据跟新策略来判断，如果是自动更新触发更新流程
                    if (g_strcmp0("auto", policy) == 0) // 自动更新
                    {
                        g_debug("prepare auto update!");
                        // 发送消息获取更新文件请求
                        Message *msgTmp = calloc(1, sizeof(Message));
                        msgTmp->type = 1;
                        msgTmp->content = "getUpdateFile";
                        msgTmp->length = strlen(msgTmp->content);
                        g_debug("start send upate msg");
                        g_print("start send upate msg\n");
                        if (send_message(client_fd, msgTmp) == -1)
                        {
                            perror("send get update file message error!");
                            return -1;
                        }
                        g_debug("end send upate msg!");
                    }
                    else
                    {
                        // 否则弹框让用户选择更新
                        g_debug("user select update!");
                    }
                }
            }
            // 服务端发来更新文件
            if (msg->type == 1)
            {
                g_print ("recv server newfile\n");
                g_debug("recv updata project");
                writeFile("../update.zip", msg->content, msg->length);
                // 进行完整性校验
                if (!checkMd5("../server_update.ini"))
                {
                    g_error("md5 verify error!");
                }
                // 进行解压缩替换
                g_debug("upzip updatezip start......");
                gboolean isunzip=unzip("../update.zip");
                g_debug("upzip updatezip end......");
                //如果替换成功，删除备份。否则进行恢复
                if(!isunzip){
                replaceFiles("../server_update.ini");
                g_debug("replaceFiles..end....");   
                }
                
                
            }
        }
    }

    return 0;
}
