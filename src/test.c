#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<string.h>
#include<sys/stat.h>

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |S_IXOTH)
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

void process_path(char* path, char* cdir_path){
    strcpy(cdir_path,"../cdir/");
    strcat(cdir_path,path);
    return;
}

int main(int argc, char** argv){
    if(argc<3){
        fprintf(stderr,"wrong usage\n");
        return 0;
    }
    chdir("../cdir");
    char cdir_path[400];
    strcpy(cdir_path,argv[2]);
    //process_path(argv[2],cdir_path);
    printf("path %s\n",cdir_path);
    if(strcmp(argv[1],"creat")==0){
        int fd = open(cdir_path, (O_WRONLY | O_CREAT) ,REG_S_FLAG);
        if(fd<0)
            fprintf(stderr,"path not found\n");
        char buf[400] = "hello world hahaha";
        write(fd,buf,strlen(buf));
        close(fd);
        return 0;
    }
    else if(strcmp(argv[1],"mkdir")==0){
        mkdir(cdir_path,DIR_S_FLAG);
        return 0;
    }
    else if(strcmp(argv[1],"rm")==0){
        remove(cdir_path);
        return 0;
    }
    else if(strcmp(argv[1],"w")==0){
        int fd = open(cdir_path, O_WRONLY);
        if(fd<0)
            fprintf(stderr,"path not found\n");
        int length = strlen(argv[3]);
        if( write(fd,argv[3],length) < length )
            fprintf(stderr,"write fail\n");
        close(fd);
        return 0;
    }
    else if(strcmp(argv[1],"emp")==0){
        int fd = open(cdir_path,(O_RDONLY,O_CREAT),REG_S_FLAG);
        close(fd);
    }

    else if(strcmp(argv[1],"a")==0){
        int fd = open(cdir_path, O_WRONLY | O_APPEND);
        if(fd<0)
            fprintf(stderr,"path not found\n");
        int length = strlen(argv[3]);
        if( write(fd,argv[3],length) < length )
            fprintf(stderr,"write fail\n");
        close(fd);
        return 0;

    }

    else if(strcmp(argv[1], "ln-s") == 0 ){
        if(symlink(argv[2],argv[3])<0)
            fprintf(stderr,"fail to create symbolic link");
        return 0;
    }

    else{
        printf("no such command\n");
        printf("command: creat | mkdir | rm | w | a | emp\n");
        return 0;
    }

}
