/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);


/* -------------routine------------*/
int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

  /* 
   * 들어온 인자 개수가 적절하지 않으면 
   * 에러 메세지와 함께 사용 가이드를 출력 
   */    
  if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* listen_fd 생성 */
    /* listenfd 식별자는 0, 1, 2 다음으로 최초로 생성되므로, 3! */
    listenfd = Open_listenfd(argv[1]);

    /* 무한 loop 돌면서 client의 connection request 대기 */
    while (1)
    {
        clientlen = sizeof(clientaddr);
        /* accept */
        /* connfd 식별자는 0, 1, 2, 3(listenfd) 다음에 생성되므로, 4! */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);  
        Close(connfd); 
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) 
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version); 

    /* 요청 method가 GET과 HEAD가 아니면 종료. main으로 가서 연결 닫고 다음 요청 기다림. */
    if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0))
    { 
        /* method 스트링이 GET or HEAD가 아니면 0이 아닌 값이 나옴 */
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0)
    { 
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");
        return;
    } // line:netp:doit:endnotfound

    /* 컨텐츠의 유형(정적, 동적)을 파악한 후 각각의 서버에 보낸다. */
    if (is_static)
    { 
        /* 정적 파일일 경우 */
        // !(일반 파일이다) or !(읽기 권한이 있다)
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't read the file");
            return;
        }

        // 정적 서버에 파일의 사이즈와 메서드를 같이 보낸다. -> Response header에 Content-length 위해!
        serve_static(fd, filename, sbuf.st_size, method);
    }
    else
    { 
        /* 동적 파일일 경우 */
        // !(일반 파일이다) or !(실행 권한이 있다)
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
        {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program");
            return;
        }

        // 동적 서버에 인자와 메서드를 같이 보낸다.
        serve_dynamic(fd, filename, cgiargs, method);
    }
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    { 
        /* EOF까지 read */
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "cgi-bin"))
    { /* 정적 파일일 때 */                 
        strcpy(cgiargs, "");               
        strcpy(filename, ".");             
        strcat(filename, uri);             
        if (uri[strlen(uri) - 1] == '/')   
            strcat(filename, "home.html"); 
        return 1;
    }
    else
    { /* 동적 파일일 때 */    
        ptr = index(uri, '?'); 
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, ""); 
        strcpy(filename, ".");   
        strcat(filename, uri);  
        return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client
 * mmap & rio_readn 사용 ver.
 */
/* $begin serve_static */
// void serve_static(int fd, char *filename, int filesize)
// {
//     int srcfd;
//     char *srcp, filetype[MAXLINE], buf[MAXBUF];

//     /* Send response headers to client */
//     get_filetype(filename, filetype);    //line:netp:servestatic:getfiletype
//     sprintf(buf, "HTTP/1.0 200 OK\r\n"); //line:netp:servestatic:beginserve
//     Rio_writen(fd, buf, strlen(buf));
//     sprintf(buf, "Server: Tiny Web Server\r\n");
//     Rio_writen(fd, buf, strlen(buf));
//     sprintf(buf, "Content-length: %d\r\n", filesize);
//     Rio_writen(fd, buf, strlen(buf));
//     sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
//     Rio_writen(fd, buf, strlen(buf));    //line:netp:servestatic:endserve

//     /* Send response body to client */
//     srcfd = Open(filename, O_RDONLY, 0); //line:netp:servestatic:open
//     srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //line:netp:servestatic:mmap
//     Close(srcfd);                       //line:netp:servestatic:close
//     Rio_writen(fd, srcp, filesize);     //line:netp:servestatic:write
//     Munmap(srcp, filesize);             //line:netp:servestatic:munmap
// }


/*
 * serve_static - copy a file back to the client
 * malloc & rio_readn & rio_writen 사용 ver.
 */
void serve_static(int fd, char *filename, int filesize, char *method)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client 클라이언트에게 응답 헤더 보내기*/
    /* 응답 라인과 헤더 작성 */
    get_filetype(filename, filetype);                   // 파일 타입 찾아오기
    sprintf(buf, "HTTP/1.0 200 OK\r\n");                // 응답 라인 작성
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); // 응답 헤더 작성
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

    /* 응답 라인과 헤더를 클라이언트에게 보냄 */
    Rio_writen(fd, buf, strlen(buf)); // connfd를 통해 clientfd에게 보냄
    printf("Response headers:\n");
    printf("%s", buf); // 서버 측에서도 출력한다.

    /* 만약 메서드가 HEAD라면, 응답 본체를 만들지 않고 끝낸다.*/
    if (strcasecmp(method, "HEAD") == 0)
        return;

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
    srcp = (char *)Malloc(filesize);     // 파일 크기만큼의 메모리를 동적할당한다.
    Rio_readn(srcfd, srcp, filesize);    // filename 내용을 동적할당한 메모리에 쓴다.
    Close(srcfd);                        // 파일을 닫는다.
    Rio_writen(fd, srcp, filesize);      // 해당 메모리에 있는 파일 내용들을 클라이언트에 보낸다(읽는다).
    free(srcp);
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4"))
        strcpy(filetype, "video/mp4");
    else if (strstr(filename, ".mpeg"))
        strcpy(filetype, "video/mpeg");
    else
        strcpy(filetype, "text/plain");
}
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0)
    { 
        /* Child */ 
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1); 
        /* 요청 메서드를 환경변수에 추가한다. */
        setenv("REQUEST_METHOD", method, 1);

        Dup2(fd, STDOUT_FILENO);                         /* Redirect stdout to client */    
        Execve(filename, emptylist, environ);            /* Run CGI program */
    }
    Wait(NULL);                                          /* Parent waits for and reaps child */ 
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */
