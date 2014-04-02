#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>
#include <sys/syscall.h>

#include "all.h"
#include "autotuner.h"
#include "at_static_linked_list.h"
#include "autotuner_perfm.h"
#include "at_wrap.h"
#include "at_cpu_util.h"

//autotuner handle for whole application
extern at_handles athandle;

//processor num
int cpu_cnt = 4;

//predefined cpu allocation plan, this should be dynamically generated in the future
int one_task_cpu_plans[3][4] = {{0x3,0x3,0xc,0xc},{0x5,0x5,0xa,0xa},{0x1,0x2,0x4,0x8}};
int one_tasks_cpu_plan_cnt = 2;
int two_tasks_cpu_plans[2][2] = {{0x5,0xa},{0x3,0xc}};
int two_tasks_cpu_plan_cnt = 2;

//perfmon counter results
extern long long counterValue1[64];
extern long long counterValue2[64];
extern long long  counterValue3[64];

//do we want to enable perfmon profiling module, or just use load balancing
extern int AT_ENABLE_PROFILING;
//do we want to enable the whole perfmon policy
extern int AT_ENABLED;

//pin asll tasks' threads in master
int at_pin_all_threads(at_handles * ath);

int at_run_os_sched_mode(at_handles * ath);

//sending signal to a particuler thread
int tkill(int tid, int sig)
{
  return syscall(SYS_tkill, tid, sig);
}


//sending perfmon start signal to all threads in this application
int at_start_perfm(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  int j = t->thread_table_head;
  while( j != -1 )
    {
      if(t->threads_table[j].active) //only pin active threads
	{
	  STRATA_LOG("autotunerd", "Task %d: sending perfmon start signal to thread %d\n", ath->index, t->threads_table[j].tid);
	  tkill(t->threads_table[j].tid, PERFMON_START_SIGNAL);
	}
      j = t->threads_table[j].next;
    }  

  return 0;
}

//sending perfmon start signal to all threads in this application
int at_stop_perfm(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  int j = t->thread_table_head;
  while( j != -1 )
    {
      if(t->threads_table[j].active) //only pin active threads
	{
	  STRATA_LOG("autotunerd", "Task %d: sending perfmon stop signal to thread %d\n", ath->index, t->threads_table[j].tid);
	  tkill(t->threads_table[j].tid, PERFMON_STOP_SIGNAL);
	}
      j = t->threads_table[j].next;
    }  

  return 0;
}

//collect results returned from each threads in this application
int at_collect_perfm_results(at_handles * ath, double * avg_ipc)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;
  
  //tell all threads to start sampling
  at_start_perfm(ath);
  //sleep for 1sec, let each thread to run
  sleep(1);
  //tell all threads to stop sampling
  at_stop_perfm(ath);

  //collect results
  //first, sleep 0.5 seconds, give other threads the chance to respond
  //NOTE: This is not a precise implementation, sometimes we may not be able to collect all results. However, this method has little overhead.
  usleep(500000);
  
  int j = t->thread_table_head;
  int k = 0; 
  double ipc = 0;
  long long total_insn = 0;
  while(j != -1 )
    {
      if(t->threads_table[j].active)
	{
	  if( counterValue1[j] != 0 )
	    {
	      double tempipc = (((double)counterValue3[j])/((double)counterValue1[j]));
	      total_insn += counterValue3[j];
	      ipc = ipc + tempipc;
	      STRATA_LOG("autotunerd", "Computing IPC from %lld / %lld = %f, accmulated IPC %f\n", counterValue3[j], counterValue1[j], tempipc, ipc);
	      k++;
	    }
	}
      j = t->threads_table[j].next;
    }

  *avg_ipc = 0;
  if(k != 0)
    *avg_ipc = ipc/k;
  
  t->insn_retired = total_insn;

  STRATA_LOG("autotunerd", "Returning IPC %f\n", *avg_ipc);

  return 0;
}


int at_sampling_one_task(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;
  int best_plan_index = 0;
  double avg_ipc1, avg_ipc2;

  //only one thread, no need to do sampling
  //sem_post((sem_t*)ath->shmem_sem);
  STRATA_LOG("autotunerd", "There is only one task for sampling\n");

  //if there are more than or less than 2 threads, then using all available cores is the only correct way of scheduling
  //so there is no need to sample for more than 2 cores
  if(t->active_thread_count != 2)
    return -2;

  //test first plan
  STRATA_LOG("autotunerd", "Sampling Plan 1\n");
  t->cpus[0] = one_task_cpu_plans[0][0];
  t->cpus[1] = one_task_cpu_plans[0][1];
  t->cpus[2] = one_task_cpu_plans[0][2];
  t->cpus[3] = one_task_cpu_plans[0][3];
  pin_threads_as_plan(ath);
  at_collect_perfm_results(ath, &avg_ipc1);
  STRATA_LOG("autotunerd", "Sampled Plan 1, with avg ipc %f\n", avg_ipc1);

  //test second plan
  STRATA_LOG("autotunerd", "Sampling Plan 2\n");
  t->cpus[0] = one_task_cpu_plans[1][0];
  t->cpus[1] = one_task_cpu_plans[1][1];
  t->cpus[2] = one_task_cpu_plans[1][2];
  t->cpus[3] = one_task_cpu_plans[1][3];
  pin_threads_as_plan(ath);
  at_collect_perfm_results(ath, &avg_ipc2);
  STRATA_LOG("autotunerd", "Sampled Plan 2, with avg ipc %f\n", avg_ipc2);
	  

  if( avg_ipc1 >= avg_ipc2 )
    best_plan_index = 0;
  else
    best_plan_index = 1;
  STRATA_LOG("autotunerd", "New Best Plan %d for Single Task\n", best_plan_index);

  return best_plan_index;
}

int at_sampling_two_tasks(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  //only sampling when two tasks have the nearly same number of active threads, i.e. |threadcountA - threadcountB| <= 1
  //otherwise, we need to test if isolation is better then load balancing or cpu usage priority mode
  int ignore_first_plan = 0;
  int use_insn_retired = 0;
  if( (abs(m->tl[m->head].active_thread_count - m->tl[m->tail].active_thread_count) > 1) ||
      m->cpu_usage_priority_mode )
    {
      ignore_first_plan = 1;
      //TODO:this is just for testing
      //use_insn_retired = 1;
    }
  STRATA_LOG("autotunerd", "Sampling 2 tasks:CPU Usage Mode %d, Ignore First Plan %d, Use_insn_retired %d\n", m->cpu_usage_priority_mode, ignore_first_plan, use_insn_retired);
     
  int i = m->head;
  int j,k;
  int cur_plan_index = 0;
  double best_value = 0;
  long long best_value_ir = 0;
  int best_plan_index = 0;
  task_list_item * tmp;
  while(cur_plan_index < 2)
    {
      STRATA_LOG("autotunerd", "Sampling 2 tasks with plan %d\n", cur_plan_index);
      i = m->head;
      k = 0; //task index in the plans array
      while(i != -1)
	{
	  tmp = (m->tl) + i;
	  if( !(cur_plan_index == 0 && ignore_first_plan) )
	    {
	      for(j = 0; j < tmp->active_thread_count; j++)
		tmp->cpus[j] = two_tasks_cpu_plans[cur_plan_index][k];
	      STRATA_LOG("autotunerd", "Setting up sampling plan for task %d with plan %d,%d,%d,%d\n", i, m->tl[i].cpus[0], m->tl[i].cpus[1], m->tl[i].cpus[2], m->tl[i].cpus[3]); 
	    }
	  STRATA_LOG("autotunerd", "Finished setting up sampling plan for task %d\n", i);

	  //send message out if they are not this task (aka master)
	  if( i != ath->index)
	    {
	      STRATA_LOG("autotunerd", "Sending message TYPE_TEST to task %d\n", i);
	      at_message msg;
	      msg.sender_id = ath->index;
	      msg.type = AT_MSG_TYPE_TEST;
	      msg.value = 0;
	      at_send_message(m, &msg, i);
	    }
	  
	  i = tmp->next;
	  k++;
	}
  
      //do self sampling
      {
	double avg_ipc = 0;
	pin_threads_as_plan(ath);
	at_collect_perfm_results(ath, &avg_ipc);
	t->avg_ipc = avg_ipc;
	STRATA_LOG("autotunerd", "Master self sampling, result is %f\n", avg_ipc);
      }
  
      at_message recv_msg;
      int type;
      int break_out = 0;
      i = 0;
      while(i < (m->task_cnt - 1))
	{
	  //wait for sampling reply
	  STRATA_LOG("autotunerd", "Master waiting for sampling replying\n");
	  sem_wait((sem_t*)ath->comm_sem);
      
	  //process the message, it could be all kinds of messages
	  //sem_wait((sem_t*)ath->comm_sem);
	  
	  type = at_receive_message(ath, &recv_msg);
	  
	  //sem_post((sem_t*)ath->shmem_sem);
	  
	  if(type == AT_MSG_TYPE_QUIT)
	    {
	      //the whole application is going to quit
	      STRATA_LOG("autotuner", "Got quit message while master waiting for test result\n");
	      return -1;
	    }
	  else if(type == AT_MSG_TYPE_DATA_READY)
	    {
	      STRATA_LOG("autotunerd", "Master got one sampling reply from %d\n", recv_msg.sender_id);
	      i++;
	    }
	  else if(type == AT_MSG_TYPE_THREAD_COUNT_CHANGED)
	    {
	      STRATA_LOG("autotunerd", "Master %d: Waiting for sampling results but got thread count change message, stop sampling and go for load balancing and cpu usage test");
	      return -3;
	    }
	 
	  //ignore all other messages... this is not so good, we need to fix it
	}
  
      if( !use_insn_retired )
	{
	  STRATA_LOG("autotunerd", "Master got all sampling replies with ipc %f and ipc %f\n",  m->tl[m->head].avg_ipc, m->tl[m->tail].avg_ipc);
	  
	  double avg_ipc = (m->tl[m->head].avg_ipc + m->tl[m->tail].avg_ipc)/2;
	  STRATA_LOG("autotunerd", "Master got new avg ipc %f\n", avg_ipc);
	  if( avg_ipc > best_value )
	    {
	      best_value = avg_ipc;
	      best_plan_index = cur_plan_index;
	      STRATA_LOG("autotunerd", "New best plan is %d\n", best_plan_index);
	    }
	}
      else
	{
	  STRATA_LOG("autotunerd", "Master got all sampling replies with insn retired  %lld and  %lld\n",  m->tl[m->head].insn_retired, m->tl[m->tail].insn_retired);
	  
	  long long total_insn = m->tl[m->head].insn_retired + m->tl[m->tail].insn_retired;
	  STRATA_LOG("autotunerd", "Master got new total insn retired %lld\n", total_insn);
	  if( total_insn > best_value_ir )
	    {
	      best_value_ir = total_insn;
	      best_plan_index = cur_plan_index;
	      STRATA_LOG("autotunerd", "New best plan is %d\n", best_plan_index);
	    }
	}
      //test next plan
      cur_plan_index++;      
    }

  if(best_plan_index == 0 && ignore_first_plan)
    best_plan_index = -3;

  return best_plan_index;
}

//assume share memory lock acquired
//nobody is allowed to quit during testing
//returns the number of best plan. 
//if return value is -1, then this master task is going to quit
//if return value is -2, then there is no plan for current running tasks
//if return value is -3, then use load balancing mode or cpu usage priority mode
int at_start_sampling(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  int sampling_not_done = 1;
  int cur_plan_index = 0;
  int best_plan_index = 0;
  double best_value = 0;

  //sem_wait((sem_t*)ath->shmem_sem);
  if(m->task_cnt == 1)
    {
      return at_sampling_one_task(ath);
    }
  else if(m->task_cnt == 2)
    {
      return at_sampling_two_tasks(ath);
    }
 
  //for the rest number of tasks, we don't have an implmentation yet
  return -2;
}

//pin threads in this application as plan
int pin_threads_as_plan(at_handles * ath)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  int j = t->thread_table_head;
  int k = 0;
  cpu_set_t cpus;
  int * p = (int*)&cpus;
  int iRet = 0;
  while( j != -1 )
    {
      if(t->threads_table[j].active) //only pin active threads
	{
	  CPU_ZERO(&cpus);
	  *p = t->cpus[k];//this is a little bit tricky, cpu_set_t is actually a bit vector, but it is declared into a structure, so I have to manually write to its memory
	  STRATA_LOG("autotunerd", "Task %d: Self assigning thread %d to core map 0x%x\n", ath->index, t->threads_table[j].tid, t->cpus[k]);
	  iRet = sched_setaffinity(t->threads_table[j].tid, sizeof(cpu_set_t), &cpus);
	  if(iRet != 0 )
	    STRATA_LOG("autotuner", "Failed set affinity of thread %d to core 0x%x\n", t->threads_table[j].tid, t->cpus[k]);
	  k++;
	}
      j = t->threads_table[j].next;
    }  

  return 0;
}

int run_as_plan_one_task(at_handles * ath, int plan_no)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  //set up the scheduling plan
  int i;
  for(i = 0; i < t->active_thread_count; i++)
    {
      t->cpus[i] = one_task_cpu_plans[plan_no][i%4];
    }
  //pin threads following this plan
  pin_threads_as_plan(ath);

  return 0;
}

int run_as_plan_two_tasks(at_handles * ath, int plan_no)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  int i = m->head;
  int j,k;
  task_list_item * tmp = NULL;
  k = 0;
  while(i != -1)
    {
      //for first task
      tmp = (m->tl) + i;
      for(j = 0; j < tmp->active_thread_count; j++)
	tmp->cpus[j] = two_tasks_cpu_plans[plan_no][k];
      STRATA_LOG("autotunerd", "Setting up sampling plan for task %d with plan %d,%d,%d,%d\n", i, m->tl[i].cpus[0], m->tl[i].cpus[1], m->tl[i].cpus[2], m->tl[i].cpus[3]); 
      
      //send message out if they are not this task (aka master)
      if( i != ath->index)
	{
	  STRATA_LOG("autotunerd", "Sending message TYPE_RUN_AS_PLAN to task %d\n", i);
	  at_message msg;
	  msg.sender_id = ath->index;
	  msg.type = AT_MSG_TYPE_RUN_AS_PLAN;
	  msg.value = 0;
	  at_send_message(m, &msg, i);
	}
      
      i = tmp->next;
      k++;
    }

  //pin master's threads as plan
  pin_threads_as_plan(ath);
  
  return 0;
}

//tell the slaves to follow the best plan
int run_as_plan(at_handles * ath, int plan_no)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;
  
  if( m->task_cnt == 1 ) //only valid for one or two tasks
    {
      run_as_plan_one_task(ath, plan_no);
    }
  else if(m->task_cnt == 2)
    {
      run_as_plan_two_tasks(ath, plan_no);  
    }
  
  return 0;
}

int at_msg_slave_quit_mst(at_handles * ath, at_message * msg)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  //if a slave is quitting
  STRATA_LOG("autotuner", "Slave task %d is quitting\n", msg->value);
  //reload balancing they system
  at_load_balance_2L2(ath);
  //redetect processor usage
  at_improve_cpu_usage(ath);

  return 0;
}

int at_msg_quit_mst(at_handles * ath, at_message * msg)
{
  return 0;
}

int at_msg_timeout_mst(at_handles * ath, int * quit)
{
  //here comes new sampling. Nobody is allowed to quit during sampling, and nobody is allow to join
  STRATA_LOG("autotunerd", "Master %d wake up due to time out\n", ath->pid);
  
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  //if we are in OS scheduling mode, then we do not have to sample
  if(m->os_sched_mode)
    return 0;

  //lost the shared memory so no task can join no task can quit
  sem_wait((sem_t*)ath->shmem_sem);
  m->slave_not_allowed_to_quit = 1;
  int iRet = at_start_sampling(ath);
  if( iRet == -1 )
    {
      //got quitting message while sampling
      STRATA_LOG("autotunerd", "Master %d got quit message will sampling, now going to quit\n", ath->pid);
      m->slave_not_allowed_to_quit = 0;
      sem_post((sem_t*)ath->shmem_sem); //release this lock so other tasks can quit as well
      task_unregister(ath, 1);
      *quit = 1;
      return 0;
    }
  else if( iRet == -3 )
    {
      //load balancing first then cpu usage priority mode
      STRATA_LOG("autotunerd", "Master %d: We are going to follow load balancing and cpu usage priority mode %d\n", ath->index, iRet);
      //load balance the system                                                                                                                                           
      at_load_balance_2L2(ath);
      //pin all task threads                                                                                                                                              
      at_pin_all_threads(ath);
      //see if we need improve cpu usage
      at_improve_cpu_usage(ath);
    }
  else if( iRet >= 0 )
    {
      //we have a best plan, tell slaves to follow it
      STRATA_LOG("autotunerd", "Master %d: We have found a good plan %d\n", ath->index, iRet);
      m->cpu_usage_priority_mode = 0;
      run_as_plan(ath, iRet);
    }
  
  //slave is free to quit
  m->slave_not_allowed_to_quit = 0;
  sem_post((sem_t*)ath->shmem_sem);
  
  return 0;
}

int at_msg_test_slv(at_handles * ath, at_message * msg)
{
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;

  //we have some sampling to do.
  STRATA_LOG("autotunerd", "Slave %d got msg TYPE_TEST, with plan %d,%d,%d,%d\n", ath->index, t->cpus[0], t->cpus[1], t->cpus[2], t->cpus[3]);
  
  //do sampling
  {
    pin_threads_as_plan(ath);
    at_collect_perfm_results(ath, &(t->avg_ipc));
    STRATA_LOG("autotunerd", "Slave got sampling result %f\n", t->avg_ipc);
  }
  
  //tell master we have done the sampling
  STRATA_LOG("autotunerd", "Slave sending sampling results back \n");
  at_message outmsg;
  outmsg.value = 0;
  outmsg.sender_id = ath->index;
  outmsg.type = AT_MSG_TYPE_DATA_READY;
  at_send_message(m, &outmsg, m->master_id);	  

  return 0;
}

int at_msg_quit_slv(at_handles * ath, at_message * msg)
{
  return 0;
}

int at_msg_mst_quit_slv(at_handles * ath, at_message * msg)
{
  return 0;
}

int at_msg_run_plan_slv(at_handles * ath, at_message * msg)
{
  STRATA_LOG("autotunerd", "Slave %d got msg TYPE_RUN_AS_PLAN\n", ath->index);
  pin_threads_as_plan(ath);

  return 0;
}

//find available thread table cell
int at_find_thread_table_cell(task_list_item * t)
{
  int i;
  int index = -1;
  for(i = 0; i < THREAD_TABLE_LENGTH; i++)
    {
      if(t->threads_table[i].valid == 0)
	{
	  index = i;
	  break;
	}
    }

  return index;
}


//add current application thread to thread table
int autotuner_add_this_thread_to_table()
{
  if(!AT_ENABLED)
    return 0;

  shared_mem * m = athandle.shmem;
  task_list_item * t = (m->tl) + athandle.index;

  //no need to use lock by far since Strata provides a lock during thread startup
  pthread_mutex_lock(&(t->thread_table_lock));

  pid_t tid = syscall(SYS_gettid);
 
  int index = at_find_thread_table_cell(t);
  STRATA_LOG("autotunerd", "Adding new thread to slot %d, id is %d\n", index, tid);

  if(index != -1 )
  {
    t->threads_table[index].tid = tid;
    t->threads_table[index].valid = 1;
    t->threads_table[index].pthread_handle = (void*)pthread_self();
    t->threads_table[index].active = 1;
    at_slist_insert(&(t->thread_table_head), &(t->thread_table_tail), t->threads_table, sizeof(thread_table_item), index);
  }
  
  //perfmon initialization for each thread
  if(t->tbl_index <= THREAD_TABLE_LENGTH)
    init_reeact_perfm_perthread(index);
  
  t->thread_count++;
  t->active_thread_count++;

  t->tbl_index++;
  pthread_mutex_unlock(&(t->thread_table_lock));
  
  if(t->tbl_index <= THREAD_TABLE_LENGTH)
  {
   //notify master that this task's thread number changed
    at_message msg;
    msg.sender_id = athandle.index;
    msg.type = AT_MSG_TYPE_THREAD_COUNT_CHANGED;
    msg.value = 0;
    STRATA_LOG("autotunerd", "Sending thread count changed message to master %d\n", m->master_id);
    at_send_message(m, &msg, m->master_id);//there may be a problem, if the master is quitting then the message is acutally lost, so every new master must rebalance the system

    //tell the master to do sampling and find best scheduling plan, since we are probably in ROI now
    msg.sender_id = athandle.index;
    msg.type = AT_MSG_TYPE_RESET_TIMER;
    msg.value = 3; //TODO: this number should be changed to 3 seconds, since we are now doing another 2 seconds cpu usage test
    STRATA_LOG("autotunerd", "Sending thread count changed message to master %d\n", m->master_id);
    at_send_message(m, &msg, m->master_id);      
  }
  else
    {
      //TODO:possible race condition
      m->os_sched_mode = 1;
      //this is the application that requested OS scheduling mode
      t->os_sched_mode_req = 1; 
    }

  return 0;
}

//remove current application thread from thread table
//no re-load-balancing triggered in this function since usually at this moment, the application is going to quit or new threads will be spawned
int autotuner_rm_this_thread_from_table()
{
  if(!AT_ENABLED)
    return 0;

  shared_mem * m = athandle.shmem;
  task_list_item * t = (m->tl) + athandle.index;
  
  int i,k;
  pid_t tid = (pid_t) syscall(SYS_gettid);
  
  STRATA_LOG("autotunerd", "Removing thread, id is %d\n", tid);
  //no need to use lock by far
  pthread_mutex_lock(&(t->thread_table_lock));
  i = t->thread_table_head;
  while(i != -1)
    {
      if( tid == t->threads_table[i].tid )
	{
	  at_slist_remove(&(t->thread_table_head), &(t->thread_table_tail), t->threads_table, sizeof(thread_table_item), i);
	  t->threads_table[i].tid = -1;
	  t->threads_table[i].active = 0;
	  t->threads_table[i].valid = 0;
	  t->thread_count--;
	  t->active_thread_count--;
	  pthread_mutex_unlock(&(t->thread_table_lock));
	  STRATA_LOG("autotunerd", "Thread %d Removed\n", tid);
	  return 0;
	}
      i = (t->threads_table[i]).next;
    }
  pthread_mutex_unlock(&(t->thread_table_lock));

  //perfmon per thread cleanning up
  close_reeact_perfm_perthread();

  return -1;
}

//redistribute all tasks' threads for a 2 L2 processor, make the system load balanced
int at_load_balance_2L2(at_handles * ath)
{
  shared_mem * m = ath->shmem;

  m->cpu_usage_priority_mode = 0;
  
  //first get the total number of threads 
  int i = m->head;
  int j;
  int th_cnt = 0;
  int min_th_cnt = 1000;
  int min_th_task = -1;
  int task_cnt = 0;
  while(i != -1)
    {
      th_cnt += m->tl[i].active_thread_count;

      //find the task with the minimal thread count, we gonna pin it first to ensure its processor time
      if(min_th_cnt > m->tl[i].active_thread_count)
	{
	  min_th_cnt = m->tl[i].active_thread_count;
	  min_th_task = i;
	  STRATA_LOG("autotunerd", "New Smallest Task is %d with %d threads\n", min_th_task, min_th_cnt);
	}

      task_cnt++;
      i = m->tl[i].next;
      STRATA_LOG("autotunerd", "Load balancing reading next application %d's thread count\n", i);
    }

  STRATA_LOG("autotunerd", "Total Thread count is %d\n", th_cnt);

  if(task_cnt == 1) //if there is only one task then we just pin its threads to the cores one by one
    {
      int tmpcpus[4] = {0x1, 0x2, 0x4, 0x8};
      for( j = 0; j < m->tl[min_th_task].active_thread_count; j++)
	{
	  m->tl[min_th_task].cpus[j] = tmpcpus[j%4];
	}
      return 0;
    }

  //second, compute how many threads per L2 cache
  int fst_L2_th_cnt = th_cnt/2;
  int snd_L2_th_cnt = th_cnt - fst_L2_th_cnt;
  STRATA_LOG("autotunerd", "First L2 run %d threads, second L2 run %d threads\n", fst_L2_th_cnt, snd_L2_th_cnt);

  //allocate processors to the smallest task first
  for(j = 0; j < m->tl[min_th_task].active_thread_count;j++)
    {
      STRATA_LOG("autotunerd", "Assigning Task %d to 0x3\n", min_th_task);
      m->tl[min_th_task].cpus[j]=0x3;
      fst_L2_th_cnt--;
    }

  //go over each task, assigned an L2 cache to it
  i = m->head;
  while(i != -1)
    {
      if( i != min_th_task )
	{
	  for(j = 0; j < m->tl[i].active_thread_count; j++)
	    {
	      if(fst_L2_th_cnt > 0)
		{
		  STRATA_LOG("autotunerd", "Assigning Task %d to 0x3\n", i);
		  m->tl[i].cpus[j] = 0x3; //assign to core 0 and 1
		  fst_L2_th_cnt--;
		}
	      else if(snd_L2_th_cnt > 0)
		{
		  STRATA_LOG("autotunerd", "Assigning Task %d to 0xc\n", i);
		  m->tl[i].cpus[j] = 0xc;//assign to core 2 and 3
		  snd_L2_th_cnt--;
		}
	      else
		{
		  STRATA_LOG("autotunerd", "Assigning Task %d to 0xf\n", i);
		  m->tl[i].cpus[j] = 0xf;//some threads were just created before we can handle it, allocate all 4 cores for it, we have to handle it any way later
		}
	    }
	}
      
      i = m->tl[i].next;
    }

  return 0;
}

//pin asll tasks' threads in master
int at_pin_all_threads(at_handles * ath)
{
  shared_mem * m = ath->shmem;

  //go over each tasks and pin there threads
  int i = m->head;
  int j,k; //j used to go over task's thread table, and k used to go over task's cpu affinities
  task_list_item * t = NULL; //pointer to current task
  cpu_set_t cpus;
  int * p = (int*)&cpus;
  int iRet = 0;
  while(i != -1 )
    {
      t = (m->tl) + i;
      j = t->thread_table_head;
      k = 0;
      while( j != -1 )
	{
	  if(t->threads_table[j].active) //only pin active threads
	    {
	      CPU_ZERO(&cpus);
	      if( t->cpus[k] != 0x0)
	      	*p = t->cpus[k];//this is a little bit tricky, cpu_set_t is actually a bit vector, but it is declared into a structure, so I have to manually write to its memory
	      else
	      {
		//STRATA_LOG("autotunerd", "Crashed Here?\n");
		*p = 0xf;
	      }

	      STRATA_LOG("autotunerd", "Task %d: Assigning thread %d to core map 0x%x\n", i, t->threads_table[j].tid, t->cpus[k]);
	      iRet = sched_setaffinity(t->threads_table[j].tid, sizeof(cpu_set_t), &cpus);
	      if(iRet != 0 )
		STRATA_LOG("autotuner", "Failed set affinity of thread %d to core 0x%x\n", t->threads_table[j].tid, t->cpus[k]);
	      k++;
	    }
	  j = t->threads_table[j].next;
	}
      i = t->next;
    }

  return 0;
}

//master message function for AT_MSG_TYPE_THREAD_COUNT_CHANGED
int at_msg_th_cnt_chg_mst(at_handles * ath, at_message * msg)
{
  STRATA_LOG("autotunerd", "Rebalancing the system\n");
  //load balance the system
  at_load_balance_2L2(ath);
  //pin all task threads
  at_pin_all_threads(ath);

  return;
}

//phtread_join call will be transferred here. We will detect if the calling thread will be blocked.
//If it will be, then we will release its processor
int at_pthread_join(pthread_t thread, void **value_ptr) 
{
    if(!AT_ENABLED)
      return at_join_thread(thread, value_ptr);
  
  shared_mem * m = athandle.shmem;
  task_list_item * t = (m->tl) + athandle.index;  

  STRATA_LOG("autotunerd", "Pthread join called\n");

  pid_t tid = (pid_t) syscall(SYS_gettid);
  int caller_id = -1;

  //go over the thread table, find corresponding thread and see if they are the same
  int i = t->thread_table_head;
  int caller_blocked = 1;
  at_message msg;
  while( i != -1 )
    {
      STRATA_LOG("autotunerd", "Checking thread %d status\n", t->threads_table[i].tid );
      if(pthread_equal((pthread_t)t->threads_table[i].pthread_handle, thread))
	{
	  if(t->threads_table[i].tid == -1)
	    {
	      //if this thread is dead, then calling thread is probably not going to be blocked
	      caller_blocked = 0;
	    }
	}
      else if(t->threads_table[i].tid == tid )
	  caller_id = i;

      i = t->threads_table[i].next;
    }

  if(caller_blocked)
    {
      STRATA_LOG("autotunerd", "Pthread join: Caller thread will be blocked\n");
      t->threads_table[caller_id].active = 0;
      t->active_thread_count--;

      //Set this thread's affinity to any cores; later any other thread created by this thread will be allowed
      //to run on any processor; this is necessary for some crazy thread creating threads
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(0, &cpuset);
      CPU_SET(1, &cpuset);
      CPU_SET(2, &cpuset);
      CPU_SET(3, &cpuset);
      sched_setaffinity(t->threads_table[caller_id].tid, sizeof(cpu_set_t), &cpuset);

      //tell the master to reload balance the system
      if(t->tbl_index <= THREAD_TABLE_LENGTH)
      {
      	msg.sender_id = athandle.index;
      	msg.type = AT_MSG_TYPE_THREAD_COUNT_CHANGED;
      	msg.value = 0;
      	STRATA_LOG("autotunerd", "Sending thread count changed message to master %d\n", m->master_id);
      	at_send_message(m, &msg, m->master_id);      
      }
      
    }

  int iRet = at_join_thread(thread, value_ptr);

  //if caller was blocked, then we have to reallocate processors to it
  if(caller_blocked)
    {
      STRATA_LOG("autotunerd", "Pthread join: Caller thread is released\n");
      t->threads_table[caller_id].active = 1;
      t->active_thread_count++;

      if(t->tbl_index <= THREAD_TABLE_LENGTH)
      {
      	msg.sender_id = athandle.index;
      	msg.type = AT_MSG_TYPE_THREAD_COUNT_CHANGED;
      	msg.value = 0;
      	STRATA_LOG("autotunerd", "Sending thread count changed message to master %d\n", m->master_id);
      	at_send_message(m, &msg, m->master_id);      
      }
    }

  return iRet;
}


int at_get_total_thread_cnt(at_handles * ath)
{
    shared_mem * m = ath->shmem;
    
    //first get the total number of threads                                                                                                                                
    int i = m->head;
    int th_cnt = 0;
    while(i != -1)
      {
	th_cnt += m->tl[i].active_thread_count;

	i = m->tl[i].next;
      }

    return th_cnt;
}


int at_release_under_used_cpu(at_handles * ath, int * cpulist, int number)
{
  shared_mem * m = ath->shmem;
  int i,j;
  int cpu_map = 0;
  
  //prepare bitmap for released processors
  for(i = 0; i < number; i++)
    {
      if(cpulist[i] == 1)
	{
	  cpu_map |= (1 << i);
	}
    }

  STRATA_LOG("autotuner", "CPU Usage Mode: Releasing Processors Map 0x%x\n", cpu_map);
  //give processors to each thread
  i = m->head;
  while(i != -1)
    {
      for(j = 0; j < m->tl[i].active_thread_count; j++)
	{
	  m->tl[i].cpus[j] |= cpu_map;//some threads were just created before we can handle it, allocate all 4 cores for it, we have to handle it any way later          
	}

      i = m->tl[i].next;
    }

  return 0;
}

//use this function after isolation to see if load balancing is cause significant processor time wasting
//however, wasting processor time is not always bad, as in the case of Streamcluster and Bodytrack, so
//we still have to do sampling to test
int at_improve_cpu_usage(at_handles * ath)
{
  shared_mem * m = ath->shmem;

  m->cpu_usage_priority_mode = 0;

  //if application has requested OS scheduling mode, then we has to do OS scheduling
  if(m->os_sched_mode)
    at_run_os_sched_mode(ath);

  //if there is only one application or there are less threads than processors than we should quit
  if(m->task_cnt == 1)
    return 0;

  if(at_get_total_thread_cnt(ath) <= cpu_cnt)
    return 0;
  
  
  //get cpu usage for each processor
  double cus[4] = {0};
  at_get_last_ncpu_usage(cus, 4000000, 4);
  STRATA_LOG("autotunerd", "Got cpu usage: %f, %f, %f. %f\n", cus[0], cus[1], cus[2], cus[3]);


  int i = 0;
  int release[4] = {0};
  int released_cpu_cnt = 0;
  
  //if both processors on 2 L2 cache has less than 160% usage, there is no need to improve
  if( (cus[0] + cus[1]) < 1.80 &&
      (cus[2] + cus[3]) < 1.80 )
    return 0;
  //if both processors on 2 L2 cache has more than 160% usage, there is no need to improve
  if( (cus[0] + cus[1]) >= 1.80 &&
      (cus[2] + cus[3]) >= 1.80 )
    return 0;
  
  //release processor whos usage is below 80%
  for(i = 0; i < cpu_cnt; i++)
    {
      if(cus[i] < 0.9)
	{
	  release[i] = 1;
	  released_cpu_cnt++;
	}
    }

  //if we have to release more than 2 processors, then the overall system usage will probably never improved
  //mathematically, this part should never be executed
  if(released_cpu_cnt > 2 || released_cpu_cnt == 0)
    return 0;

  STRATA_LOG("autotunerd", "CPU Usage Mode: Release porcessors: %d %d %d %d\n", release[0], release[1], release[2], release[3]);

  m->cpu_usage_priority_mode = 1;
  //now let actually release the under used process to everybody
  at_release_under_used_cpu(ath, release, cpu_cnt);

  //pin threads according to this new scheduling pattern
  at_pin_all_threads(ath);

  return 0;
}

//Run applications with OS native scheduling. This is saved for the cases that we can not handle, for example, there are too many threads to be accomodated by thread table
//One application has to explicitly ask for OS native scheduling mode; this application also has to explicitly ask for turning this mode off
int at_run_os_sched_mode(at_handles * ath)
{
  int release[4] = {0};

  release[0] = release[1] = release[2] = release[3] = 1;
  
  //release all 4 processors to every body
  at_release_under_used_cpu(ath, release, cpu_cnt);

  //pin threads according to this new scheduling pattern                                                                                                                   
  at_pin_all_threads(ath);

  return 0;
}
