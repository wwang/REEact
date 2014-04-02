#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <perfmon/perfmon.h>
#include <perfmon/pfmlib.h>

#include "all.h"
#include "autotuner_perfm.h"

typedef struct over_args {
	pfmlib_event_t ev;
	pfmlib_input_param_t  inp;
	pfmlib_output_param_t outp;
	pfarg_pmc_t pc[PFMLIB_MAX_PMCS];
	pfarg_pmd_t pd[PFMLIB_MAX_PMDS];
	pfarg_ctx_t ctx;
	pfarg_load_t load_arg;
	int fd;
	int core_number;
	pid_t tid;
	pthread_t self;
}perfmon_args;

__thread int thread_index;

int tids[64];
perfmon_args ovs[64];
long long counterValue1[64];
long long counterValue2[64];
long long counterValue3[64];
int next = 0;
pthread_mutex_t tids_mut = PTHREAD_MUTEX_INITIALIZER;

pid_t gettid()
{
  return syscall(SYS_gettid);
}

int registerSigHandler(int sigType, void(*sighandler)(int))
{
  struct sigaction sa = {0};;

  sa.sa_handler = sighandler;

  sigaction(sigType, &sa, NULL);

  return 0;
}

void perfmon_init_perthread(perfmon_args *ov)
{
	int i, fd, ret, num_counters;

	memset(ov, 0, sizeof(struct over_args));

	pfm_get_num_counters(&num_counters);

	ret = pfm_find_full_event("UNHALTED_CORE_CYCLES", &ov->inp.pfp_events[0]);
	if (ret != PFMLIB_SUCCESS)
	  printf("event %s: UNHALTED_CORE_CYCLES\n", pfm_strerror(ret));
	
  	ret = pfm_find_full_event("LAST_LEVEL_CACHE_MISSES", &ov->inp.pfp_events[1]);
	if (ret != PFMLIB_SUCCESS)
	  printf("event %s: Error\n", pfm_strerror(ret));

	ret = pfm_find_full_event("INSTRUCTIONS_RETIRED", &ov->inp.pfp_events[2]);
	//ret = pfm_find_full_event("L2_RQSTS:S_STATE", &ov->inp.pfp_events[2]);
	if (ret != PFMLIB_SUCCESS)
	printf("event %s: INSTRUCTIONS_RETIRED\n", pfm_strerror(ret));

	ov->inp.pfp_event_count = 3;
	ov->inp.pfp_dfl_plm = PFM_PLM3;
	ov->inp.pfp_events[0] = ov->ev;
	
	fd = pfm_create_context(&ov->ctx, NULL, NULL, 0);
	if (fd < 0)
		errx(1, "pfm_create_context failed");

	//fd2ov[fd] = ov;
	ov->fd = fd;
	ov->tid = gettid();
	ov->self = pthread_self();

	if (pfm_dispatch_events(&ov->inp, NULL, &ov->outp, NULL) != PFMLIB_SUCCESS)
		errx(1, "pfm_dispatch_events failed");

	for (i = 0; i < ov->outp.pfp_pmc_count; i++) {
		ov->pc[i].reg_num =   ov->outp.pfp_pmcs[i].reg_num;
		ov->pc[i].reg_value = ov->outp.pfp_pmcs[i].reg_value;
	}
	for (i = 0; i < ov->outp.pfp_pmd_count; i++) {
		ov->pd[i].reg_num = ov->outp.pfp_pmds[i].reg_num;
	}

	ov->pd[0].reg_flags |= PFM_REGFL_OVFL_NOTIFY;
	ov->pd[0].reg_value =  0;
	ov->pd[1].reg_value = 0;
	
	if (pfm_write_pmcs(fd, ov->pc, ov->outp.pfp_pmc_count))
		errx(1, "pfm_write_pmcs failed");

	if (pfm_write_pmds(fd, ov->pd, ov->outp.pfp_pmd_count))
		errx(1, "pfm_write_pmds failed");
	
	ov->load_arg.load_pid = gettid();

	if (pfm_load_context(fd, &ov->load_arg) != 0)
		errx(1, "pfm_load_context failed");

	//pfm_self_start(fd);

	return;
}

//initialize reeact performance monitoring function for each thread
int init_reeact_perfm_perthread( int index)
{
  //pthread_mutex_lock(&tids_mut);
  //thread_index = next++;
  //pthread_mutex_unlock(&tids_mut);

  thread_index = index;
  tids[thread_index] = gettid();

  //perfmon initialization for per-thread monitoring
  perfmon_init_perthread(&(ovs[thread_index]));
   
  return 0;
}

//start performance monitoring
void PerfMonStart( int status )
{
  perfmon_args * ov;
  int i;
     
  ov = &(ovs[thread_index]);

  pid_t tid = gettid();
  STRATA_LOG("autotuner_pfm", "Thread %d: Start perfmon\n", tid);
	
  //erase all performance counters to 0
  for(i=0; i<ov->inp.pfp_event_count; i++)
    {
      ov->pd[i].reg_value=0;
    }
  //write to performance counters	
  pfm_write_pmds(ov->fd, ov->pd, ov->inp.pfp_event_count);

  //start monitoring
  pfm_self_start(ov->fd);

  return;
}

void PerfMonStop(int status)
{
  perfmon_args * ov;

  ov = &(ovs[thread_index]);

  pid_t tid = gettid();

  if (pfm_read_pmds(ov->fd, ov->pd, ov->inp.pfp_event_count))
    {
      printf("pfm_read_pmds error errno %d\n",errno);
      printf("Error name: %s\n", strerror(errno));
      return;
    }
  //"inp_count: %d\n", ov.inp.pfp_event_count);

  counterValue1[thread_index]=ov->pd[0].reg_value;
  counterValue2[thread_index]=ov->pd[1].reg_value;
  counterValue3[thread_index]=ov->pd[2].reg_value;
  STRATA_LOG("autotuner_pfm", "Thread %d: get perfmon results: %llu, %llu, %llu\n", tid, counterValue1[thread_index], counterValue2[thread_index], counterValue3[thread_index]);

  //stop monitoring
  pfm_self_stop(ov->fd);

  return;
}



//initialize reeact performance monitoring function - whole application part 
int init_reeact_perfm_app()
{ 
  if (pfm_initialize() != PFMLIB_SUCCESS)
    errx(1, "pfm_initialize failed");
  
  //register a signal handler to receive the "start to monitor" signal
  //although this signal handler is register for each thread, it still can be 
  //invoked within each threads context
  registerSigHandler(PERFMON_START_SIGNAL, PerfMonStart);		
  registerSigHandler(PERFMON_STOP_SIGNAL, PerfMonStop);		

  return 0;
}

int close_reeact_perfm_perthread()
{
  perfmon_args * ov;

  ov = &(ovs[thread_index]);

  pfm_self_stop(ov->fd);
  close(ov->fd);

  return 0;
}
