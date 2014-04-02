#ifndef __AUTOTUNER_WRAP__
#define __AUTOTUNER_WRAP__

#ifdef AUTOTUNER_WRAP

#define at_create_thread __real_pthread_create
#define at_join_thread __real_pthread_join
// compile -WI,--wrap,pthread_create
int __real_pthread_create(pthread_t * thread, pthread_attr_t * attr,
                          void * (*start_routine)(void *), void * arg);
// compile -WI,--wrap,pthread_join
int __real_pthread_join(pthread_t thread, void **value_ptr);

#else

#define at_create_thread pthread_create
#define at_join_thread pthread_join

#endif

#endif
