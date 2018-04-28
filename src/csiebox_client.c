#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"
#include "hash.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
//#include <fcntl.h>
#include <errno.h>
#include <linux/inotify.h>
#include <unistd.h>
#include <dirent.h>
/*#include<iostream>
#include<unordered_map>
using namespace std;
*/

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE+16))

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);

int sync_meta(csiebox_client* client, char* pathname); //pathname is relative to cdir
int sync_file(csiebox_client* client,char* pathname);
int sync_hardlink(csiebox_client* client, char* src, char* target);
int remove_client(csiebox_client* client, char* pathname);
void inotify(csiebox_client* client);
void con_longest(csiebox_client* client, char longest[401]);
//read config file, and connect to server
void traverse(csiebox_client* client, int fd);
void dopath(csiebox_client* client, char path[401], char longest_path[401], int layer_count, int* longest, int fd);
int sym_link(csiebox_client* client, char* pathname);
void csiebox_client_init(
  csiebox_client** client, int argc, char** argv) {
  csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
  if (!tmp) {
    fprintf(stderr, "client malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_client));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = client_start(tmp->arg.name, tmp->arg.server);
  if (fd < 0) {
    fprintf(stderr, "connect fail\n");
    free(tmp);
    return;
  }
  tmp->conn_fd = fd;
  *client = tmp;
}


int sync_meta(csiebox_client* client, char* pathname){ //pathname is relative to cdir
    char relate_path[400];
    if(strcmp(pathname,".")==0 || strcmp(pathname,"..")==0)
        return 1;
    if(strlen(pathname)>=3 && pathname[0]=='.' && pathname[1] == '/' && pathname[2] == '.'){
        printf("ignore\n");
        return 1;
    }
/*
    strcpy(relate_path,client->arg.path);
    strcat(relate_path,"/");
*/
    strcpy(relate_path,pathname);

//    printf("pathname:%s\n",relate_path);

	csiebox_protocol_meta req;
	memset(&req,0,sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
	req.message.header.req.datalen = sizeof(req)-sizeof(req.message.header);
	req.message.body.pathlen = strlen(pathname);
	if(fstatat(AT_FDCWD,relate_path,&req.message.body.stat,AT_SYMLINK_NOFOLLOW)<0){
        fprintf(stderr,"fstatat failed\n");
    }

    //CSIEBOX_PROTOCOL_STATUS_MORE
    if(!S_ISDIR(req.message.body.stat.st_mode)){ 
//        printf("file into md5...\n");
	    md5_file(relate_path,req.message.body.hash);
//        printf("md5 complete\n");
    }

	if(!send_message(client->conn_fd, &req, sizeof(req))){
		printf("meta send fail\n");
		return 0;
	}	

	if(!send_message(client->conn_fd, pathname, strlen(pathname)))
        printf("path send fail\n");
	
    csiebox_protocol_header header;
	memset(&header,0,sizeof(header));

	if(recv_message(client->conn_fd,&header,sizeof(header))){
		if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
           header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META ){
            if(header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
			    printf("Successfully syn meta\n");
			    return 1;
            }
            else if(header.res.status == CSIEBOX_PROTOCOL_STATUS_MORE){
                printf("require more\n");
                int ret = 1;
                if(!(sync_file(client,pathname))){
                    printf("sync_file failed\n");
                    return 0;
                }
                else{
                    printf("sync_file success\n");
                    return 1;
                }
            }
        }
        else
            return 0;
    }
	return 0;
}

void traverse(csiebox_client* client, int fd){
    char path[401];
    memset(path,0,sizeof(path));
    strcpy(path,".");
    printf("path check:%s\n", path);
    char longest_path[401];
    int longest = 0;
    dopath(client, path, longest_path, 0, &longest, fd);
    printf("longest path:%s\n",longest_path);
    con_longest(client, longest_path);
    return;
}

void con_longest(csiebox_client* client, char longest[401]){
    char file_name[401];
    strcpy(file_name, "longestPath.txt");
    int fd = open(file_name, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    write(fd,longest,strlen(longest));
    close(fd);
    sync_file(client,file_name);
    sync_meta(client,file_name);
    return;
}

void dopath(csiebox_client* client, char path[401], char longest_path[401], int layer_count, int* longest, int fd){
    struct stat statbuf;
    struct dirent *dirp;
    DIR *dp;
    if(lstat(path, &statbuf) < 0){
        printf("stat error in dopath\n");
        return;
    }

    if(S_ISDIR(statbuf.st_mode) == 0){ /*not a dir*/
       switch(statbuf.st_mode & S_IFMT){
            case S_IFREG:
                if(statbuf.st_nlink == 1){
                    sync_file(client, path);
                }
                else{
//                  printf("may be a hard_link!!\n");
                    int find = 0;
                    int index = 0;
                    for(int i = 0 ; i < client->map_count;i++){
                        if(statbuf.st_ino == client->inode_map[i]){
                            find = 1;
                            index = i;
                            break;
                        }
                    }
                    if(find){
                        sync_hardlink(client,path,client->path_map[index]);
                    }
                    else{
//                      printf("first reference exceed 2 in this inode\n");
                        int count = client->map_count;
                        client->inode_map[count] = statbuf.st_ino;
                        strcpy( client->path_map[count], path);
                        client->map_count++;
                        sync_file(client, path);
                    }
                }
                if(layer_count >= *longest){
                    strcpy(longest_path, path);
                    *longest = layer_count;
                }
                break;
            case S_IFLNK:
  //            printf("symbolic link\n");
                sym_link(client, path);
                break;
            default:
                printf("not regular\n");
                break;
       }
       return;
    }
    else{
        if(layer_count >= *longest){
            strcpy(longest_path, path);
            *longest = layer_count;
        }
        int wd = inotify_add_watch(fd,path,IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
        strcpy(client->wd_map[wd],path);

        sync_file(client, path);
            //printf("fail to sync dir\n");
          //  return;
        //}
        int len = strlen(path);
        path[len++] = '/';
        path[len] = 0;

        if( (dp = opendir(path)) == NULL){
            printf("can't not read dir\n");
            return;
        }
        while( (dirp = readdir(dp)) !=NULL){
            if(strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..")==0)
                continue;
            strcpy(&path[len],dirp->d_name);
            printf("traverse %s\n", path);
            dopath(client,path,longest_path, layer_count+1, longest, fd);
        }
        path[len-1] = 0;
        sync_meta(client, path);
        if(closedir(dp) < 0){
            printf("can't close dir\n");
        }
    }
    return;
}
int IS_DIR(csiebox_client* client, char* pathname){
    char re_path[400];
    memset(re_path,0,sizeof(re_path));
/*
    strcpy(re_path,client->arg.path);
    strcat(re_path,"/");
*/
    strcpy(re_path,pathname);

    struct stat stat_buf;
    if( lstat(re_path,&stat_buf) < 0){
        fprintf(stderr,"fail to lstat in IS_DIR\n");
    }

    return S_ISDIR(stat_buf.st_mode);
}

int sym_link(csiebox_client* client, char* pathname){
    printf("in sym_link function\n");
    csiebox_protocol_symlink req;
    memset(&req,0,sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYM;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    
    char content[401];
    memset(content,0,sizeof(content));
    if(readlink(pathname,content,401)<0)
        fprintf(stderr,"readlink fail");
    req.message.body.exist_len = strlen(content);
    req.message.body.new_len = strlen(pathname);

    if(!send_message(client->conn_fd,&req,sizeof(req))){
        fprintf(stderr,"send fail\n");
        return 0;
    }
    
    if(!send_message(client->conn_fd,content,strlen(content))){
        fprintf(stderr,"sent fail");
    }

    if(!send_message(client->conn_fd, pathname, strlen(pathname))){
        fprintf(stderr,"sent fail\n");
        return 0;
    }

    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    if(recv_message(client->conn_fd, &header, sizeof(header))){
        if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
           header.res.op == CSIEBOX_PROTOCOL_OP_SYM &&
           header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
            printf("SYNC symbolic link succees\n");
            sync_meta(client, pathname);
            return 1;
        }
        else{
            printf("fail to sync symbolic link\n");
            return 0;
        }
    }
    return 0;
}

int sync_file(csiebox_client* client,char* pathname){
    if(strcmp(pathname,".")==0 || strcmp(pathname,"..")==0)
        return 1;
    if(strlen(pathname)>=3 && pathname[0]=='.' && pathname[1] == '/' && pathname[2] == '.'){
        //printf("ignore\n");
        return 1;
    }
    printf("in sync_file function\n");
    csiebox_protocol_file req;
    memset(&req,0,sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
    req.message.header.req.datalen = sizeof(req)-sizeof(req.message.header);

    char relative_path[400];
/*
    strcpy(relative_path,client->arg.path);
    strcat(relative_path,"/");
*/
    strcpy(relative_path,pathname);

    //printf("relative_path check:%s\n",relative_path);
    
    //directory
    if(IS_DIR(client,relative_path)){
        req.message.body.datalen = -1;
    
        if(!send_message(client->conn_fd,&req,sizeof(req))){
            fprintf(stderr,"send fail\n");
            return 0;
        }
        int filename_len = strlen(pathname);
        if(!send_message(client->conn_fd,&filename_len,sizeof(int))){
            fprintf(stderr,"send length fail\n");
            return 0;
        }

        if(!send_message(client->conn_fd,pathname,filename_len)){
            fprintf(stderr,"send path fail\n");
            return 0;
        }
        //sync_meta(client,relative_path);
        csiebox_protocol_header header;
        memset(&header,0,sizeof(header));

        if(recv_message(client->conn_fd,&header,sizeof(header))){
            if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_FILE &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
                printf("successfully sync file\n");
                sync_meta(client,relative_path);
                return 1;
            }
            else {
                printf("fail to sync file\n");
                return 0;
            }
        }
    }


    else{
    //read file
        char transmit[4001];
        memset(transmit,0,sizeof(transmit));

        int fd = open(relative_path,O_RDONLY);
        if(fd<0){
            fprintf(stderr,"faile to open %s\n",relative_path);
            return 0;
        }

        req.message.body.datalen = 0;
        while(read(fd,transmit,4001)>0){
            int length = sizeof(transmit);
            req.message.body.datalen += length;
        }

        //printf("transmit content:%s\n",transmit);

        close(fd);
    
        if(!send_message(client->conn_fd,&req,sizeof(req))){
            fprintf(stderr,"send fail\n");
            return 0;
        }
        int filename_len = strlen(pathname);
        if(!send_message(client->conn_fd,&filename_len,sizeof(int))){
            fprintf(stderr,"send length fail\n");
            return 0;
        }

    
        if(!send_message(client->conn_fd,pathname,filename_len)){
            fprintf(stderr,"send path fail\n");
            return 0;
           }
        if(req.message.body.datalen!=0){
            if(!send_message(client->conn_fd,transmit,sizeof(transmit))){
                fprintf(stderr,"transmit data fail\n");
                return 0;
            }
        }
        //sync_meta(client,relative_path);csiebox_protocol_header header;
        
        csiebox_protocol_header header;
        memset(&header,0,sizeof(header));

        if(recv_message(client->conn_fd,&header,sizeof(header))){
            if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
                header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_FILE &&
                header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
                printf("successfully sync file\n");
                sync_meta(client,relative_path);
                return 1;
            }
            else {
                printf("fail to sync file\n");
                return 0;
            }
        }
    }
        return 0;
}

int sync_hardlink(csiebox_client* client, char* src, char* target){
    printf("in sync hardlink function\n");
    //printf("src:%s target:%s\n", src, target);
    csiebox_protocol_hardlink req;
    memset(&req,0,sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
    req.message.header.req.datalen = sizeof(req)-sizeof(req.message.header);
    req.message.body.srclen = strlen(src);
    req.message.body.targetlen = strlen(target);

    if(!send_message(client->conn_fd,&req,sizeof(req))){
        fprintf(stderr,"send fail\n");
    }

    if(!send_message(client->conn_fd,src,strlen(src))){
        fprintf(stderr,"send fail\n");
        return 0;
    }
    if(!send_message(client->conn_fd,target,strlen(target))){
        fprintf(stderr,"send fail\n");
    }

    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    if(recv_message(client->conn_fd,&header,sizeof(header))){
        if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
           header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK  &&
           header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
            printf("sync hardlink successfully\n");
            sync_meta(client,src);
            return 1;
        }
        else{
            fprintf(stderr,"sync hardlink fail\n");
            return 0;
        }
    }
    return 0;
}

int remove_client(csiebox_client* client, char* pathname){
    csiebox_protocol_rm req;
    memset(&req,0,sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
    req.message.header.req.datalen = sizeof(req)-sizeof(req.message.header);
    req.message.body.pathlen = strlen(pathname);

    //printf("pathname:%s\n",pathname);
    
    if(!send_message(client->conn_fd, &req, sizeof(req))){
        printf("send fail\n");
    }

    if(!send_message(client->conn_fd, pathname, strlen(pathname))){
        printf("send path fail\n");
    }

    csiebox_protocol_header header;
    memset(&header,0,sizeof(header));
    if(recv_message(client->conn_fd,&header,sizeof(header))){
        if(header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
           header.res.op == CSIEBOX_PROTOCOL_OP_RM        &&
           header.res.status == CSIEBOX_PROTOCOL_STATUS_OK){
            printf("successfully delete");
            return 1;
        }
        else{
            printf("fail to delete");
            return 0;
        }
    }
    return 0;

}

void inotify(csiebox_client* client){
    int length, i = 0;
    int fd;
    int wd;
    char buffer[EVENT_BUF_LEN];
    memset (buffer,0,EVENT_BUF_LEN);
    fd = inotify_init();

    if(fd<0){
        perror("inotify_init");
    }
    traverse(client, fd);
    wd = inotify_add_watch(fd,".",IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
    strcpy(client->wd_map[wd], ".");
    while( (length = read(fd,buffer,EVENT_BUF_LEN)) > 0){
        i = 0;
        while(i < length){
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
           // printf("event: (%d, %d, %s)\ntype: ",event->wd,strlen(event->name),event->name);
            char event_path[401] = {0};
            strcpy(event_path, client->wd_map[event->wd]);
            strcat(event_path, "/");
            strcat(event_path,event->name);
            printf("event_path: %s\n", event_path);

            if(event->mask & IN_CREATE){
                printf("create\n");
                if(!sync_file(client,event_path)){
                    fprintf(stderr,"fail to sync file");
                }
                else{
                    //printf("sync file success\n");
                }
                if(!sync_meta(client,event_path)){
                    printf("fail to sync meta\n");
                }
                else{
                    //printf("create successfully\n");
                }
                if(event->mask & IN_ISDIR){
                    int wd_new = inotify_add_watch(fd, event_path, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);
                    strcpy(client->wd_map[wd_new],event_path);
                }
            }
            else if(event->mask & IN_ATTRIB || event->mask & IN_MODIFY){
                printf("attrib/modify = %d/%d\n",event->mask & IN_ATTRIB ,event->mask & IN_MODIFY);
                if(!sync_meta(client,event_path)){
                    fprintf(stderr,"sync_meta fail\n");
                }
                else{
                    //printf("sync_meta successfully\n");
                }
            }
            else if(event->mask & IN_DELETE){
                printf("delete\n");
                if(!remove_client(client,event_path)){
                    printf("remove fail\n");
                }
                /*else{
                    if(event->mask & IN_ISDIR){
                        del_from_hash(&(client->inotify_hash), (void**)event_path, event->wd);
                        inotify_remove_watch(fd,event->wd);
                    }
                }*/
            }
            printf("\n");
            i+= EVENT_SIZE + event->len;
        }
        memset(buffer,0,EVENT_BUF_LEN);
    }

    //inotify_rm_watch(fd,wd);
    close(fd);
    return;
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {
  if (!login(client)) {
    fprintf(stderr, "login fail\n");
    return 0;
  }
  fprintf(stderr, "login success\n");
  if(chdir(client->arg.path) < 0)
      fprintf(stderr, "chdir fail\n");
  inotify(client);

  //This is a sample function showing how to send data using defined header in common.h
  //You can remove it after you understand
  
  //====================
  //        TODO
  //====================
  
  
  return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
  csiebox_client* tmp = *client;
  *client = 0;
  if (!tmp) {
    return;
  }
  close(tmp->conn_fd);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
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
  int accept_config_total = 5;
  int accept_config[5] = {0, 0, 0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("name", key) == 0) {
      if (vallen <= sizeof(client->arg.name)) {
        strncpy(client->arg.name, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("server", key) == 0) {
      if (vallen <= sizeof(client->arg.server)) {
        strncpy(client->arg.server, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("user", key) == 0) {
      if (vallen <= sizeof(client->arg.user)) {
        strncpy(client->arg.user, val, vallen);
        accept_config[2] = 1;
      }
    } else if (strcmp("passwd", key) == 0) {
      if (vallen <= sizeof(client->arg.passwd)) {
        strncpy(client->arg.passwd, val, vallen);
        accept_config[3] = 1;
      }
    } else if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(client->arg.path)) {
        strncpy(client->arg.path, val, vallen);
        accept_config[4] = 1;
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

static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
