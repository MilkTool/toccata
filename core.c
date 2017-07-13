
#include <stdlib.h>
#include "core.h"

void prefs(char *tag, Value *v) {
  printf("%s: %p %d\n", tag, v, v->refs);
}

#ifdef CHECK_MEM_LEAK
int64_t malloc_count = 0;
int64_t free_count = 0;
#endif

Value *nothing = (Value *)&(Maybe){MaybeType, -1, 0};
List *empty_list = &(List){ListType,-1,0,0,0};
Vector *empty_vect = &(Vector){VectorType,-1,0,5,0,0};

int32_t refsInit = 1;
int32_t staticRefsInit = -1;
int32_t refsError = -10;
Value *my_malloc(int64_t sz) {
#ifdef CHECK_MEM_LEAK
  __atomic_fetch_add(&malloc_count, 1, __ATOMIC_ACQ_REL);
#endif
  Value *val = malloc(sz);
  if (sz > sizeof(Value))
    __atomic_store(&val->refs, &refsInit, __ATOMIC_RELAXED);
  return(val);
}

typedef struct {Value *head; uintptr_t aba;} FreeValList;

Value *removeFreeValue(FreeValList *freeList) {
  FreeValList orig;
  __atomic_load((long long *)freeList, (long long *)&orig, __ATOMIC_RELAXED);
  FreeValList next = orig;
  Value *item = (Value *)0;
  if (orig.head != (Value *)0) {
    do {
      item = orig.head;
      next.head = item->next;
      next.aba = orig.aba + 1;
    } while (!__atomic_compare_exchange((long long *)freeList,
                                        (long long *)&orig,
                                        (long long *)&next, 1,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED) &&
             orig.head != (Value *)0);
    if (orig.head == (Value *)0)
      item = (Value *)0;
  }

  if (item == (Value *)0) {
    return((Value *)0);
  } else {
    int32_t refs;
    __atomic_load(&item->refs, &refs, __ATOMIC_RELAXED);
    if (refs != -10) {
      fprintf(stderr, "failure in removeFreeValue: %d\n", refs);
      abort();
    }
    return(item);
  }
}

FreeValList centralFreeIntegers = (FreeValList){(Value *)0, 0};
__thread FreeValList freeIntegers = {(Value *)0, 0};
Integer *malloc_integer() {
  Integer *newInteger = (Integer *)freeIntegers.head;
  if (newInteger == (Integer *)0) {
    newInteger = (Integer *)removeFreeValue(&centralFreeIntegers);
    if (newInteger == (Integer *)0) {
      Integer *numberStructs = (Integer *)my_malloc(sizeof(Integer) * 100);
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&malloc_count, 99, __ATOMIC_ACQ_REL);
#endif
      for (int i = 1; i < 99; i++) {
        __atomic_store(&numberStructs[i].refs, &refsError, __ATOMIC_RELAXED);
        ((Value *)&numberStructs[i])->next = (Value *)&numberStructs[i + 1];
      }
      __atomic_store(&numberStructs[99].refs, &refsError, __ATOMIC_RELAXED);
      ((Value *)&numberStructs[99])->next = (Value *)0;
      freeIntegers.head = (Value *)&numberStructs[1];

      numberStructs->type = IntegerType;
      __atomic_store(&numberStructs->refs, &refsInit, __ATOMIC_RELAXED);
      return(numberStructs);
    }
  } else {
    freeIntegers.head = freeIntegers.head->next;
  }
  newInteger->type = IntegerType;
  __atomic_store(&newInteger->refs, &refsInit, __ATOMIC_RELAXED);
  return(newInteger);
}

int decRefs(Value *v, int deltaRefs) {
  int32_t refs;
  __atomic_load(&v->refs, &refs, __ATOMIC_RELAXED);
  if (refs == -1)
    return(refs);

  int32_t newRefs;
  do {
    if (refs < deltaRefs) {
      fprintf(stderr, "failure in dec_and_free: %d %p\n", refs, v);
      abort();
    } else if (refs == deltaRefs)
      newRefs = -10;
    else
      newRefs = refs - deltaRefs;
  } while (!__atomic_compare_exchange(&v->refs, &refs, &newRefs, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

  return(newRefs);
}

typedef void (*freeValFn)(Value *);

void freeInteger(Value *v) {
  v->next = freeIntegers.head;
  freeIntegers.head = v;
}

FreeValList centralFreeStrings = (FreeValList){(Value *)0, 0};
__thread FreeValList freeStrings = {(Value *)0, 0};
#define STRING_RECYCLE_LEN 100
String *malloc_string(int len) {
  String *str;
  if (len > STRING_RECYCLE_LEN) {
    str = (String *)my_malloc(sizeof(String) + len + 4);
    memset(str->buffer, 0, len + 4);
  } else {
    str = (String *)freeStrings.head;
    if (str == (String *)0) {
      str = (String *)removeFreeValue(&centralFreeStrings);
      if (str == (String *)0) {
        str = (String *)my_malloc(sizeof(String) + STRING_RECYCLE_LEN + 4);
        memset(str->buffer, 0, STRING_RECYCLE_LEN);
        str->hash = (Integer *)0;
        str->type = StringType;
        str->len = len;
      }
    } else {
      freeStrings.head = freeStrings.head->next;
    }
  }
  __atomic_store(&str->refs, &refsInit, __ATOMIC_RELAXED);
  str->hash = (Integer *)0;
  str->type = StringType;
  str->len = len;
  return(str);
}

void freeString(Value *v) {
    Integer *hash = ((String *)v)->hash;
    if (hash != (Integer *)0) {
      dec_and_free((Value *)hash, 1);
    }

    int64_t len = ((String *)v)->len;
    if (len <= STRING_RECYCLE_LEN) {
      v->next = freeStrings.head;
      freeStrings.head = v;
    } else {
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&free_count, 1, __ATOMIC_ACQ_REL);
      // __atomic_fetch_add(&free_strs, 1, __ATOMIC_ACQ_REL);
#endif
      free(v);
    }
  }

FreeValList centralFreeSubStrings = (FreeValList){(Value *)0, 0};
__thread FreeValList freeSubStrings = {(Value *)0, 0};
SubString *malloc_substring() {
  SubString *subStr = (SubString *)freeSubStrings.head;
  if (subStr == (SubString *)0) {
    subStr = (SubString *)removeFreeValue(&centralFreeSubStrings);
    if (subStr == (SubString *)0) {
      subStr = (SubString *)my_malloc(sizeof(SubString));
      subStr->hash = (Integer *)0;
      __atomic_store(&subStr->refs, &refsInit, __ATOMIC_RELAXED);
      return(subStr);
    }
  } else {
    freeSubStrings.head = freeSubStrings.head->next;
  }
  __atomic_store(&subStr->refs, &refsInit, __ATOMIC_RELAXED);
  subStr->hash = (Integer *)0;
  return(subStr);
}

void freeSubString(Value *v) {
  Value *src = ((SubString *)v)->source;
  Integer *hash = ((SubString *)v)->hash;
  if (src != (Value *)0) {
    dec_and_free(src, 1);
  }
  if (hash != (Integer *)0) {
    dec_and_free((Value *)hash, 1);
  }
  v->next = freeSubStrings.head;
  freeSubStrings.head = v;
}

FreeValList centralFreeFnArities = (FreeValList){(Value *)0, 0};
__thread FreeValList freeFnArities = {(Value *)0, 0};
FnArity *malloc_fnArity() {
  FnArity *newFnArity = (FnArity *)freeFnArities.head;
  if (newFnArity == (FnArity *)0) {
    newFnArity = (FnArity *)removeFreeValue(&centralFreeFnArities);
    if (newFnArity == (FnArity *)0) {
      newFnArity = (FnArity *)my_malloc(sizeof(FnArity));
      newFnArity->type = FnArityType;
      newFnArity->refs = 1;
      return(newFnArity);
    }
  } else {
    freeFnArities.head = freeFnArities.head->next;
  }
  newFnArity->type = FnArityType;
  __atomic_store(&newFnArity->refs, &refsInit, __ATOMIC_RELAXED);
  return(newFnArity);
}

void freeFnArity(Value *v) {
  FnArity *arity = (FnArity *)v;
  dec_and_free((Value *)arity->closures, 1);
  v->next = freeFnArities.head;
  freeFnArities.head = v;
}

FreeValList centralFreeFunctions[10] = {(FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0},
                                        (FreeValList){(Value *)0, 0}};
__thread FreeValList freeFunctions[10] = {{(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0},
                                          {(Value *)0, 0}};
Function *malloc_function(int arityCount) {
  Function *newFunction;
  if (arityCount > 9) {
    newFunction = (Function *)my_malloc(sizeof(Function) + sizeof(FnArity *) * arityCount);
    __atomic_store(&((Function *)newFunction)->refs, &refsInit, __ATOMIC_RELAXED);
    newFunction->type = FunctionType;
    return(newFunction);
  } else {
    newFunction = (Function *)freeFunctions[arityCount].head;
    if (newFunction == (Function *)0) {
      newFunction = (Function *)removeFreeValue(&centralFreeFunctions[arityCount]);
      if (newFunction == (Function *)0) {
        newFunction = (Function *)my_malloc(sizeof(Function) + sizeof(FnArity *) * arityCount);
      }
    } else {
      freeFunctions[arityCount].head = freeFunctions[arityCount].head->next;
    }
    newFunction->type = FunctionType;
    __atomic_store(&((Function *)newFunction)->refs, &refsInit, __ATOMIC_RELAXED);
    return((Function *)newFunction);
  }
}

void freeFunction(Value *v) {
  Function *f = (Function *)v;
  for (int i = 0; i < f->arityCount; i++) {
    dec_and_free((Value *)f->arities[i], 1);
  }
  // fprintf(stderr, "%p freed\n", v);
  if (f->arityCount < 10) {
    v->next = freeFunctions[f->arityCount].head;
    freeFunctions[f->arityCount].head = v;
  } else {
#ifdef CHECK_MEM_LEAK
    __atomic_fetch_add(&free_count, 1, __ATOMIC_ACQ_REL);
#endif
    free(v);
  }
}

FreeValList centralFreeLists = (FreeValList){(Value *)0, 0};
__thread FreeValList freeLists = (FreeValList){(Value *)0, 0};
List *malloc_list() {
  List *newList = (List *)freeLists.head;
  if (newList == (List *)0) {
    newList = (List *)removeFreeValue(&centralFreeLists);
    if (newList == (List *)0) {
      List *listStructs = (List *)my_malloc(sizeof(List) * 100);
      for (int i = 1; i < 99; i++) {
        __atomic_store(&listStructs[i].refs, &refsError, __ATOMIC_RELAXED);
        ((Value *)&listStructs[i])->next = (Value *)&listStructs[i + 1];
      }
      __atomic_store(&listStructs[99].refs, &refsError, __ATOMIC_RELAXED);
      ((Value *)&listStructs[99])->next = freeLists.head;
      freeLists.head = (Value *)&listStructs[1];
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&malloc_count, 99, __ATOMIC_ACQ_REL);
#endif

      listStructs->type = ListType;
      __atomic_store(&listStructs->refs, &refsInit, __ATOMIC_RELAXED);
      listStructs->head = (Value *)0;
      listStructs->tail = (List *)0;
      listStructs->len = 0;
      return(listStructs);
    }
  } else {
    freeLists.head = freeLists.head->next;
  }

  newList->type = ListType;
  newList->refs = 1;
  newList->head = (Value *)0;
  newList->tail = (List *)0;
  newList->len = 0;
  return(newList);
}

void freeList(Value *v) {
  List *l = (List *)v;
  Value *head = l->head;
  if (head != (Value *)0)
    dec_and_free(head, 1);
  List *tail = l->tail;
  l->tail = (List *)0;
  v->next = freeLists.head;
  freeLists.head = v;
  if (tail != (List *)0)
    dec_and_free((Value *)tail, 1);
}

FreeValList centralFreeMaybes = (FreeValList){(Value *)0, 0};
__thread FreeValList freeMaybes = {(Value *)0, 0};
Maybe *malloc_maybe() {
  Maybe *newMaybe = (Maybe *)freeMaybes.head;
  if (newMaybe == (Maybe *)0) {
    newMaybe = (Maybe *)removeFreeValue(&centralFreeMaybes);
    if (newMaybe == (Maybe *)0) {
      Maybe *maybeStructs = (Maybe *)my_malloc(sizeof(Maybe) * 50);
      for (int i = 1; i < 49; i++) {
        __atomic_store(&maybeStructs[i].refs, &refsError, __ATOMIC_RELAXED);
        ((Value *)&maybeStructs[i])->next = (Value *)&maybeStructs[i + 1];
      }
      __atomic_store(&maybeStructs[49].refs, &refsError, __ATOMIC_RELAXED);
      ((Value*)&maybeStructs[49])->next = (Value *)0;
      freeMaybes.head = (Value *)&maybeStructs[1];
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&malloc_count, 49, __ATOMIC_ACQ_REL);
#endif

      maybeStructs->type = MaybeType;
      __atomic_store(&maybeStructs->refs, &refsInit, __ATOMIC_RELAXED);
      return(maybeStructs);
    }
  } else {
    freeMaybes.head = freeMaybes.head->next;
  }
  newMaybe->type = MaybeType;
  __atomic_store(&newMaybe->refs, &refsInit, __ATOMIC_RELAXED);
  return(newMaybe);
}

void freeMaybe(Value *v) {
  Value *value = ((Maybe *)v)->value;
  dec_and_free(value, 1);
  v->next = freeMaybes.head;
  freeMaybes.head = v;
}

FreeValList centralFreeVectorNodes = (FreeValList){(Value *)0, 0};
__thread FreeValList freeVectorNodes = {(Value *)0, 0};
VectorNode *malloc_vectorNode() {
  VectorNode *newVectorNode = (VectorNode *)freeVectorNodes.head;
  if (newVectorNode == (VectorNode *)0) {
    newVectorNode = (VectorNode *)removeFreeValue(&centralFreeVectorNodes);
    if (newVectorNode == (VectorNode *)0) {
      VectorNode *nodeStructs = (VectorNode *)my_malloc(sizeof(VectorNode) * 50);
      for (int i = 1; i < 49; i++) {
        __atomic_store(&nodeStructs[i].refs, &refsError, __ATOMIC_RELAXED);
        ((Value *)&nodeStructs[i])->next = (Value *)&nodeStructs[i + 1];
      }
      __atomic_store(&nodeStructs[49].refs, &refsError, __ATOMIC_RELAXED);
      ((Value*)&nodeStructs[49])->next = (Value *)0;
      freeVectorNodes.head = (Value *)&nodeStructs[1];
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&malloc_count, 49, __ATOMIC_ACQ_REL);
#endif

      nodeStructs->type = VectorNodeType;
      __atomic_store(&nodeStructs->refs, &refsInit, __ATOMIC_RELAXED);
      memset(&nodeStructs->array, 0, sizeof(Value *) * VECTOR_ARRAY_LEN);
      return(nodeStructs);
    }
  } else {
    freeVectorNodes.head = freeVectorNodes.head->next;
  }
  newVectorNode->type = VectorNodeType;
  __atomic_store(&newVectorNode->refs, &refsInit, __ATOMIC_RELAXED);
  memset(&newVectorNode->array, 0, sizeof(Value *) * VECTOR_ARRAY_LEN);
  return(newVectorNode);
}

void freeVectorNode(Value *v) {
  for (int i = 0; i < VECTOR_ARRAY_LEN; i++) {
    if (((VectorNode *)v)->array[i] != (Value *)0) {
      dec_and_free(((VectorNode *)v)->array[i], 1);
    }
  }
  v->next = freeVectorNodes.head;
  freeVectorNodes.head = v;
}

FreeValList centralFreeVectors = (FreeValList){(Value *)0, 0};
__thread FreeValList freeVectors = {(Value *)0, 0};
Vector *malloc_vector() {
  Vector *newVector = (Vector *)freeVectors.head;
  if (newVector == (Vector *)0) {
    newVector = (Vector *)removeFreeValue(&centralFreeVectors);
    if (newVector == (Vector *)0) {
      Vector *vectorStructs = (Vector *)my_malloc(sizeof(Vector) * 300);
      for (int i = 1; i < 299; i++) {
        __atomic_store(&vectorStructs[i].refs, &refsError, __ATOMIC_RELAXED);
        ((Value *)&vectorStructs[i])->next = (Value *)&vectorStructs[i + 1];
      }
      __atomic_store(&vectorStructs[299].refs, &refsError, __ATOMIC_RELAXED);
      ((Value*)&vectorStructs[299])->next = (Value *)0;
      freeVectors.head = (Value *)&vectorStructs[1];
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&malloc_count, 299, __ATOMIC_ACQ_REL);
#endif

      vectorStructs->type = VectorType;
      __atomic_store(&vectorStructs->refs, &refsInit, __ATOMIC_RELAXED);
      vectorStructs->count = 0;
      vectorStructs->shift = 5;
      vectorStructs->root = (VectorNode *)0;
      vectorStructs->root = (VectorNode *)0;
      memset(&vectorStructs->tail, 0, sizeof(Value *) * VECTOR_ARRAY_LEN);
      return(vectorStructs);
    }
  } else {
    freeVectors.head = freeVectors.head->next;
  }
  newVector->type = VectorType;
  __atomic_store(&newVector->refs, &refsInit, __ATOMIC_RELAXED);
  newVector->count = 0;
  newVector->shift = 5;
  newVector->root = (VectorNode *)0;
  newVector->root = (VectorNode *)0;
  memset(&newVector->tail, 0, sizeof(Value *) * VECTOR_ARRAY_LEN);
  return(newVector);
}

void freeVector(Value *v) {
  Value *root = (Value *)((Vector *)v)->root;
  if (root != (Value *)0)
    dec_and_free(root, 1);
  for (int i = 0; i < VECTOR_ARRAY_LEN; i++) {
    if (((Vector *)v)->tail[i] != (Value *)0) {
      dec_and_free(((Vector *)v)->tail[i], 1);
    }
  }
  v->next = freeVectors.head;
  freeVectors.head = v;
}

FreeValList centralFreeReified[20] = {(FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0},
                                      (FreeValList){(Value *)0, 0}};
__thread FreeValList freeReified[20] = {{(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0},
                                        {(Value *)0, 0}};
ReifiedVal *malloc_reified(int implCount) {
  ReifiedVal *newReifiedVal;
  if (implCount > 19) {
    newReifiedVal = (ReifiedVal *)my_malloc(sizeof(ReifiedVal) + sizeof(Function *) * implCount);
  } else {
    newReifiedVal = (ReifiedVal *)freeReified[implCount].head;
    if (newReifiedVal == (ReifiedVal *)0) {
      newReifiedVal = (ReifiedVal *)removeFreeValue(&centralFreeReified[implCount]);
      if (newReifiedVal == (ReifiedVal *)0) {
        newReifiedVal = (ReifiedVal *)my_malloc(sizeof(ReifiedVal) + sizeof(Function *) * implCount);
      }
    } else {
      freeReified[implCount].head = freeReified[implCount].head->next;
    }
  }
  __atomic_store(&newReifiedVal->refs, &refsInit, __ATOMIC_RELAXED);
  newReifiedVal->implCount = implCount;
  newReifiedVal->typeArgs = (Value *)0;
  return(newReifiedVal);
}

#define FREE_FN_COUNT 20
freeValFn freeJmpTbl[FREE_FN_COUNT] = {NULL,
                                       &freeInteger,
				       &freeString,
				       &freeFnArity,
				       &freeFunction,
				       &freeSubString,
				       &freeList,
				       &freeMaybe,
				       &freeVector,
				       &freeVectorNode
};

void dec_and_free(Value *v, int deltaRefs) {
  if (decRefs(v, deltaRefs) >= -1)
    return;

  if (v->type < FREE_FN_COUNT) {
    freeJmpTbl[v->type](v);
  } else {
    ReifiedVal *rv = (ReifiedVal *)v;
    for (int i = 0; i < rv->implCount; i++) {
      dec_and_free(rv->impls[i], 1);
    }
    if (rv->typeArgs != (Value *)0)
      dec_and_free(rv->typeArgs, 1);
    if (rv->implCount < 20) {
      v->next = freeReified[rv->implCount].head;
      freeReified[rv->implCount].head = v;
    } else {
#ifdef CHECK_MEM_LEAK
      __atomic_fetch_add(&free_count, 1, __ATOMIC_ACQ_REL);
#endif
      free(v);
    }
  }
#ifdef CHECK_MEM_LEAK
  // fprintf(stderr, "malloc_count: %ld free_count: %ld\r", malloc_count, free_count);
#endif
};

Value *incRef(Value *v, int deltaRefs) {
  if (deltaRefs < 0) {
    fprintf(stderr, "bad deltaRefs: %p\n", v);
    abort();
  }

  if (deltaRefs < 1)
    return(v);

  int32_t refs;
  __atomic_load(&v->refs, &refs, __ATOMIC_RELAXED);
  if (refs < -1) {
    fprintf(stderr, "failure in incRef: %d %p\n", refs, v);
    abort();
  }

  if (refs >= 0)
    __atomic_fetch_add(&v->refs, deltaRefs, __ATOMIC_ACQ_REL);
  return(v);
}

#ifdef CHECK_MEM_LEAK
void moveFreeToCentral();

void freeGlobal(Value *x) {
  if (x->refs == -10)
    return;
  x->refs = 1;
  dec_and_free(x, 1);
}

void emptyFreeList(FreeValList *freeLinkedList) {
  FreeValList listHead;
  __atomic_load((long long *)freeLinkedList, (long long *)&listHead, __ATOMIC_RELAXED);
  for(Value *item = listHead.head;
      item != (Value *)0;
      item =  item->next) {
    __atomic_fetch_add(&free_count, 1, __ATOMIC_ACQ_REL);
  }
}

void freeAll() {
  moveFreeToCentral();

  for (int i = 0; i < 10; i++) {
    emptyFreeList(&centralFreeFunctions[i]);
  }
  for (int i = 0; i < 20; i++) {
    emptyFreeList(&centralFreeReified[i]);
  }
  // for (int i = 0; i < 20; i++) {
  // emptyFreeList(&centralFreeBMINodes[i]);
  // }
  // emptyFreeList(&centralFreeFutures);
  // FreeValList listHead;
  // __atomic_load((long long *)&centralFreeFutures,
  //               (long long *)&listHead, __ATOMIC_RELAXED);
  // emptyFreeList(&centralFreePromises);
  // emptyFreeList(&centralFreeArrayNodes);
  emptyFreeList(&centralFreeSubStrings);
  emptyFreeList(&centralFreeFnArities);
  emptyFreeList(&centralFreeLists);
  emptyFreeList(&centralFreeMaybes);
  emptyFreeList(&centralFreeVectors);
  emptyFreeList(&centralFreeVectorNodes);
  emptyFreeList(&centralFreeStrings);
  emptyFreeList(&centralFreeIntegers);

//*
  int64_t mallocs;
  __atomic_load(&malloc_count, &mallocs, __ATOMIC_RELAXED);
  int64_t frees;
  __atomic_load(&free_count, &frees, __ATOMIC_RELAXED);
  fprintf(stderr, "malloc count: %ld  free count: %ld  diff: %ld\n",
          mallocs, frees, mallocs - frees);
// */
}

void moveToCentral(FreeValList *freeList, FreeValList *centralList) {
  Value *tail = freeList->head;
  while (tail != (Value *)0 && tail->next != (Value *)0) {
    tail = tail->next;
  }
  
  if (tail == (Value *)0)
    return;
  else {
    FreeValList orig;
    __atomic_load((long long *)centralList, (long long *)&orig, __ATOMIC_RELAXED);
    FreeValList next = orig;
    do {
      tail->next = orig.head;
      next.head = freeList->head;
      next.aba = orig.aba + 1;
    } while (!__atomic_compare_exchange((long long *)centralList, (long long *)&orig, (long long *)&next, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    freeList->head = (Value *)0;
    return;
  }
}

void moveFreeToCentral() {
  moveToCentral(&freeLists, &centralFreeLists);
  for (int i = 0; i < 10; i++) {
    moveToCentral(&freeFunctions[i], &centralFreeFunctions[i]);
  }
  // for (int i = 0; i < 20; i++) {
  // moveToCentral(&freeBMINodes[i], &centralFreeBMINodes[i]);
  // }
  for (int i = 0; i < 20; i++) {
    moveToCentral(&freeReified[i], &centralFreeReified[i]);
  }
  moveToCentral(&freeStrings, &centralFreeStrings);
  // moveToCentral(&freeArrayNodes, &centralFreeArrayNodes);
  moveToCentral(&freeSubStrings, &centralFreeSubStrings);
  moveToCentral(&freeIntegers, &centralFreeIntegers);
  moveToCentral(&freeMaybes, &centralFreeMaybes);
  moveToCentral(&freeVectors, &centralFreeVectors);
  moveToCentral(&freeVectorNodes, &centralFreeVectorNodes);
  moveToCentral(&freeFnArities, &centralFreeFnArities);
  // moveToCentral(&freePromises, &centralFreePromises);
  // moveToCentral(&freeFutures, &centralFreeFutures);
}
#endif

char *extractStr(Value *v) {
  // Should only be used to print an error meessage when calling 'abort'
  // Leaks a String value
  String *newStr = (String *)my_malloc(sizeof(String) + ((String *)v)->len + 5);
  newStr->hash = (Integer *)0;
  if (v->type == StringType)
    snprintf(newStr->buffer, ((String *)v)->len + 1, "%s", ((String *)v)->buffer);
  else if (v->type == SubStringType)
    snprintf(newStr->buffer, ((String *)v)->len + 1, "%s", ((SubString *)v)->buffer);
  else {
    fprintf(stderr, "\ninvalid type for 'extractStr'\n");
    abort();
  }
  return(newStr->buffer);
}

Value *findProtoImpl(int64_t type, ProtoImpls *impls) {
  int64_t implIndex = 0;
  Value *defaultImpl = (Value *)0;
  while(implIndex < impls->implCount) {
    if (impls->impls[implIndex].type == 0) {
       defaultImpl = impls->impls[implIndex].implFn;
    }

    if (type != impls->impls[implIndex].type) {
      implIndex++;
    } else
      return(impls->impls[implIndex].implFn);
  }
  return(defaultImpl);
};

FnArity *findFnArity(Value *fnVal, int64_t argCount) {
  Function *fn = (Function *)fnVal;
  int arityIndex = 0;
  FnArity *arity = (FnArity *)fn->arities[arityIndex];
  FnArity *variadic = (FnArity *)0;
  while(arityIndex < fn->arityCount) {
    arity = (FnArity *)fn->arities[arityIndex];
    if (arity->variadic) {
      variadic = arity;
      arityIndex++;
    } else if (arity->count != argCount) {
      arityIndex++;
    } else
      return(arity);
  }
  return(variadic);
};

Value *proto1Arg(ProtoImpls *protoImpls, char *name, Value *arg0,
                 char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 1 argument for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType1 *_fn = (FnType1 *)_arity->fn;
  return(_fn(_arity->closures, arg0));
}

Value *proto2Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1,
                 char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 2 arguments for type: %s (%ld) at %s: %ld\n",
                    name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType2 *_fn = (FnType2 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1));
}

Value *proto3Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 3 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType3 *_fn = (FnType3 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2));
}

Value *proto4Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 4 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType4 *_fn = (FnType4 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3));
}

Value *proto5Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, Value *arg4, char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 5 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType5 *_fn = (FnType5 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3, arg4));
}

Value *proto6Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, Value *arg4, Value *arg5, char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 5 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType6 *_fn = (FnType6 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3, arg4, arg5));
}

Value *proto7Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, Value *arg4, Value *arg5, Value *arg6,
                 char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 7 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType7 *_fn = (FnType7 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3, arg4, arg5, arg6));
}

Value *proto8Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, Value *arg4, Value *arg5, Value *arg6, Value *arg7,
                 char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 8 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType8 *_fn = (FnType8 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7));
}

Value *proto9Arg(ProtoImpls *protoImpls, char *name, Value *arg0, Value *arg1, Value *arg2,
                 Value *arg3, Value *arg4, Value *arg5, Value *arg6, Value *arg7,
                 Value *arg8, char *file, int64_t line) {
  FnArity *_arity = (FnArity *)findProtoImpl(arg0->type, protoImpls);
  if(_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** Could not find implmentation of '%s' with 9 arguments for type: %s (%ld) at %s: %ld\n",
            name, extractStr(type_name(empty_list, arg0)), arg0->type, file, line);
    abort();
  }
  FnType9 *_fn = (FnType9 *)_arity->fn;
  return(_fn(_arity->closures, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8));
}

int8_t isNothing(Value *v) {
  return(v->type == MaybeType && ((Maybe *)v)->value == (Value *)0);
}

Value *maybe(List *closures, Value *arg0, Value *arg1) {
  Maybe *mVal = malloc_maybe();
  mVal->value = arg1;
  return((Value *)mVal);
}

Value *intValue(int64_t n) {
  Integer *numVal = malloc_integer();
  numVal->numVal = n;
  return((Value *)numVal);
};

Value *pr_STAR(Value *str) {
  int bytes;
  if (str->type == StringType) {
    bytes = fprintf(outstream, "%-.*s", (int)((String *)str)->len, ((String *)str)->buffer);
  } else if (str->type == SubStringType) {
    bytes = fprintf(outstream, "%-.*s", (int)((SubString *)str)->len, ((SubString *)str)->buffer);
  } else {
    fprintf(stderr, "\ninvalid type for 'pr*'\n");
    abort();
  }
  dec_and_free(str, 1);
  return(intValue(bytes));
}

Value *add_ints(Value *arg0, Value *arg1) {
  if (arg0->type != arg1->type) {
    fprintf(stderr, "\ninvalid types for 'add-numbers'\n");
    abort();
  } else {
    Value *numVal = intValue(((Integer *)arg0)->numVal + ((Integer *)arg1)->numVal);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(numVal);
  }
}

Value *integerValue(int64_t n) {
  Integer *numVal = malloc_integer();
  numVal->numVal = n;
  return((Value *)numVal);
};

Value *integer_str(Value *arg0) {
  String *numStr = malloc_string(10);
  snprintf(numStr->buffer, 9, "%ld", ((Integer *)arg0)->numVal);
  numStr->len = strlen(numStr->buffer);
  dec_and_free(arg0, 1);
  return((Value *)numStr);
}

Value *integer_EQ(Value *arg0, Value *arg1) {
  if (IntegerType != arg0->type || IntegerType != arg1->type) {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  } else if (((Integer *)arg0)->numVal != ((Integer *)arg1)->numVal) {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  } else {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  }
}

Value *isInstance(Value *arg0, Value *arg1) {
  int64_t typeNum = ((Integer *)arg0)->numVal;
  if (typeNum == arg1->type) {
     dec_and_free(arg1, 1);
     return(maybe((List *)0, (Value *)0, arg0));
  } else if (StringType == typeNum && SubStringType == arg1->type) {
     dec_and_free(arg1, 1);
     return(maybe((List *)0, (Value *)0, arg0));
  // } else if (HashMapType == typeNum && (BitmapIndexedType == arg1->type ||
                                        // ArrayNodeType == arg1->type ||
                                        // HashCollisionNodeType == arg1->type)) {
     // dec_and_free(arg1, 1);
     // return(maybe((List *)0, (Value *)0, arg0));
  } else {
     dec_and_free(arg0, 1);
     dec_and_free(arg1, 1);
     return(nothing);
  }
}

List *listCons(Value *x, List *l) {
  if (l->type != ListType) {
    fprintf(stderr, "'cons' requires a list\n");
    abort();
  }
  List *newList = malloc_list();
  newList->len = l->len + 1;
  newList->head = (Value *)x;
  newList->tail = l;
  return(newList);
};

Vector *newVector(Value *array[], int indexToSkip) {
  Vector *ret = malloc_vector();
  for (int i = 0; i < VECTOR_ARRAY_LEN; i++) {
    if (array[i] != (Value *)0 && i != indexToSkip) {
      ret->tail[i] = array[i];
      incRef(array[i], 1);
    }
  }
  return(ret);
}

VectorNode *newVectorNode(Value *array[], int indexToSkip) {
  VectorNode *ret = malloc_vectorNode();
  for (int i = 0; i < VECTOR_ARRAY_LEN; i++) {
    if (array[i] != (Value *)0 && i != indexToSkip) {
      ret->array[i] = array[i];
      incRef(array[i], 1);
    }
  }
  return(ret);
}

Value **arrayFor(Vector *v, unsigned index) {
  if (index < v->count) {
    if (index >= v->tailOffset) {
      return(v->tail);
    } else {
      VectorNode *node = v->root;
      for (int level = v->shift; level > 0; level -= 5) {
        node = (VectorNode *)node->array[(index >> level) & 0x1f];
      }
      return(node->array);
    }
  } else {
    fprintf(stderr, "Vector index out of bounds\n");
    abort();
    return((Value **)0);
  }
}

VectorNode *newPath(int level, VectorNode *node) {
  if (level == 0) {
    return(node);
  } else {
    VectorNode *ret = malloc_vectorNode();
    ret->array[0] = (Value *)newPath(level - 5, node);
    return(ret);
  }
}

VectorNode *pushTail(unsigned count, int level, VectorNode *parent, VectorNode *tailNode) {
  int subidx = ((count - 1) >> level) & 0x1f;
  VectorNode *ret;
  if (parent != (VectorNode *)0) {
    ret = newVectorNode(parent->array, subidx);
  } else {
    ret = malloc_vectorNode();
  }
  VectorNode *nodeToInsert;
  if (level == 5) {
    nodeToInsert = tailNode;
  } else {
    VectorNode *child = (VectorNode *)parent->array[subidx];
    if (child != (VectorNode *)0) {
      nodeToInsert = pushTail(count, level - 5, child, tailNode);
    } else {
      nodeToInsert = newPath(level - 5, tailNode);
    }
  }
  ret->array[subidx] = (Value *)nodeToInsert;
  return(ret);
}

Vector *vectCons(Vector *vect, Value *val) {
  // if there's room in the tail
  if (vect->count - vect->tailOffset < VECTOR_ARRAY_LEN) {
    // make a new vector and copy info over
    Vector *newVect = newVector(vect->tail, VECTOR_ARRAY_LEN);
    newVect->shift = vect->shift;
    newVect->count = vect->count + 1;
    if (newVect->count < 32) {
      newVect->tailOffset = 0;
    } else {
      newVect->tailOffset = (newVect->count - 1) & ~0x1f;
    }
    newVect->root = vect->root;
    if (newVect->root != (VectorNode *)0) {
      incRef((Value *)newVect->root, 1);
    }

    // add value to tail of new vector
    newVect->tail[vect->count & 0x1F] = val;
    return(newVect);
  } else {
    // since tail is full, make a new node from the tail of 'vect'
    VectorNode *newRoot;
    VectorNode *tailNode = newVectorNode(vect->tail, VECTOR_ARRAY_LEN);
    int newShift = vect->shift;

    // if the root of 'vect' is completely full
    if ((vect->count >> 5) > (1 << vect->shift)) {
      // make new vector one level deeper
      newRoot = malloc_vectorNode();
      newRoot->array[0] = (Value *)vect->root;
      incRef(newRoot->array[0], 1);

      // and make a new path that includes that node
      newRoot->array[1] = (Value *)newPath(vect->shift, tailNode);
      newShift += 5;
    } else {
      // otherwise, push the tail node down, creating a new root
      newRoot = pushTail(vect->count, vect->shift, vect->root, tailNode);
    }
    Vector *newVect = malloc_vector();
    newVect->count = vect->count + 1;
    newVect->tailOffset = (newVect->count - 1) & ~0x1f;
    newVect->shift = newShift;
    newVect->root = newRoot;
    newVect->tail[0] = val;
    return(newVect);
  }
}

Vector *mutateVectConj(Vector *vect, Value *val) {
  // if 'vect' is a static vector
  if (vect->refs == -1) {
    Vector *result = vectCons(vect, val);
    return(result);
  } else if (vect->count - vect->tailOffset < VECTOR_ARRAY_LEN) {
    // if there's room in the tail, add value to tail of vector
    vect->tail[vect->count & 0x1F] = val;
    vect->count += 1;
    return(vect);
  } else {
    // since tail is full, make a new node from the tail of 'vect'
    VectorNode *newRoot;
    VectorNode *tailNode = newVectorNode(vect->tail, VECTOR_ARRAY_LEN);
    for (unsigned i = 0; i < VECTOR_ARRAY_LEN; i++) {
      dec_and_free(vect->tail[i], 1);
      vect->tail[i] = (Value *)0;
    }
    int newShift = vect->shift;

    // if the root of 'vect' is completely full
    if ((vect->count >> 5) > (1 << vect->shift)) {
      // make new vector one level deeper
      newRoot = malloc_vectorNode();
      newRoot->array[0] = (Value *)vect->root;

      // and make a new path that includes that node
      newRoot->array[1] = (Value *)newPath(vect->shift, tailNode);
      newShift += 5;
    } else {
      // make new vector one level deeper
      // otherwise, push the tail node down, creating a new root
      newRoot = pushTail(vect->count, vect->shift, vect->root, tailNode);
      if (vect->root != (VectorNode *)0)
        dec_and_free((Value *)vect->root, 1);
    }
    vect->count += 1;
    vect->tailOffset = (vect->count - 1) & ~0x1f;
    vect->shift = newShift;
    vect->root = newRoot;
    vect->tail[0] = val;
    return(vect);
  }
}

Value *vectGet(Vector *vect, unsigned index) {
  // this fn does not dec_and_free vect on purpose
  // lets calling functions do that.
  Value **array = arrayFor(vect, index);
  return(array[index & 0x1f]);
}

Value *vectSeq(Vector *vect, int index) {
  List *ret = empty_list;
  if (vect->count > 0) {
    for (int i = vect->count - 1; i >= index; i -= 1) {
      Value *v = vectGet(vect, (unsigned)i);
      incRef(v, 1);
      ret = listCons(v, ret);
    }
  }
  dec_and_free((Value *)vect, 1);
  return((Value *)ret);
}

void destructValue(char *fileName, char *lineNum, Value *val, int numArgs, Value **args[]) {
  if (val->type == ListType) {
    List *l = (List *)val;
    if (l->len < numArgs - 1) {
      fprintf(stderr, "Insufficient values in list for destructuring at %s: %s\n",
	      fileName, lineNum);
      abort();
    }
    int64_t len = l->len - numArgs + 1;
    for (int i = 0; i < numArgs - 1; i++) {
      *args[i] = l->head; l = l->tail;
      incRef(*args[i], 1);
    }
    l->len = len;
    *args[numArgs - 1] = (Value *)l;
    incRef(*args[numArgs - 1], 1);
    dec_and_free(val, 1);
  } else if (val->type == VectorType) {
    Vector *v = (Vector *)val;
    if (v->count < numArgs - 1) {
      fprintf(stderr, "Insufficient values in vector for destructuring at %s: %s\n",
	      fileName, lineNum);
      abort();
    }
    // unpack vector
    for (int i = 0; i < numArgs - 1; i++) {
      *args[i] = vectGet(v, i);
      incRef(*args[i], 1);
    }
    *args[numArgs - 1] = vectSeq(v, numArgs - 1);
  } else {
    fprintf(stderr, "Could not unpack value at %s %s\n", fileName, lineNum);
    abort();
  }
}

Value *strEQ(Value *arg0, Value *arg1) {
  if (arg0->type == StringType &&
      arg1->type == StringType) {
    String *s1 = (String *)arg0;
    String *s2 = (String *)arg1;
    if (s1->len == s2->len && strncmp(s1->buffer,s2->buffer,s1->len) == 0) {
      dec_and_free(arg1, 1);
      return(maybe((List *)0, (Value *)0, arg0));
    } else {
      dec_and_free(arg0, 1);
      dec_and_free(arg1, 1);
      return(nothing);
    }
  } else if (arg0->type == SubStringType &&
             arg1->type == SubStringType) {
    SubString *s1 = (SubString *)arg0;
    SubString *s2 = (SubString *)arg1;
    if (s1->len == s2->len && strncmp(s1->buffer,s2->buffer,s1->len) == 0) {
      dec_and_free(arg1, 1);
      return(maybe((List *)0, (Value *)0, arg0));
    } else {
      dec_and_free(arg0, 1);
      dec_and_free(arg1, 1);
      return(nothing);
    }
  } else if (arg0->type == StringType &&
             arg1->type == SubStringType) {
    String *s1 = (String *)arg0;
    SubString *s2 = (SubString *)arg1;
    if (s1->len == s2->len && strncmp(s1->buffer,s2->buffer,s1->len) == 0) {
      dec_and_free(arg1, 1);
      return(maybe((List *)0, (Value *)0, arg0));
    } else {
      dec_and_free(arg0, 1);
      dec_and_free(arg1, 1);
      return(nothing);
    }
  } else if (arg0->type == SubStringType &&
             arg1->type == StringType) {
    SubString *s1 = (SubString *)arg0;
    String *s2 = (String *)arg1;
    if (s1->len == s2->len && strncmp(s1->buffer,s2->buffer,s1->len) == 0) {
      dec_and_free(arg1, 1);
      return(maybe((List *)0, (Value *)0, arg0));
    } else {
      dec_and_free(arg0, 1);
      dec_and_free(arg1, 1);
      return(nothing);
    }
  } else {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  }
}

Value *strCount(Value *arg0) {
   Value *numVal = integerValue(((String *)arg0)->len);
   dec_and_free(arg0, 1);
   return(numVal);
}

Value *strList(Value *arg0) {
  List *result = empty_list;
  if (arg0->type == StringType) {
    String *s = (String *)arg0;
    for (int64_t i = s->len - 1; i >= 0; i--) {
      SubString *subStr = malloc_substring();
      subStr->type = SubStringType;
      subStr->len = 1;
      subStr->source = arg0;
      subStr->buffer = s->buffer + i;
      result = listCons((Value *)subStr, result);
    }
    incRef(arg0, s->len);
  } else if (arg0->type == SubStringType) {
    SubString *s = (SubString *)arg0;
    for (int64_t i = s->len - 1; i >= 0; i--) {
      SubString *subStr = malloc_substring();
      subStr->type = SubStringType;
      subStr->len = 1;
      subStr->source = arg0;
      subStr->buffer = s->buffer + i;
      result = listCons((Value *)subStr, result);
    }
    incRef(arg0, s->len);
  }
  dec_and_free(arg0, 1);
  return((Value *)result);
}

Value *strVect(Value *arg0) {
  Vector *result = empty_vect;
  if (arg0->type == StringType) {
    String *s = (String *)arg0;
    for (int64_t i = 0; i < s->len; i++) {
      SubString *subStr = malloc_substring();
      subStr->type = SubStringType;
      subStr->len = 1;
      subStr->source = arg0;
      subStr->buffer = s->buffer + i;
      result = mutateVectConj(result, (Value *)subStr);
    }
    incRef(arg0, s->len);
  } else if (arg0->type == SubStringType) {
    SubString *s = (SubString *)arg0;
    for (int64_t i = 0; i < s->len; i++) {
      SubString *subStr = malloc_substring();
      subStr->type = SubStringType;
      subStr->len = 1;
      subStr->source = arg0;
      subStr->buffer = s->buffer + i;
      result = mutateVectConj(result, (Value *)subStr);
    }
    incRef(arg0, s->len);
  }
  dec_and_free(arg0, 1);
  return((Value *)result);
}

Value *integer_LT(Value *arg0, Value *arg1) {
 if (((Integer *)arg0)->numVal < ((Integer *)arg1)->numVal) {
     dec_and_free(arg1, 1);
     return(maybe((List *)0, (Value *)0, arg0));
  } else {
     dec_and_free(arg0, 1);
     dec_and_free(arg1, 1);
     return(nothing);
  }
}

Value *checkInstance(Value *arg0, Value *arg1) {
  int64_t typeNum = ((Integer *)arg0)->numVal;
  if (typeNum == arg1->type) {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  } else if (StringType == typeNum && SubStringType == arg1->type) {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  } else if (HashMapType == typeNum && (BitmapIndexedType == arg1->type ||
                                        ArrayNodeType == arg1->type ||
                                        HashCollisionNodeType == arg1->type)) {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  } else {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  }
}

Value *listMap(Value *arg0, Value *arg1) {
  // List map
  List *l = (List *)arg0;
  if (l->len == 0) {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return((Value *)empty_list);
  } else {
    List *head = empty_list;
    List *tail = empty_list;
    FnArity *arity2;
    if(arg1->type == FunctionType) {
      arity2 = findFnArity(arg1, 1);
      if(arity2 == (FnArity *)0) {
        fprintf(stderr, "\n*** no arity found for '%s'.\n", ((Function *)arg1)->name);
        abort();
      }
    }
    for(Value *x = l->head; x != (Value *)0; l = l->tail, x = l->head) {
      Value *y;
      incRef(x, 1);
      if(arg1->type != FunctionType) {
        incRef(arg1, 1);
        y = invoke1Arg(empty_list, arg1, x);
      } else if(arity2->variadic) {
        FnType1 *fn4 = (FnType1 *)arity2->fn;
        List *varArgs3 = (List *)listCons(x, empty_list);
        y = fn4(arity2->closures, (Value *)varArgs3);
      } else {
        FnType1 *fn4 = (FnType1 *)arity2->fn;
        y = fn4(arity2->closures, x);
      }

      // 'y' is the value for the new list

      if (head == empty_list) {
        // if we haven't started the new list yet
        head = malloc_list();
        head->len = 1;
        head->head = y;
        head->tail = empty_list;
        tail = head;
      } else {
        // otherwise, append to tail of list
        List *new_tail = malloc_list();
        new_tail->len = 1;
        new_tail->head = y;
        new_tail->tail = empty_list;
        tail->tail = new_tail;
        tail = new_tail;
        head->len++;
      }
    }
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return((Value *)head);
  }
}

Value *listConcat(Value *arg0) {
  List *ls = (List *)arg0;

  if (ls->len == 0) {
    dec_and_free(arg0, 1);
    return((Value *)empty_list);
  }
  else if (ls->len == 1) {
    Value *h = ls->head;
    incRef(h, 1);
    dec_and_free((Value *)ls, 1);
    return(h);
  } else {
    List *head = empty_list;
    List *tail = empty_list;
    for (; ls != (List *)0; ls = ls->tail) {
      List *l = (List *)ls->head;
      List *newL;
      int discard = 0;
      if (l != (List *)0 && l->type == VectorType) {
        l = (List *)vectSeq((Vector *)incRef((Value *)l, 1), 0);
        discard = 1;
      }
      Value *x;
      for(; l != (List *)0 && l->head != (Value *)0; l = newL) {
        x = l->head;
        if (head == empty_list) {
          // if we haven't started the new list yet
          head = malloc_list();
          head->len = 1;
          head->head = x;
          incRef(x, 1);
          head->tail = empty_list;
          tail = head;
        } else {
          // otherwise, append to tail of list
          List *new_tail = malloc_list();
          new_tail->len = 1;
          new_tail->head = x;
          incRef(x, 1);
          new_tail->tail = empty_list;
          tail->tail = new_tail;
          tail = new_tail;
          head->len++;
        }
        newL = l->tail;
        if(discard) {
          l->tail = (List *)0;
          dec_and_free((Value *)l, 1);
        }
      }
    }
    dec_and_free(arg0, 1);
    return((Value *)head);
  }
}

Value *car(Value *arg0) {
  List *lst = (List *)arg0;
  if (lst->len == 0) {
    return(nothing);
  } else {
    Value *h = lst->head;
    incRef(h, 1);
    dec_and_free(arg0, 1);
    return(maybe((List *)0, (Value *)0, h));
  }
}

Value *cdr(Value *arg0) {
  List *lst = (List *)arg0;
  if (lst->len == 0) {
    dec_and_free(arg0, 1);
    return((Value *)empty_list);
  } else {
    List *tail = ((List *)arg0)->tail;
    tail->len = lst->len - 1;
    incRef((Value *)tail, 1);
    dec_and_free(arg0, 1);
    return((Value *)tail);
  }
}

Value *integerLT(Value *arg0, Value *arg1) {
  if (((Integer *)arg0)->numVal < ((Integer *)arg1)->numVal) {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  } else {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  }
}

// SHA1 implementation courtesy of: Steve Reid <sreid@sea-to-sky.net>
// and others.
// from http://waterjuice.org/c-source-code-for-sha1/

typedef struct
{
 uint32_t        State[5];
 uint32_t        Count[2];
 uint8_t         Buffer[64];
 } Sha1Context;

#define SHA1_HASH_SIZE           ( 64 / 8 )

typedef struct
{
 uint8_t      bytes [SHA1_HASH_SIZE];
 } SHA1_HASH;

typedef union
{
 uint8_t     c [64];
 uint32_t    l [16];
 } CHAR64LONG16;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) |(rol(block->l[i],8)&0x00FF00FF))
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] ^block->l[(i+2)&15]^block->l[i&15],1))

#define R0(v,w,x,y,z,i)  z += ((w&(x^y))^y)     + blk0(i)+ 0x5A827999 + rol(v,5); w=rol(w,30);
#define R1(v,w,x,y,z,i)  z += ((w&(x^y))^y)     + blk(i) + 0x5A827999 + rol(v,5); w=rol(w,30);
#define R2(v,w,x,y,z,i)  z += (w^x^y)           + blk(i) + 0x6ED9EBA1 + rol(v,5); w=rol(w,30);
#define R3(v,w,x,y,z,i)  z += (((w|x)&y)|(w&x)) + blk(i) + 0x8F1BBCDC + rol(v,5); w=rol(w,30);
#define R4(v,w,x,y,z,i)  z += (w^x^y)           + blk(i) + 0xCA62C1D6 + rol(v,5); w=rol(w,30);

static void TransformFunction(uint32_t state[5], const uint8_t buffer[64]) {
   uint32_t            a;
   uint32_t            b;
   uint32_t            c;
   uint32_t            d;
   uint32_t            e;
   uint8_t             workspace[64];
   CHAR64LONG16*       block = (CHAR64LONG16*) workspace;

   memcpy( block, buffer, 64 );

   // Copy context->state[] to working vars
   a = state[0];
   b = state[1];
   c = state[2];
   d = state[3];
   e = state[4];

   // 4 rounds of 20 operations each. Loop unrolled.
   R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
   R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
   R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
   R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
   R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
   R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
   R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
   R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
   R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
   R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
   R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
   R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
   R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
   R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
   R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
   R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
   R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
   R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
   R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
   R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

   // Add the working vars back into context.state[]
   state[0] += a;
   state[1] += b;
   state[2] += c;
   state[3] += d;
   state[4] += e;
   }

void Sha1Initialise (Sha1Context* Context) {
   // SHA1 initialization constants
   Context->State[0] = 0x67452301;
   Context->State[1] = 0xEFCDAB89;
   Context->State[2] = 0x98BADCFE;
   Context->State[3] = 0x10325476;
   Context->State[4] = 0xC3D2E1F0;
   Context->Count[0] = 0;
   Context->Count[1] = 0;
   }

void Sha1Update (Sha1Context* Context, void* Buffer, int64_t BufferSize) {
   uint32_t    i;
   uint32_t    j;

   j = (Context->Count[0] >> 3) & 63;
   if( (Context->Count[0] += BufferSize << 3) < (BufferSize << 3) )
   {
      Context->Count[1]++;
   }

   Context->Count[1] += (BufferSize >> 29);
   if( (j + BufferSize) > 63 )
   {
      i = 64 - j;
      memcpy( &Context->Buffer[j], Buffer, i );
      TransformFunction(Context->State, Context->Buffer);
      for( ; i + 63 < BufferSize; i += 64 )
      {
         TransformFunction(Context->State, (uint8_t*)Buffer + i);
      }
      j = 0;
   }
   else
   {
      i = 0;
   }

   memcpy( &Context->Buffer[j], &((uint8_t*)Buffer)[i], BufferSize - i );
}

void Sha1Finalise (Sha1Context* Context, SHA1_HASH* Digest) {
   uint32_t    i;
   uint8_t     finalcount[8];

   for( i=0; i<8; i++ )
   {
      finalcount[i] = (unsigned char)((Context->Count[(i >= 4 ? 0 : 1)]
         >> ((3-(i & 3)) * 8) ) & 255);  // Endian independent
   }
   Sha1Update( Context, (uint8_t*)"\x80", 1 );
   while( (Context->Count[0] & 504) != 448 )
   {
      Sha1Update( Context, (uint8_t*)"\0", 1 );
   }

Sha1Update( Context, finalcount, 8 );  // Should cause a Sha1TransformFunction()
   for( i=0; i<SHA1_HASH_SIZE; i++ )
   {
      Digest->bytes[i] = (uint8_t)((Context->State[i>>2] >> ((3-(i & 3)) * 8) ) & 255);
   }
}

Value *integerSha1(Value *arg0) {
  int64_t shaVal;
  Sha1Context context;
  Integer *numVal = (Integer *)arg0;

  Sha1Initialise(&context);
  Sha1Update(&context, (void *)&numVal->type, 8);
  Sha1Update(&context, (void *)&numVal->numVal, 8);
  Sha1Finalise(&context, (SHA1_HASH *)&shaVal);
  dec_and_free(arg0, 1);
  return((Value *)integerValue(shaVal));
}

Value *bitAnd(Value *arg0, Value *arg1) {
  Value *result;
  result = integerValue(((Integer *)arg0)->numVal & ((Integer *)arg1)->numVal);
  dec_and_free(arg0, 1);
  dec_and_free(arg1, 1);
  return(result);
}

Value *bitOr(Value *arg0, Value *arg1) {
  Value *result;
  result = integerValue(((Integer *)arg0)->numVal | ((Integer *)arg1)->numVal);
  dec_and_free(arg0, 1);
  dec_and_free(arg1, 1);
  return(result);
}

Value *addIntegers(Value *arg0, Value *arg1) {
  Value *numVal = integerValue(((Integer *)arg0)->numVal + ((Integer *)arg1)->numVal);
  dec_and_free(arg0, 1);
  dec_and_free(arg1, 1);
  return(numVal);
}

Value *listEQ(Value *arg0, Value *arg1) {
  if (arg1->type != ListType ||
      ((List *)arg0)->len != ((List *)arg1)->len) {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  } else {
    List *l0 = (List *)arg0;
    List *l1 = (List *)arg1;
    for (;
         l0 != (List *)0 && l0->head != (Value *)0 &&
           l1 != (List *)0 && l1->head != (Value *)0;
         l0 = l0->tail, l1 = l1->tail) {
      incRef(l0->head, 1);
      incRef(l1->head, 1);
      if (!equal(l0->head, l1->head)) {
        dec_and_free(arg0, 1);
        dec_and_free(arg1, 1);
        return(nothing);
      }
    }
    dec_and_free(arg1, 1);
    return(maybe(empty_list, (Value *)0, arg0));
  }
}

int8_t equal(Value *v1, Value *v2) {
   Value *equals = equalSTAR((List *)0, v1, v2);
   int8_t notEquals = isNothing(equals);
   dec_and_free(equals, 1);
   return(!notEquals);
}

Value *maybeExtract(Value *arg0) {
  Maybe *mValue = (Maybe *)arg0;
  if (mValue->value == (Value *)0) {
    fprintf(stderr, "The 'nothing' value can not be passed to 'extract'.\n");
    abort();
  }
  incRef(mValue->value, 1);
  Value *result = mValue->value;
  dec_and_free(arg0, 1);
  return(result);
}

Value *fnApply(Value *arg0, Value *arg1) {
  List *argList = (List *)arg1;
  FnArity *_arity = findFnArity(arg0, argList->len);

  if (_arity == (FnArity *)0) {
    fprintf(stderr, "\n*** no arity of '%s' found to apply to %ld args\n",
            ((Function *)arg0)->name, argList->len);
    abort();
  } else if(_arity->variadic) {
    FnType1 *_fn = (FnType1 *)_arity->fn;
    Value *result = _fn(_arity->closures, arg1);
    dec_and_free(arg0, 1);
    return(result);
  } else if (argList->len == 0) {
    FnType0 *_fn = (FnType0 *)_arity->fn;
    Value *result = _fn(_arity->closures);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 1) {
    FnType1 *_fn = (FnType1 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    Value *result = _fn(_arity->closures, appArg0);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 2) {
    FnType2 *_fn = (FnType2 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 3) {
    FnType3 *_fn = (FnType3 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 4) {
    FnType4 *_fn = (FnType4 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 5) {
    FnType5 *_fn = (FnType5 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    argList = argList->tail;
    Value *appArg4 = argList->head; incRef(appArg4, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3, appArg4);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 6) {
    FnType6 *_fn = (FnType6 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    argList = argList->tail;
    Value *appArg4 = argList->head; incRef(appArg4, 1);
    argList = argList->tail;
    Value *appArg5 = argList->head; incRef(appArg5, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3, appArg4, appArg5);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 7) {
    FnType7 *_fn = (FnType7 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    argList = argList->tail;
    Value *appArg4 = argList->head; incRef(appArg4, 1);
    argList = argList->tail;
    Value *appArg5 = argList->head; incRef(appArg5, 1);
    argList = argList->tail;
    Value *appArg6 = argList->head; incRef(appArg6, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3, appArg4, appArg5, appArg6);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 8) {
    FnType8 *_fn = (FnType8 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    argList = argList->tail;
    Value *appArg4 = argList->head; incRef(appArg4, 1);
    argList = argList->tail;
    Value *appArg5 = argList->head; incRef(appArg5, 1);
    argList = argList->tail;
    Value *appArg6 = argList->head; incRef(appArg6, 1);
    argList = argList->tail;
    Value *appArg7 = argList->head; incRef(appArg7, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3, appArg4, appArg5, appArg6, appArg7);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else if (argList->len == 9) {
    FnType9 *_fn = (FnType9 *)_arity->fn;
    Value *appArg0 = argList->head; incRef(appArg0, 1);
    argList = argList->tail;
    Value *appArg1 = argList->head; incRef(appArg1, 1);
    argList = argList->tail;
    Value *appArg2 = argList->head; incRef(appArg2, 1);
    argList = argList->tail;
    Value *appArg3 = argList->head; incRef(appArg3, 1);
    argList = argList->tail;
    Value *appArg4 = argList->head; incRef(appArg4, 1);
    argList = argList->tail;
    Value *appArg5 = argList->head; incRef(appArg5, 1);
    argList = argList->tail;
    Value *appArg6 = argList->head; incRef(appArg6, 1);
    argList = argList->tail;
    Value *appArg7 = argList->head; incRef(appArg7, 1);
    argList = argList->tail;
    Value *appArg8 = argList->head; incRef(appArg8, 1);
    Value *result = _fn(_arity->closures, appArg0, appArg1, appArg2, appArg3, appArg4, appArg5, appArg6, appArg7,
                        appArg8);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(result);
  } else {
    fprintf(outstream, "error in 'fn-apply'\n");
    abort();
  }
}

Value *maybeApply(Value *arg0, Value *arg1) {
  if (isNothing(arg0)) {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  } else if (((List *)arg1)->len == 0) {
    Value *f = ((Maybe *)arg0)->value;
    Value *rslt9;
    FnArity *arity6 = findFnArity(f, 0);
    if(arity6 != (FnArity *)0 && !arity6->variadic) {
      FnType0 *fn8 = (FnType0 *)arity6->fn;
      rslt9 = fn8(arity6->closures);
    } else if(arity6 != (FnArity *)0 && arity6->variadic) {
      FnType1 *fn8 = (FnType1 *)arity6->fn;
      rslt9 = fn8(arity6->closures, (Value *)empty_list);
    } else {
      fprintf(stderr, "\n*** no arity found for '%s'.\n", ((Function *)f)->name);
      abort();
    }
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, rslt9));
  } else {
    List *head = empty_list;
    List *tail;
    for (List *l = (List *)arg1; l->head != (Value *)0; l = l->tail) {
      if (isNothing(l->head)) {
        dec_and_free((Value *)head, 1);
        dec_and_free(arg0, 1);
        dec_and_free(arg1, 1);
        return(nothing);
      } else {
        Value *x = ((Maybe *)l->head)->value;
        incRef(x, 1);
        if (head == empty_list) {
          // if we haven't started the new list yet
          head = malloc_list();
          head->len = 1;
          head->head = x;
          head->tail = empty_list;
          tail = head;
        } else {
          // otherwise, append to tail of list
          List *new_tail = malloc_list();
          new_tail->len = 1;
          new_tail->head = x;
          new_tail->tail = empty_list;
          tail->tail = new_tail;
          tail = new_tail;
          head->len++;
        }
      }
    }

    Value *f = ((Maybe *)arg0)->value;

    incRef(f, 1);
    Value *rslt19 = fnApply(f, (Value *)head);
    Value *rslt20 = maybe(empty_list, (Value *)0, rslt19);
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(rslt20);
  }
}

Value *maybeEQ(Value *arg0, Value *arg1) {
  if (arg1->type == MaybeType &&
      ((Maybe *)arg0)->value == ((Maybe *)arg1)->value) {
    dec_and_free(arg1, 1);
    return(maybe((List *)0, (Value *)0, arg0));
  } else if (arg1->type == MaybeType &&
             ((Maybe *)arg0)->value != (Value *)0 &&
             ((Maybe *)arg1)->value != (Value *)0) {
    incRef(((Maybe *)arg0)->value, 1);
    incRef(((Maybe *)arg1)->value, 1);
    Value *eqResult = equalSTAR((List *)0, ((Maybe *)arg0)->value, ((Maybe *)arg1)->value);
    if (isNothing(eqResult)) {
      dec_and_free(eqResult, 1);
      dec_and_free(arg0, 1);
      dec_and_free(arg1, 1);
      return(nothing);
    } else {
      dec_and_free(eqResult, 1);
      dec_and_free(arg1, 1);
      Value *result = maybe((List *)0, (Value *)0, arg0);
      return(result);
    }
  } else {
    dec_and_free(arg0, 1);
    dec_and_free(arg1, 1);
    return(nothing);
  }
}