/* Instrument only the compiled Store/cJSON test allocation requests.
 * Header overhead and CRT internals are excluded. Host ABI != NuttX ABI.
 */
#undef malloc
#undef calloc
#undef realloc
#undef free
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef union {max_align_t align;struct {size_t size;} data;} allocation;
static size_t live,peak,requests;static int registered;
static void report(void)
{printf("HOST allocation requests=%llu, peak_payload=%llu bytes, remaining_payload=%llu bytes (instrumented Store/cJSON only)\n",
        (unsigned long long)requests,(unsigned long long)peak,(unsigned long long)live);}
void *vg_measure_malloc(size_t n)
{
  allocation *h;
  if(n>SIZE_MAX-sizeof(*h))return NULL;
  if(!registered){registered=1;atexit(report);}
  h=malloc(sizeof(*h)+n);if(!h)return NULL;
  h->data.size=n;live+=n;if(live>peak)peak=live;requests++;return h+1;
}
void vg_measure_free(void *p)
{allocation *h;if(!p)return;h=(allocation *)p-1;live-=h->data.size;free(h);}
void *vg_measure_calloc(size_t n,size_t size)
{void *p;if(size&&n>SIZE_MAX/size)return NULL;p=vg_measure_malloc(n*size);if(p)memset(p,0,n*size);return p;}
void *vg_measure_realloc(void *p,size_t n)
{
  allocation *h;size_t old;void *q;
  if(!p)return vg_measure_malloc(n);
  if(!n){vg_measure_free(p);return NULL;}
  h=(allocation *)p-1;old=h->data.size;
  q=vg_measure_malloc(n);if(!q)return NULL;
  memcpy(q,p,old<n?old:n);vg_measure_free(p);return q;
}
