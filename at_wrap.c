#include <pthread.h>
#include <stdlib.h>

#include "at_wrap.h"
#include "autotuner.h"
#include "at_policy.h"

//structure used to store real thread routine and argument
typedef struct _at_thread_closure{
  void * (*start_routine)(void *);
  void * arg;
}at_thread_closure;


//This is the thread entrace that finally will be created for the new thread
//Within this function we will do some bookkeeping work for autotuner then call the real thread routine
void * at_pthread_start(void * arg)
{
  at_thread_closure * closure;
  void * ret = NULL;

  closure = (at_thread_closure *)arg;

  strata_log("threading", "strata_pthread_start called\n");
  //add current application thread to thread table
  autotuner_add_this_thread_to_table();
  
  ret = (closure->start_routine)(closure->arg);

  //remove current thread from thread table
  autotuner_rm_this_thread_from_table();
  strata_log("threading", "strata_pthread exiting\n");	

  return ret;
}

#ifdef AUTOTUNER_WRAP
// compile -WI,--wrap,pthread_create
int __wrap_pthread_create(pthread_t * thread, pthread_attr_t * attr,
                          void * (*start_routine)(void *), void * arg)
{
  //this is actaully executed within application context, so it should be save to make any libc calls
  strata_log("threading", "strata_pthread_create called\n");
  at_thread_closure * closure = (at_thread_closure*)malloc(sizeof(at_thread_closure));
 
  // save the actually start_routine & args
  closure->start_routine = start_routine;
  closure->arg = arg;
  
  //!important the line below is changed
  // creating pthread using the wrapped start routine
  return __real_pthread_create(thread, attr, at_pthread_start, closure); 

}
// compile -WI,--wrap,pthread_join
int __wrap_pthread_join(pthread_t thread, void **value_ptr)
{
  return at_pthread_join(thread, value_ptr);
}
#endif

