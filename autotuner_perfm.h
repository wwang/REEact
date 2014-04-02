#ifndef __REEACT_PERFMON__
#define __REEACT_PERFMON__

//SINGAL used to delivery test signals
#define PERFMON_START_SIGNAL  51
#define PERFMON_STOP_SIGNAL    52

#ifdef __cplusplus
extern "C"{
#endif
int init_reeact_perfm_perthread(int index);
int init_reeact_perfm_app();
int close_reeact_perfm_perthread();
#ifdef __cplusplus
}
#endif

#endif
