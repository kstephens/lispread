#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef void *VALUE;
#define EQ(X,Y)         ((X) == (Y))
#define EOS ((VALUE) -1)
#define NIL             ((VALUE) 0)
#define T               ((VALUE) 512)
#define F               ((VALUE) 513)
#define U               ((VALUE) 514)

static struct alloc {
  int id;
  void *ptr;
  size_t size;
  void *realloc_of;
  int live;
} allocs[1024];
static int allocs_n;

static
struct alloc *find_alloc(void *p)
{
  int i;
  for ( i = 0; i < allocs_n; ++ i ) {
    if ( allocs[i].ptr == p )
      return &allocs[i];
  }
  return 0;
}

static
void *alloc_id(void *p)
{
  struct alloc *a = find_alloc(p);
  return a ? (void*) (a->id + 32768) : p;
}

#define P(x) alloc_id((void*) x)

static
void *test_malloc(size_t s)
{
  void *p = malloc(s);
  allocs[allocs_n].id   = allocs_n + 1;
  allocs[allocs_n].ptr  = p;
  allocs[allocs_n].size = s;
  allocs[allocs_n].live = 1; 
  ++ allocs_n;
  printf("MALLOC(%ld) => %p\n", (unsigned long) s, P(p));
  return p;
}

static
void *test_realloc(void *p, size_t s)
{
  void *pn = realloc(p, s);
  struct alloc *a = find_alloc(p);
  if ( p == pn ) {
    a->ptr = pn;
    a->size = s;
  } else {
    allocs[allocs_n].id   = allocs_n + 1;
    allocs[allocs_n].ptr  = pn;
    allocs[allocs_n].size = s;
    allocs[allocs_n].live = 1; 
    ++ allocs_n;
  }
  if ( p != pn ) printf("REALLOC(%p,%ld) => %p\n", P(p), (unsigned long) s, P(pn));
  return pn;
}

static
void test_free(void *p)
{
  struct alloc *a = find_alloc(p);
  a->live = 0;
  a->ptr = 0;
  printf("FREE(%p)\n", P(p));
  free(p);
}

static
VALUE make_string(const char *p, size_t s)
{
  void *o = memcpy(test_malloc(s + 1), p, s + 1);
  printf("STRING(%s,%lu) => %p\n", p, s, P(o));
  return o;
}

static
struct symbol {
  int id;
  const char *name;
} symbols[1024] = { 
  { 1, "." },
  { 2, "quote" },
  { 3, "quasiquote" },
  { 4, "unquote" },
  { 5, "unquote-splicing" },
};
static int symbols_n = 1;

static
VALUE string_2_symbol(VALUE x)
{
  const char *s = x;
  int i;
  for ( i = 0; i < symbols_n; ++ i ) {
    if ( strcmp(s, symbols[i].name) == 0 ) {
      return (VALUE) symbols[i].name;
    }
  }
  s = strcpy(test_malloc(strlen(s)), s);
  symbols[i].id = ++ symbols_n;
  symbols[i].name = s;
  printf("STRING_2_SYMBOL(%s) => %p\n", s, P(s));
  return (VALUE) s;
}

static
VALUE string_2_number(VALUE x, int radix)
{
  char *s = x, *se = 0;
  int n = strtol(s, &se, radix);
  x = se && *se == 0 ? (VALUE) (n + 8192) : F;
  printf("STRING_2_NUMBER(%s) => %p\n", s, P(x));
  return x;
}

struct pair { VALUE car, cdr; };
static
VALUE cons(VALUE a, VALUE d)
{
  struct pair *p = test_malloc(sizeof(*p));
  p->car = a;
  p->cdr = d;
  printf("CONS(%p,%p) => %p\n", P(a), P(d), P(p));
  return p;
}
static
VALUE car(struct pair *p)
{
  printf("CAR(%p)\n", P(p));
  return p->car;
}

static
void set_cdr(struct pair *p, VALUE v)
{
  printf("SET_CDR(%p,%p)\n", P(p), P(v));
  p->cdr = v;
}


#define READ_DECL static VALUE test_read(FILE *stream)
#define READ_CALL() test_read(stream)
#define GETC(S)      fgetc(S)
#define UNGETC(S,C)  ungetc(C,S)
#define MALLOC(S)    test_malloc(S)
#define REALLOC(P,S) test_realloc(P,S)
#define FREE(P)      test_free(P)
#define CONS(X,Y)    cons(X,Y)
#define CAR(X)       printf("CAR(%p)\n", P(X))
#define SET_CDR(CONS,V) set_cdr(CONS,V)
#define MAKE_CHAR(I)    (printf("MAKE_CHAR(%d)\n", I), (VALUE) ((I) + 256))
#define STRING(P,S)        make_string(P,S)
#define STRING_2_NUMBER(X,RADIX) string_2_number(X,RADIX)
#define STRING_2_SYMBOL(X) string_2_symbol(X)
#define LIST_2_VECTOR(X) (printf("LIST_2_VECTOR(%p)", P(X)), X)
#define SYMBOL(NAME)    string_2_symbol(#NAME)
#define SYMBOL_DOT      string_2_symbol(".")
#define BRACKET_LISTS   1
#define ERROR(STR...)      (printf("ERROR: "), printf(STR), abort(), NIL)
#include "lispread.c"

int main(int argc, char **argv)
{
  while ( ! feof(stdin) ) {
    printf("  fpos = %lu\n", (unsigned long) ftell(stdin));
    test_read(stdin);
  }
  return 0;
}
