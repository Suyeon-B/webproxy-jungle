#include <stdio.h>
#include "csapp.h"

// #define CONCURRENT                         // Part II: Dealing with multiple concurrent requests
// #define DEBUG                              // debug시 주석 제거


/* C언어 전처리기 활용 debug */
#ifndef DEBUG                                 // ifndef = if not define. 즉, DEBUG가 정의되어 있지 않다면
#define debug_printf(...) \                   
  {}                                          // debug_printf 부분이 출력되지 않도록 한다. "\(backslash)"는 줄바꿈의 의미
#else                                         // DEBUG가 정의되어 있다면,
#define debug_printf(...) printf(__VA_ARGS__) // debug_printf 부분이 출력되도록 한다.
#endif

/* Recommended max cache and object sizes */
// #define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* ------------ global var ------------ */
/* 코드 스타일 유지 & 간결한 표현을 위해 변수 설정 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n"; /* \r\n -> 서버에서 클라이언트로 전송할 때 분할전송 */
static const char *connection_hdr = "Connection: close\r\n";                 
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

// static const char *bad_request_response =
//     "HTTP/1.0 400 Bad Request\r\n\r\n<html><body>Bad "
//     "Request</body></html>\r\n\r\n";
static char *dns_error_response =
    "HTTP/1.0 500 Proxy Error\r\n\r\n<html><body>DNS "
    "Error</body></html>\r\n\r\n";
static char *sock_error_response =
    "HTTP/1.0 500 Proxy Error\r\n\r\n<html><body>Socket "
    "Error</body></html>\r\n\r\n";

typedef struct
{
  int port;
  char host[MAXLINE];
  char content[MAXLINE];
} HttpRequest;

/* -----------declare func------------- */
void sigpipe_handler(int sig);
void proxy(int connfd);
void *proxy_thread(void *vargp);
void read_requesthdrs(rio_t *rp);
int parse_uri(const char *uri, int *port, char *hostname, char *pathname);
int parse_http_request(rio_t *rio, HttpRequest *request);
int parse_http_host(const char *host_header, char *hostname, int *port);
void forward_http_request(int clientfd, HttpRequest *request);

/* -------------routine------------*/
/* 루틴이란? 어떤 작업을 정의한 명령어의 집합(그룹)을 의미 */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_in clientaddr;
  pthread_t tid; /* 멀티 쓰레드용 */
  int *connfdp;

  /* 들어온 인자 개수가 적절하지 않으면 알려줌 */
  if (argc != 2)
  {
    /*
     * 사용법을 에러메세지로 write
     * stderr는 버퍼 없이 바로 출력하기 때문에
     * 어떤 상황이 와도 가장 빠르게 에러 메세지를 출력할 수 있도록 fprintf & stderr 사용
     */
    fprintf(stderr, "Usage: %s <port>\n", argv[0]); // argv[0]은 ./proxy
    exit(1);
  }

  /*
   * 캐시 초기화
   * ? 캐시 방법은 모름 알아보기
   */
  // cache_init();

  /* set SIGPIPE handler func, no exiting */
  // signal(SIGPIPE, sigpipe_handler); /* ? 이게 머꼬 */

  /* 클라이언트 --------> 프록시(listenfd, connfd) */

  /* listen_fd 생성 */
  /* 듣기 소켓 식별자는 0, 1, 2 다음으로 최초로 생성되므로, 3! */
  listenfd = open_listenfd(argv[1]);

  /* 무한 loop 돌면서 client의 connection request 대기 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    /* accept */
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd < 0)
      continue; /* accept가 error 시 -1 리턴하므로 */
    /*
     * accept에서 clientaddr 채워와서,
     * 클라이언트의 hostname(IP) & port 정보를 얻어 버퍼로 복사한다.
     * 이 때 설정된 hostname과 port가 연결 성공 시, 아래 연결 성공 메세지가 출력된다.
     */
    getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

#ifndef CONCURRENT // Part I: Implementing a sequential web proxy
    printf("Accepted new connection from (%s, %s)\n", hostname, port);
    proxy(connfd);
    Close(connfd);
#else              // Part II: Dealing with multiple concurrent requests
    debug_printf("New Thread\n");
    printf("Accepted new connection from (%s, %s)\n", hostname, port);
    connfdp = malloc(sizeof(int));
    *connfdp = connfd;
    if (0 != pthread_create(&tid, NULL, proxy_thread, connfdp))
    {
      debug_printf("create new thread [%d] failed", tid);
      free(connfdp);
    }
#endif
  }
  // cache_destroy(); /* ? 캐시 부분은 아직 안 건드림 */
  return 0;
}

// void sigpipe_handler(int sig)
// {
//   printf("SIGPIPE handled!\n");
//   return;
// }

void proxy(int connfd)
{
  rio_t fromcli_rio, toserv_rio;
  HttpRequest request;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], path[MAXLINE];
  int port, toserverfd, irespond;
  char toserverport[MAXLINE];
  char toserverreq[MAXLINE];

  /* 클라이언트 <-> 프록시 서버 */
  /* 클라이언트의 요청을 받거나, 엔드 서버의 응답을 클라이언트로 전달 */
  /* client ---(request)---> (connfd)proxy server */
  /*        <--(response)---                      */
  rio_readinitb(&fromcli_rio, connfd);
  if (parse_http_request(&fromcli_rio, &request) == -1)
  {
    // rio_writen(connfd, bad_request_response, strlen(bad_request_response));
    return;
  }

  /* debug용 - client의 host와 port를 출력 */
  debug_printf("Host: %s, Port: %d\n", request.host, request.port);

  /* 프록시 서버 <-> 엔드 서버 통신 */
  /* 클라이언트의 요청을 엔드 서버로 전달함 */
  /* proxy ----(request)----> server */
  /*       <---(response)----        */
  forward_http_request(connfd, &request);
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  rio_readlineb(rp, buf, MAXLINE);
  while (strcasecmp(buf, "\r\n"))
  {
    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(const char *uri, int *port, char *hostname, char *pathname)
{
  /* uri : http://www.google.com:80/index.html or /index.html */
  /* Remove protocal part */
  const char *url = strncmp(uri, "http://", 7) == 0 ? uri + 7 : uri;
  
  if (strchr(url, ':'))                                                   // ':'문자가 처음으로 등장하는 곳 다음의 포인터를 리턴 
  {
    sscanf(url, "%[^:]:%i%s", hostname, port, pathname);                  // 정규 표현식으로 ':'으로 구분된 지점을 나누어 hostname, port, pathname 구함
  }
  else
  {
    *port = 80;
    sscanf(url, "%[^/]%s", hostname, pathname);
  }

  /* if Path is NULL */
  if (pathname == NULL)
    strcpy(pathname, "/");

  return 0;
}


/*
 * client---(request)--->proxy
 * parse "request (GET / HTTP/1.1 ...)" into specific "request".
 */
int parse_http_request(rio_t *rio, HttpRequest *request)
{
  char line[MAXLINE], version[64], method[64], uri[MAXLINE], path[MAXLINE];
  size_t rc;
  rc = 0;
  request->port = 80; /* HTTP 기본 포트 */
  rio_readlineb(rio, line, MAXLINE);
  /* request header : GET http://www.google.com:80/index.html or /index.html
   * HTTP/1.1 */
  /* line에서 뽑아서 */
  sscanf(line, "%s %s %s", method, uri, version); /* 버전은 1.1 */

  if (strcasecmp(method, "GET") != 0)
  {
    printf("Error: %s is not supported!\n", method);
    return -1;
  }
  debug_printf("Request from client: >---------%s", line); /* ifndef DEBUG */
  /* URI 파싱 : host(client IP), port(client port) */
  parse_uri(uri, &(request->port), (char *)&(request->host), path);
  /* HTTP/1.1 -> HTTP/1.0 ? 왱? */
  sprintf(request->content, "GET %s HTTP/1.0\r\n", path); /* 여기서 path는 /index.html 같은 거 /{filename} */

  /* 다음 헤더내용 한 줄씩 read */
  while ((rc = rio_readlineb(rio, line, MAXLINE)) > 0)
  {
    /* http request header 마지막 줄은 \r\n */
    if (strcmp(line, "\r\n") == 0)
    {
      strcat(request->content, line);
      break;
    }
    else if (strstr(line, "Host:"))
    {
      strcat(request->content, line);
      // Host: 192.168.1.1:8000
      parse_http_host(line, (char *)&(request->host), &(request->port));
    }
    else if (strstr(line, "Connection:"))
    {
      strcat(request->content, connection_hdr);
    }
    else if (strstr(line, "User-Agent:"))
    {
      strcat(request->content, user_agent_hdr);
    }
    else if (strstr(line, "Proxy-Connection:"))
    {
      strcat(request->content, proxy_connection_hdr);
    }
    else
    {
      /* others */
      strcat(request->content, line);
    }
  }
  if (rc < 0)
  {
    printf("Error when reading request!\n");
    return -1;
  }
  return 0;
}

/*

build_http_header

proxy <-- request -- end server
 */

/*
Pthread_detach(pthread_self());

스레드가 종료되면 스택에서 썼던 걸(공유자원이 아닌 것) 반납함
종료될 때 까지 기다림! 바로 삭제가 아니고.
 */

/* Host: 192.168.1.1:8000\r\n  get host and port */
int parse_http_host(const char *host_header, char *hostname, int *port)
{
  char port_str[8];
  char *host_begin = (char *)(host_header + 6); // skip Host:
  char *host_end = strstr(host_header, "\r\n");
  char *port_begin = strchr(host_begin, ':');
  size_t port_len;
  if (port_begin == NULL)
    strncpy(hostname, host_begin, (size_t)(host_end - host_begin));
  else
  {
    port_len = (size_t)(host_end - port_begin) - 1;
    strncpy(hostname, host_begin, (size_t)(port_begin - host_begin));
    strncpy(port_str, port_begin + 1, port_len);
    port_str[port_len] = '\0';
    *port = atoi(port_str);
  }
  return 0;
}

/*
 * client <------(response)------ [connfd] proxy [serverfd]
 * ----(request)---->server
 */
void forward_http_request(int connfd, HttpRequest *request)
{
  int serverfd, object_size, n;
  char buf[MAXLINE], response_from_server[MAX_OBJECT_SIZE], port_str[8];
  rio_t toserver_rio;
  debug_printf("Request to server: \n---------\n%s", request->content);

  /*
   * If the cache has the request's response,
   * just write it back to connfd.
   * If not , to proxy ---> server, server--->proxy,proxy--->client.
   * And put the new request-response into the cache.
   */

  /* if (cache_get(request->content, response_from_server)) {
    debug_printf("Hit response in the cache!\n");
    rio_writen(connfd, response_from_server, strlen(response_from_server));
    return;
  } */

  sprintf(port_str, "%d", request->port);
  serverfd = open_clientfd(request->host, port_str);
  if (serverfd == -1)
  {
    /* create socket error */
    /* response the info to client */
    rio_writen(connfd, sock_error_response, strlen(sock_error_response));
    return;
  }
  else if (serverfd == -2)
  {
    /* DNS error */
    rio_writen(connfd, dns_error_response, strlen(dns_error_response));
    return;
  }

  /* proxy[serverfd] -----(request(from client)) ----> server */
  rio_readinitb(&toserver_rio, serverfd);
  rio_writen(serverfd, request->content, strlen(request->content));

  object_size = 0;
  while ((n = rio_readlineb(&toserver_rio, buf, MAXLINE)) > 0)
  {
    object_size += n;
    if (object_size <= MAX_OBJECT_SIZE)
      strcat(response_from_server, buf);
    rio_writen(connfd, buf, n);
  }

  /* proxy[serverfd] <--------response------- server */
  debug_printf("Response from server :\n-----------\n%s", response_from_server);

  /* Put the new request-respond into the cache */
  /* if (object_size <= MAX_OBJECT_SIZE)
    cache_place(request->content, response_from_server); */
  close(serverfd);
}