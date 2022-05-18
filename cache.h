#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* 
 * 캐시(client request)-값(server response) 저장할 노드 구조체
 */
typedef struct cnode {
    char *key;
    char *value;
    struct cnode *prev;
    struct cnode *next;
} cnode_t;

/* 
 * 전체 캐시 노드들을 포함할 구조체 
 * LRU 노드를 가장 앞에 둔 linked list로 이루어짐
 */
typedef struct cache {
    cnode_t *head;
    cnode_t *tail;
    size_t size;
} cache_t;


void cache_init();
void cache_place(char *key,char *value);
int  cache_get(char *key,char *value);
void cache_destroy();

#endif