#include "cache.h"
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* ------------ global var ------------ */
/* 
 * mutex: 한 번에 하나의 스레드만 캐시 정보를 교환할 수 있음
 * w: 변수값 변경도 한 writer만 허용
 * u: 캐시 순회 시에 한 번에 하나의 스레드만 사용하며, LRU노드(제일 최근에 방문한 노드)는 헤드에 위치해야 함
 */
sem_t mutex, w, u; 
int readcnt;
static cache_t *g_cache;


/* ------------ routine ------------ */
void cache_init() {
  g_cache = calloc(1, sizeof(cache_t));
  Sem_init(&mutex, 0, 1);
  Sem_init(&w, 0, 1);
  Sem_init(&u, 0, 1);
  readcnt = 0;
}

/* 원하는 캐시(client request) get */
int cache_get(char *key, char *value) {
  /* 
   * 세마포어 발명한 다익스트라가 네덜란드 사람이라서
   * 변수명 P, V는 아래와 같은 의미!
   * P : Probeer(try) - 임계영역에 접근 가능하게 해주는 함수
   * V : Verhoog(increment) - 접근 불가능하게
   */
  P(&mutex); /* 임계영역 시작 */
  if (++readcnt == 1) P(&w);
  V(&mutex); /* 임계영역 끝 */

  
  int hit;
  cnode_t *elem;
  hit = 0;
  elem = g_cache->head;
  while (elem != NULL) {
    /* 현재 노드의 key값이 찾고자하는 client request content와 같지 않으면 */
    if (!strcmp(elem->key, key)) { /* strcmp : returns 0 if same // not 0 = True */
      /* 현재 노드가 헤드노드가 아니면 가장 최근에 참조했으니 head로 옮긴다. */
      if (elem != g_cache->head) { 
        P(&u); /* 임계영역 시작 */
        /* 현재의 다음으로 next 이어줌 */
        elem->prev->next = elem->next;
        if (elem == g_cache->tail)
          g_cache->tail = elem->prev;
        else
          elem->next->prev = elem->prev;
        /* 현재 노드를 head로 재조정 */
        elem->prev = NULL;
        elem->next = g_cache->head;
        if (g_cache->head != NULL) g_cache->head->prev = elem;
        g_cache->head = elem;
        V(&u); /* 임계영역 끝 */
      }
      /* 현재 노드가 헤드노드면 바로 뽑아내면 된다. LRU! */
      strcpy(value, elem->value);
      hit = 1;
      break;
    }
    /* 현재 노드의 key값이 찾고자하는 client request content와 같으면 다음 노드로 옮겨서 확인 */
    elem = elem->next;
  }

  P(&mutex); /* 임계영역 시작 */
  if (--readcnt == 0) V(&w); 
  V(&mutex); /* 임계영역 끝 */


  /* 
   * hit = 0 -> 캐시에 저장된 request가 없으므로 엔드 서버에 요청해야함
   * hit > 0 -> 캐시에 저장된 request가 있으므로 프록시 서버에서 바로 응답
   */
  return hit;
}

/* 캐시 저장 */
void cache_place(char *key, char *value) {
  P(&w); /* 임계영역 시작 */
  cnode_t *elem;
  size_t size = strlen(key) + strlen(value) + sizeof(elem);
  g_cache->size += size;
  while ((g_cache->tail != NULL) && (g_cache->size > MAX_CACHE_SIZE)) {
    /* 캐시를 저장할 충분한 공간이 없으면 tail 부터 삭제함 (LRU) */
    elem = g_cache->tail;
    size = strlen(elem->key) + strlen(elem->value) + sizeof(elem);

    g_cache->size -= size;
    g_cache->tail = g_cache->tail->prev;
    g_cache->tail->next = NULL;
    free(elem->key);
    free(elem->value);
    free(elem);
  }

  /* 캐시를 저장할 공간이 충분하면 head로 새롭게 넣는다. */
  elem = (cnode_t *)malloc(sizeof(cnode_t));
  elem->key = (char *)malloc(strlen(key) + 1);
  elem->value = (char *)malloc(strlen(value) + 1);
  strcpy(elem->key, key);
  strcpy(elem->value, value);

  elem->prev = NULL;
  elem->next = g_cache->head;
  if (g_cache->head == NULL)
    g_cache->tail = elem;
  else
    g_cache->head->prev = elem;
  g_cache->head = elem;

  V(&w); /* 임계영역 끝 */
}

/* 캐시 전체 노드에 할당했던 메모리를 전부 free */
void cache_destroy() {
  cnode_t *elem, *tmp;
  if (g_cache != NULL) {
    elem = g_cache->head;
    while (elem != NULL) {
      tmp = elem->next;
      free(elem->key);
      free(elem->value);
      free(elem);
      elem = tmp;
    }
  }
}