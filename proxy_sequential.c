#include <stdio.h>
#include "csapp.h"

/* 
 * < proxy_sequential.c >
 * Proxy Lab - Part I: Implementing a sequential web proxy 
 */


// #define CONCURRENT                             // 주석 처리시 sequential proxy
// #define DEBUG                                  // 주석 처리시 debug X

/* C언어 전처리기 활용 debug printf */
#ifndef DEBUG                                 // ifndef = if not define. 즉, DEBUG가 정의되어 있지 않다면
#define debug_printf(...) \
     {}                                       // debug_printf 부분이 출력되지 않도록 한다. "\(backslash)"는 줄바꿈의 의미
#else                                         // DEBUG가 정의되어 있다면,
#define debug_printf(...) printf(__VA_ARGS__) // debug_printf 부분이 출력되도록 한다.
#endif

/* Recommended max object sizes */
#define MAX_OBJECT_SIZE 102400

/* ------------ global var ------------ */
/* 코드 스타일 유지 & 간결한 표현을 위해 변수 설정 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

static const char *requestline_hdr_format = "GET %s HTTP/1.1\r\n";
static const char *endof_hdr = "\r\n";

/* error reponses */
static const char *bad_request_response =
    "HTTP/1.0 400 Bad Request\r\n\r\n<html><body>Bad "
    "Request</body></html>\r\n\r\n";
static char *dns_error_response =
    "HTTP/1.0 500 Proxy Error\r\n\r\n<html><body>DNS "
    "Error</body></html>\r\n\r\n";
static char *sock_error_response =
    "HTTP/1.0 500 Proxy Error\r\n\r\n<html><body>Socket "
    "Error</body></html>\r\n\r\n";

/* 클라이언트의 요청 정보를 담을 구조체 */
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
int parse_uri(const char *uri, int *port, char *hostname, char *pathname);
int parse_http_request(rio_t *rio, HttpRequest *request);
int parse_http_host(const char *host_header, char *hostname, int *port);
void forward_http_request(int clientfd, HttpRequest *request);

/* -------------routine------------*/
/* 루틴이란? 어떤 작업을 정의한 명령어(or 함수)의 집합을 의미 */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_in clientaddr;
  int *connfdp;

  /* 
   * 들어온 인자 개수가 적절하지 않으면 
   * 에러 메세지와 함께 사용 가이드를 출력 
   */
  if (argc != 2)
  {
    /*
     * 사용법을 에러메세지로 write.
     * stderr는 버퍼 없이 바로 출력하기 때문에
     * 어떤 상황이 와도 가장 빠르게 에러 메세지를 출력할 수 있도록 fprintf & stderr 사용
     */
    fprintf(stderr, "Usage: %s <port>\n", argv[0]); // argv[0]은 ./proxy or ./tiny
    exit(1);
  }

  /* client [connfd] --------> [listenfd] proxy server */
  /* listen_fd 생성 */
  /* listenfd 식별자는 0, 1, 2 다음으로 최초로 생성되므로, 3! */
  listenfd = open_listenfd(argv[1]);

  /* 무한 loop 돌면서 client의 connection request 대기 */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    /* accept */
    /* connfd 식별자는 0, 1, 2, 3(listenfd) 다음에 생성되므로, 4! */
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd < 0)
      continue; /* accept가 error 시 -1 리턴하므로 */

    /*
     * accept에서 clientaddr 채워와서,
     * 클라이언트의 hostname(IP) & port 정보를 얻어 버퍼로 복사한다.
     * 이 때 설정된 hostname과 port가 연결 성공 시, 아래 연결 성공 메세지가 출력된다.
     */
    getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

/* Part I: Implementing a sequential web proxy */
#ifndef CONCURRENT
    printf("Accepted new connection from (%s, %s)\n", hostname, port);
    proxy(connfd);
    Close(connfd);
/* Part II: Dealing with multiple concurrent requests */
#else
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
  return 0;
}

/* 
 * client <-> proxy server <-> end server
 * 전반적인 프록시 서버 기능 메인 함수
 */
void proxy(int connfd)
{
  rio_t fromcli_rio;
  HttpRequest request;

  /* client ---(request)---> (connfd)proxy server */
  /* 클라이언트에서 프록시 서버로 요청 */
  rio_readinitb(&fromcli_rio, connfd);
  if (parse_http_request(&fromcli_rio, &request) == -1)
  {
    /* HTTP request 파싱에 실패했으면 에러 메세지 띄움 */
    rio_writen(connfd, bad_request_response, strlen(bad_request_response));
    return;
  }

  /* ifndef DEBUG - client의 host와 port를 출력 */
  debug_printf("Host: %s, Port: %d\n", request.host, request.port);

  /* proxy ----(request)----> server */
  /*       <---(response)----        */
  /* client <---(response)--- proxy */
  /* 클라이언트의 요청을 엔드 서버로 전달하고, 엔드 서버의 응답을 클라이언트로 전달 */
  forward_http_request(connfd, &request);
}

/* 
 * uri -> host(IP), port, path 파싱
 */
int parse_uri(const char *uri, int *port, char *hostname, char *pathname)
{
  /* uri : http://54.85.138.98:8000/home.html or /home.html */
  /* 프로토콜 부분 삭제 */
  const char *url = strncmp(uri, "http://", 7) == 0 ? uri + 7 : uri;

  /* 
   * ':' 문자가 있음 -> 포트번호가 있음 
   * ':'문자가 처음으로 등장하는 곳 다음의 포인터를 리턴
   */
  if (strchr(url, ':'))
  {
    sscanf(url, "%[^:]:%i%s", hostname, port, pathname); // 정규 표현식으로 ':'으로 구분된 지점을 나누어 hostname, port, pathname 파싱
  }
  else
  {
    /* 포트번호가 없을 때 */
    *port = 80;
    sscanf(url, "%[^/]%s", hostname, pathname);
  }

  /* 파일 경로가 없으면 root로 지정 */
  if (pathname == NULL)
    strcpy(pathname, "/");

  return 0;
}

/*
 * client의 request 메세지 파싱
 */
int parse_http_request(rio_t *rio, HttpRequest *request)
{
  char line[MAXLINE], version[64], method[64], uri[MAXLINE], path[MAXLINE];
  size_t rc;
  rc = 0;
  request->port = 80; /* HTTP 기본 포트 */
  rio_readlineb(rio, line, MAXLINE);
  /*
   * line에서 뽑아서 채움
   * ↓↓↓ example ↓↓↓
   * line : GET /home.html HTTP/1.1
   * method : GET
   * uri : /home.html
   * version : HTTP/1.1
   */
  sscanf(line, "%s %s %s", method, uri, version);

  /* GET 만 지원 */
  if (strcasecmp(method, "GET") != 0)
  {
    printf("Error: %s is not supported!\n", method);
    return -1;
  }
  debug_printf("Request from client: >---------%s", line); /* ifndef DEBUG */
  /* URI 파싱 : host(client IP), port(client port), path(file path) */
  parse_uri(uri, &(request->port), (char *)&(request->host), path);

  /* request line */
  sprintf(request->content, requestline_hdr_format, path); /* 여기서 path는 /index.html 같은 거 /{filename} */

  /* 다음 헤더내용 한 줄씩 read */
  while ((rc = rio_readlineb(rio, line, MAXLINE)) > 0)
  {
    /* http request header 마지막 줄은 \r\n */
    if (strcmp(line, endof_hdr) == 0)
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
  if (rc < 0) /* 읽자마자 EOF */
  {
    printf("Error when reading request!\n");
    return -1;
  }
  return 0;
}

/* 
 * HTTP host 파싱 -> host(IP)와 port 분리
 * ↓↓↓ example ↓↓↓
 * HTTP host: 54.85.138.98:8000\r\n  
 * host : 54.85.138.98
 * port : 8000
 */
int parse_http_host(const char *host_header, char *hostname, int *port)
{
  char port_str[8];
  char *host_begin = (char *)(host_header + 6); // skip 'Host:'
  char *host_end = strstr(host_header, "\r\n");
  char *port_begin = strchr(host_begin, ':');
  size_t port_len;
  if (port_begin == NULL) /* 포트 번호 없을 때 */
    strncpy(hostname, host_begin, (size_t)(host_end - host_begin));
  else
  {
    /* 포트 번호 있을 때 */
    port_len = (size_t)(host_end - port_begin) - 1;
    strncpy(hostname, host_begin, (size_t)(port_begin - host_begin));
    strncpy(port_str, port_begin + 1, port_len);
    port_str[port_len] = '\0';
    *port = atoi(port_str);
  }
  return 0;
}

/*
 * proxy -> end server로의 요청 전달과 응답의 전달(forward)을 담당하는 함수
 * client                   [connfd] proxy [serverfd] ----(request)---->server
 *       <----(response)----                          <----(response)----
 */
void forward_http_request(int connfd, HttpRequest *request)
{
  int serverfd, object_size, n;
  char buf[MAXLINE], response_from_server[MAX_OBJECT_SIZE], port_str[8];
  rio_t toserver_rio;
  debug_printf("Request to server: \n---------\n%s", request->content); /* ifndef DEBUG */

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
  /* serverfd 식별자는 0, 1, 2, 3(listenfd), 4(connfd) 다음에 생성되므로, 5! */
  serverfd = open_clientfd(request->host, port_str);
  /* 에러 시 클라이언트 측에 메세지 출력 - socket 생성 실패 or getaddrinfo 실패 */
  if (serverfd == -1)
  {
    /* socket 생성 실패 */
    rio_writen(connfd, sock_error_response, strlen(sock_error_response));
    return;
  }
  else if (serverfd == -2)
  {
    /* getaddrinfo 실패 */
    rio_writen(connfd, dns_error_response, strlen(dns_error_response));
    return;
  }

  /* proxy[serverfd] -----(request(from client)) ----> server */
  /* proxy [serverfd] ----(request)---->server */
  rio_readinitb(&toserver_rio, serverfd);
  rio_writen(serverfd, request->content, strlen(request->content));

  object_size = 0;
  while ((n = rio_readlineb(&toserver_rio, buf, MAXLINE)) > 0)
  {
    object_size += n;
    if (object_size <= MAX_OBJECT_SIZE)
      /* proxy[serverfd] <----(response)---- server */
      strcat(response_from_server, buf);

    /* client <----(response)---- [connfd] proxy */
    rio_writen(connfd, buf, n);
  }

  debug_printf("Response from server :\n-----------\n%s", response_from_server); /* ifndef DEBUG */
  close(serverfd);
}