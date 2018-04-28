#include "csiebox_server.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

//read config file, and start to listen
void csiebox_server_init(
  csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)
      malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;
  *server = tmp;
}

//wait client to connect and handle requests from connected socket fd
int csiebox_server_run(csiebox_server* server) {
  int conn_fd, conn_len;
  struct sockaddr_in addr;
  while (1) {
    memset(&addr, 0, sizeof(addr));
    conn_len = 0;
    // waiting client connect
    conn_fd = accept(
      server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
    if (conn_fd < 0) {
      if (errno == ENFILE) {
          fprintf(stderr, "out of file descriptor table\n");
          continue;
        } else if (errno == EAGAIN || errno == EINTR) {
          continue;
        } else {
          fprintf(stderr, "accept err\n");
          fprintf(stderr, "code: %s\n", strerror(errno));
          break;
        }
    }
    // handle request from connected socket fd
    handle_request(server, conn_fd);
    printf("next request\n");
  }
  return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  close(tmp->listen_fd);
  int i = getdtablesize() - 1;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
  if (argc != 2) {
    return 0;
  }
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 2;
  int accept_config[2] = {0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(server->arg.path)) {
        strncpy(server->arg.path, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("account_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.account_path, val, vallen);
        accept_config[1] = 1;
      }
    }
  }
  free(key);
  free(val);
  fclose(file);
  int i, test = 1;
  for (i = 0; i < accept_config_total; ++i) {
    test = test & accept_config[i];
  }
  if (!test) {
    fprintf(stderr, "config error\n");
    return 0;
  }
  return 1;
}

int sync_meta(csiebox_server* server,int conn_fd, csiebox_protocol_meta* meta){
    printf("In sync_meta_function\n");
    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));

    char buf[400];
    memset(buf,0,sizeof(buf));
    recv_message(conn_fd,buf,meta->message.body.pathlen);
    char pathname[400];
    //strcpy(pathname,"../sdir/");
    //strcat(pathname,server->client[conn_fd]->account.user);
    //strcat(pathname,"/");
    strcpy(pathname,buf);

    printf("path: %s\n",pathname);
    // stat_file/dir
    struct stat stat_server;
    if(fstatat(AT_FDCWD,pathname,&stat_server,AT_SYMLINK_NOFOLLOW)<0){
        printf("fail to fstatat");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    }
    else{
        printf("successfully fstatat\n");
    }
    //permission

    //mode
    if(chmod(pathname,meta->message.body.stat.st_mode)<0){
        printf("fchmodat error for %s\n",pathname);
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    }
    else 
        printf("chmod successfully\n");    
    //owner
    //time
    struct timespec times[2];
    times[0] = meta->message.body.stat.st_atim;
    times[1] = meta->message.body.stat.st_mtim;
    if(utimensat(AT_FDCWD,pathname,times,AT_SYMLINK_NOFOLLOW) < 0){
        printf("utimesat fail for %s\n",pathname);
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    }
    else 
        printf("utimenstat successfully\n");
    
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
    header.res.datalen = 0;

    //CSIEBOX_PROTOCOL_STATUS_MORE
    if(!S_ISDIR(stat_server.st_mode)){
        uint8_t hash[MD5_DIGEST_LENGTH];
        memset(&hash,0,sizeof(hash));
        md5_file(pathname,hash);
        if(memcmp(hash, meta->message.body.hash, sizeof(hash))!=0){
            printf("md5 not equal\n");
            header.res.status = CSIEBOX_PROTOCOL_STATUS_MORE;
        }
        else
            header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    }
    else{
        header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    }
    if(!send_message(conn_fd,&header,sizeof(header))){
        printf("sync_meta send faile\n");
        return 0;
    }
    printf("sucessful\n\n");
    return 1;
}

int sync_file(csiebox_server* server, int conn_fd, csiebox_protocol_file* file){
    
    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));

    printf("in sync_file function\n");
    //receive pathname
    int length;
    recv_message(conn_fd,&length,sizeof(int));
    printf("lengtn %d\n",length);
    char pathname[400];
    memset(pathname,0,sizeof(pathname));
    if(!recv_message(conn_fd,pathname,length))
        printf("receive fail\n");
    printf("pathname check:%s\n",pathname);

    char relative_path[400];
    memset(relative_path,0,sizeof(relative_path));
    //strcpy(relative_path,server->arg.path);
    //strcat(relative_path,"/");
    //strcat(relative_path,server->client[conn_fd]->account.user);
    //strcat(relative_path,"/");
    strcpy(relative_path,pathname);
    printf("path check:%s\n",relative_path);


    //directory
    if(file->message.body.datalen == -1){
        mkdir(relative_path,DIR_S_FLAG);
    }

    //file
    else{
        int fd;
        fd = open(relative_path,(O_WRONLY | O_CREAT | O_TRUNC),REG_S_FLAG);
        if(fd<0){
            printf("open %s failed\n",relative_path);
            header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
        }
        else if(file->message.body.datalen != 0){
            char content[4001];
            memset(content,0,sizeof(content));
            recv_message(conn_fd,content,file->message.body.datalen);
            write(fd,content,strlen(content));
            fputs(content,stdout);
            printf("\n");
            close(fd);
        }
        else
            close(fd);
    }
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
    header.res.datalen = 0;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    if( !send_message(conn_fd,&header,sizeof(header) ) ){
        fprintf(stderr, "send fail\n");
        return 0;
    }

    printf("sucessful\n\n");
    return 1;
}

int remove_server(csiebox_server* server, int conn_fd, csiebox_protocol_rm* RM){
    printf("In remove function\n");
    char buf[400];
    memset(buf,0,sizeof(buf));
    recv_message(conn_fd,buf,RM->message.body.pathlen);
    printf("pathcheck:%s\n",buf);
    char relative_path[400];
    //strcpy(relative_path,server->arg.path);
    //strcat(relative_path,"/");
    //strcat(relative_path,server->client[conn_fd]->account.user);
    //strcat(relative_path,"/");
    strcpy(relative_path,buf);
    printf("relative_path check:%s\n",relative_path);

    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.op = CSIEBOX_PROTOCOL_OP_RM;
    header.res.datalen = 0;

    if(remove(relative_path)==0){
        printf("remove successfully\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_OK; 
    }
    else{
        printf("remove fail\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    }
    if(!send_message(conn_fd,&header,sizeof(header))){
        fprintf(stderr,"send fail");
        return 0;
    }
    printf("sucessful\n\n");
    return 1;
}

/*void process_path(csiebox_server* server, int conn_fd, char* path, char* relative_path){
    strcpy(relative_path,server->arg.path);
    strcat(relative_path,"/");
    strcat(relative_path,server->client[conn_fd]->account.user);
    strcat(relative_path,"/");
    strcat(relative_path,path);
    return;
}*/

int sync_hardlink(csiebox_server* server,int conn_fd, csiebox_protocol_hardlink* hardlink){
    printf("In sync_hardlink function\n");
    char src_buf[400];
    memset(src_buf,0,sizeof(src_buf));
    char target_buf[400];
    memset(target_buf,0,sizeof(target_buf));
    if(recv_message(conn_fd,src_buf,hardlink->message.body.srclen))
        printf("src path check:%s\n",src_buf);
    else{
        printf("fail to receive\n");
    }
    if(recv_message(conn_fd,target_buf,hardlink->message.body.targetlen))
        printf("targer path check:%s\n",target_buf);
    else
        printf("fail to receive\n");
    char relative_src_path[400];
   // process_path(server, conn_fd,src_buf,relative_src_path);
    strcpy(relative_src_path, src_buf);
    char relative_target_path[400];
    //process_path(server, conn_fd,target_buf,relative_target_path);
    strcpy(relative_target_path, target_buf);

    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
    header.res.datalen = 0;

    if(link(relative_target_path,relative_src_path)==0){
        printf("create hardlink successfully\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    }
    else{
        printf("fail to create hardlink\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    }

    if(!send_message(conn_fd, &header, sizeof(header))){
        fprintf(stderr,"send fail\n");
    }

    return 1;
}

int sym_link(csiebox_server* server,int conn_fd, csiebox_protocol_symlink* sym){
    printf("in sym_link fumction\n");
    char exist_path[401];
    memset(exist_path,0,sizeof(exist_path));
    recv_message(conn_fd, exist_path, sym->message.body.exist_len);

    char new_path[401];
    memset(new_path,0,sizeof(new_path));
    recv_message(conn_fd, new_path, sym->message.body.new_len);

    printf("new path: %s point to exist_path:%s\n",new_path,exist_path);
    int success = 1;
    if(symlink(exist_path, new_path)<0){
        success = 0;
    }
    
    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
    header.res.op = CSIEBOX_PROTOCOL_OP_SYM;
    header.res.datalen = 0;
    if(success){
        printf("successfully\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    }
    else
        header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    if(!send_message(conn_fd, &header, sizeof(header))){
        fprintf(stderr,"send fail\n");
        return 0;
    }
    
    return 1;
}

//this is where the server handle requests, you should write your code here
static void handle_request(csiebox_server* server, int conn_fd) {
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  while (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      continue;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_LOGIN:
        fprintf(stderr, "login\n");
        csiebox_protocol_login req;
        if (complete_message_with_header(conn_fd, &header, &req)) {
          login(server, conn_fd, &req);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_META:
        fprintf(stderr, "sync meta\n");
        csiebox_protocol_meta meta;
        if (complete_message_with_header(conn_fd, &header, &meta)) {
          
        //This is a sample function showing how to send data using defined header in common.h
        //You can remove it after you understand
        //sampleFunction(conn_fd, &meta);
          sync_meta(server,conn_fd,&meta);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
        fprintf(stderr, "sync file\n");
        csiebox_protocol_file file;
        if (complete_message_with_header(conn_fd, &header, &file)) {
            sync_file(server,conn_fd,&file);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\n");
        csiebox_protocol_hardlink hardlink;
        if (complete_message_with_header(conn_fd, &header, &hardlink)) {
            sync_hardlink(server, conn_fd, &hardlink);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "sync end\n");
        csiebox_protocol_header end;
          //====================
          //        TODO
          //====================
        break;
      case CSIEBOX_PROTOCOL_OP_RM:
        fprintf(stderr, "rm\n");
        csiebox_protocol_rm rm;
        if (complete_message_with_header(conn_fd, &header, &rm)) {
            remove_server(server,conn_fd,&rm);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYM:
        fprintf(stderr,"sym link\n");
        csiebox_protocol_symlink sym;
        if(complete_message_with_header(conn_fd, &header, &sym)){
            sym_link(server,conn_fd,&sym);
        }
        break;
      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
        break;
    }
  }
  fprintf(stderr, "end of connection\n");
  logout(server, conn_fd);
}

//open account file to get account information
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info) {
  FILE* file = fopen(server->arg.account_path, "r");
  if (!file) {
    return 0;
  }
  size_t buflen = 100;
  char* buf = (char*)malloc(sizeof(char) * buflen);
  memset(buf, 0, buflen);
  ssize_t len;
  int ret = 0;
  int line = 0;
  while ((len = getline(&buf, &buflen, file) - 1) > 0) {
    ++line;
    buf[len] = '\0';
    char* u = strtok(buf, ",");
    if (!u) {
      fprintf(stderr, "illegal form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "illegal form in account file, line %d\n", line);
        continue;
      }
      md5(passwd, strlen(passwd), info->passwd_hash);
      ret = 1;
      break;
    }
  }
  free(buf);
  fclose(file);
  return ret;
}

//handle the login request from client
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
  int succ = 1;
  csiebox_client_info* info =
    (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
  memset(info, 0, sizeof(csiebox_client_info));
  if (!get_account_info(server, login->message.body.user, &(info->account))) {
    fprintf(stderr, "cannot find account\n");
    succ = 0;
  }
  if (succ &&
      memcmp(login->message.body.passwd_hash,
             info->account.passwd_hash,
             MD5_DIGEST_LENGTH) != 0) {
    fprintf(stderr, "passwd miss match\n");
    succ = 0;
  }

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    if (server->client[conn_fd]) {
      free(server->client[conn_fd]);
    }
    info->conn_fd = conn_fd;
    server->client[conn_fd] = info;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
    char* homedir = get_user_homedir(server, info);
    mkdir(homedir, DIR_S_FLAG);
    chdir(homedir);
    char work[400];
    getcwd(work,401);
    printf("working directory now %s\n",work);
    free(homedir);
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));
}

static void logout(csiebox_server* server, int conn_fd) {
  free(server->client[conn_fd]);
  server->client[conn_fd] = 0;
  close(conn_fd);
}

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

