// /* $end adder */
// /*
//  * head-adder.c - a minimal CGI program that adds two numbers together
//  */
#include "../csapp.h"

// int main(void)
// {
//   char *buf, *p, *method;
//   char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
//   int n1 = 0, n2 = 0;

//   /* 인자 2개 추출하기 */
//   if ((buf = getenv("QUERY_STRING")) != NULL)
//   {
//     p = strchr(buf, '&');
//     *p = '\0';
//     strcpy(arg1, buf);
//     strcpy(arg2, p + 1);
//     n1 = atoi(arg1);
//     n2 = atoi(arg2);
//   }
//   /* 환경 변수로 넣어둔 요청 메서드를 확인한다. */
//   method = getenv("REQUEST_METHOD");

//   /* 응답 본체 만들기 */
//   sprintf(content, "Welcome to add.com: ");
//   sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
//   sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>",
//           content, n1, n2, n1 + n2);
//   sprintf(content, "%sThanks for visiting!\r\n", content);

//   /* 클라이언트에 보낼 응답 출력하기 */
//   printf("Connection: close\r\n");
//   printf("Content-length: %d\r\n", (int)strlen(content));
//   printf("Content-type: text/html\r\n\r\n");

//   /* 메서드가 HEAD가 아닐 경우만 응답 본체를 출력한다. */
//   if (strcasecmp(method, "HEAD") != 0)
//     printf("%s", content);

//   fflush(stdout);

//   exit(0);
// }
int main(void)
{
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL)
  {
    p = strchr(buf, '&');
    *p = '\0';
    sscanf(buf, "first=%d", &n1);
    sscanf(p + 1, "second=%d", &n2);
  }

  /* Make the response body */
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>",
          content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}