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
#include "at_policy.h"
#include "autotuner_perfm.h"
#include "at_wrap.h"
#include "at_opt.h"

//autotuner handle, one per application
at_handles athandle;

//helper functions
int insert_to_tasklist(shared_mem * shmem, int index);
int remove_from_tasklist(shared_mem * shmem, int index);
//find the first task that is not "index"
int find_first_task_not_index( shared_mem *m, int index);

//do we want to enable perfmon profiling module, or just use load balancing
extern int AT_ENABLE_PROFILING;
//do we want to enable the whole perfmon policy
extern int AT_ENABLED;

//new task setup: may including shared memory initialization, including registeration
int task_setup(at_handles * ath)
{
  STRATA_LOG("autotunerd", "Begin task setup\n");
  //first acquire the mutex to protect share memory
  ath->shmem_sem = (void *)sem_open(AT_SHMEM_SEM, O_CREAT, S_IRUSR | S_IWUSR, 1);
  
  if(ath->shmem_sem == SEM_FAILED)
    {
      STRATA_LOG("autotuner", "Failed to open shared memory semaphore\n");
      return -1;
    }

  STRATA_LOG("autotunerd", "Open shared memory semaphore successfully\n");
  //regsiter the task 
  task_register(ath);

  return 0;
}

//register new task in shared memory: including find new task cell, find new mutex 
int task_register(at_handles* ath)
{
  STRATA_LOG("autotunerd", "Begin task registration\n");
  
  //wait for shared memory semaphore
  sem_wait((sem_t*)ath->shmem_sem);
  
  STRATA_LOG("autotunerd", "Got shared memory mutext\n");

  //open shared memory
  int is_master = 0; //if this task will become the master
  key_t key;
  key = ftok(AT_SHMEM_NAME, 'R');
  ath->shmem_id = shmget(key, sizeof(shared_mem), 0644);
  if( ath->shmem_id == -1)
    {
      //this is the first creation of that shared memory
      STRATA_LOG("autotuner", "Shared memory doesn't exist, creating new\n");
      ath->shmem_id = shmget(key, sizeof(shared_mem), 0644 | IPC_CREAT | IPC_EXCL);
      is_master = 1; //we created it, so we are the master
    }

  //map shared memory into this process
  ath->shmem = (shared_mem*)shmat(ath->shmem_id, NULL, 0);

  if( ath->shmem == (shared_mem*)-1 )
    {
      STRATA_LOG("autotuner", "Failed to map shared memory\n");
      return -1;
    }

  STRATA_LOG("autotuner", "Sucessfully mapped shared memory\n");
  
  //if we created this shared memory for the first time, initializa it to zero
  if(ath->shmem->task_cnt == 0)
    {
      //this is the first task in this shared memory, so this is the master
      ath->is_master = is_master = 1;
    }
  else
    {
      //there is already one or more tasks in the queue
      STRATA_LOG("autotuner", "Find master at %d\n", ath->shmem->master_id);
    }

  if(is_master)
    {
      memset( ath->shmem, sizeof(shared_mem), 0);
      ath->shmem->head = ath->shmem->tail = -1;
    }

  //start looking for available cell
  int index;
  shared_mem * m;
  m = ath->shmem;
  //find cell index
  index = ath->index = m->cur_index;
  m->cur_index++;
  memset( (m->tl + index), sizeof(task_list_item), 0);
  m->tl[index].valid = 1;
  m->tl[index].prev = m->tl[index].next = -1;
  STRATA_LOG("autotunerd", "New task index: %d\n", index);

  //get communication semaphore suffix
  m->tl[index].sem_suffix = m->sem_suffix;
  m->sem_suffix++;
  sprintf(ath->comm_sem_name, "%s%d", AT_COMM_SEM_PFX, m->tl[index].sem_suffix);
  STRATA_LOG("autotunerd", "New Communication semaphore %s\n", ath->comm_sem_name); 
  
  ath->comm_sem = sem_open(ath->comm_sem_name, O_CREAT|O_EXCL, S_IRUSR | S_IWUSR, 0);
  if(ath->comm_sem == SEM_FAILED)
    {
      STRATA_LOG("autotuner", "Failed to create communication semaphore\n");
      return 1;
    }
  
  //get message queue semaphore
  sprintf(ath->msg_q_sem_name, "%s%d", AT_MSG_Q_SEM_PFX, m->tl[index].sem_suffix);
  STRATA_LOG("autotunerd", "New message queue semaphore %s\n", ath->msg_q_sem_name); 
  
  ath->msg_q_sem = sem_open(ath->msg_q_sem_name, O_CREAT|O_EXCL, S_IRUSR | S_IWUSR, 1);
  if(ath->msg_q_sem == SEM_FAILED)
    {
      STRATA_LOG("autotuner", "Failed to create message queue semaphore\n");
      return 1;
    }
  
  //get pid and store it
  ath->pid = m->tl[index].pid = getpid();

  //set OS scheduling request flag to 0
  m->tl[index].os_sched_mode_req = 0;

  //put master info into shared memory
  ath->is_master = is_master;
  if( is_master )
    {
      STRATA_LOG("autotunerd", "A new master %d\n", index);
      m->master_id = index;
    }
  
  //increase task count
  m->task_cnt++;

  //insert this new task into static linked task list
  insert_to_tasklist(m, index);

  //initialize thread table and corresponding members
  //members for thread table
  m->tl[index].thread_table_head = -1;
  m->tl[index].thread_table_tail = -1;
  m->tl[index].thread_count = 0;
  m->tl[index].active_thread_count = 0;
  m->tl[index].tbl_index = 0; //available thread table cell index, for a proper implementation, it should be two static linked lists over threads_table
  pthread_mutex_init(&(m->tl[index].thread_table_lock), NULL);

  //signal the shared memory semaphore
  sem_post((sem_t*)ath->shmem_sem);

  STRATA_LOG("autotunerd", "Task registration finished\n");

  return 0;
}

int task_unregister(at_handles * ath, int need_lock)
{
  STRATA_LOG("autotunerd", "Start unregistering Thread\n");

  int rm_shmem = 0;
  int task_cnt = ath->shmem->task_cnt;
  //acquire shared memory lock if not acquired yet
  if(need_lock)
    sem_wait((sem_t*)ath->shmem_sem);
  
  if(ath->is_master)
    {
      //this is the master, so we have to point a new master
      int new_master = find_first_task_not_index(ath->shmem, ath->index);
      STRATA_LOG("autotuner", "Master quitting, found new master %d\n", new_master);
      if( new_master != -1 )
	{
	  //found a new master, tell it to be the new master
	  at_message msg;
	  msg.sender_id = ath->index;
	  msg.value = 0;
	  msg.type = AT_MSG_TYPE_MASTER_QUIT;
	  at_send_message(ath->shmem, &msg, new_master);
	}
      else
	//this is the last task, after we quit we have to remove shared memory
	rm_shmem = 1;
    }
  else
    {
      //this is a slave, so we have to tell master we are existing
      STRATA_LOG("autotunerd", "Slave %d is going to quit\n", ath->index);
      //ath->shmem->tl[ath->shmem->master_id].msg[ath->shmem->tl[ath->shmem->master_id].msg_cnt++] = ath->index;
      at_message msg;
      msg.sender_id = ath->index;
      msg.value = 0;
      msg.type = AT_MSG_TYPE_SLAVE_QUIT;
      at_send_message(ath->shmem, &msg, ath->shmem->master_id);
    }

  
  STRATA_LOG("autotunerd", "Removing the task from task list\n");
  //unset OS scheduling flag, if necessary
  if(ath->shmem->tl[ath->index].os_sched_mode_req)
    ath->shmem->os_sched_mode = 0;

  //remove this task from the list
  remove_from_tasklist(ath->shmem, ath->index);
  ath->shmem->tl[ath->index].valid = 0;
  task_cnt = --(ath->shmem->task_cnt);
  //detach shared memory
  shmdt(ath->shmem);

  if(rm_shmem || task_cnt == 0) //since slaves are allowed to quit then some time they may 
    {
      //we are going to remove this share memory
      STRATA_LOG("autotunerd", "Removing shared memory\n");
      shmctl(ath->shmem_id, IPC_RMID, NULL);
    }

  //close and unlink communication semaphore
  STRATA_LOG("autotunerd", "Closing and unlinking communication semaphore\n");
  sem_close((sem_t*)ath->comm_sem);
  sem_unlink(ath->comm_sem_name);
  //close and unlink message queue semaphore
  STRATA_LOG("autotunerd", "Closing and unlinking message queue semaphore\n");
  sem_close((sem_t*)ath->msg_q_sem);
  sem_unlink(ath->msg_q_sem_name);
  
  //release shared memory lock and close it
  STRATA_LOG("autotunerd", "Closing shared memory semaphore\n");
  sem_post((sem_t*)ath->shmem_sem);
  sem_close((sem_t*)ath->shmem_sem);
  
  if(rm_shmem)
    {
      //if shared memory is destroyed than we can remove its protection semaphore
      STRATA_LOG("autotunerd", "Unlinking shared memory semaphore\n");
      sem_unlink(AT_SHMEM_SEM);
    }

  return 0;
}

//TODO: should this part be converted to general static list implementation?
int at_debug_print_list(shared_mem * m)
{
  STRATA_LOG("autotunerd", "Current task list:\n");
  
  int i = m->head;
  int j = 0;
  while(i != -1)
    {
      STRATA_LOG("autotunerd", "Task %d: At %d\n", j, i);
      i = m->tl[i].next;
      j++;
    }

  STRATA_LOG("autotunerd", "Finished printing task list\n");
  
  return 0;
}

int insert_to_tasklist(shared_mem * m, int index)
{
  if( m->head == m->tail && m->head == -1 )
    {
      //this is the first item in the lisk
      m->head = m->tail = index;
      m->tl[index].prev = m->tl[index].next = -1;
    }
  else
    {
      //there are some items already
      m->tl[m->tail].next = index;
      m->tl[index].prev = m->tail;
      m->tl[index].next = -1;
      m->tail = index;
    }

  at_debug_print_list(m);
  
  return 0;
}

int remove_from_tasklist(shared_mem * m, int index)
{
  m->tl[index].valid = 0;

  if( index == m->head && index == m->tail)
    {
      //removing the last itme
      m->head = m->tail = -1;
    }
  else if( index == m->head )
    {
      //removing the head
      int next = m->tl[index].next;
      m->head = next;
      m->tl[next].prev = -1;
    }
  else if( index == m->tail )
    {
      //removing the tail
      int prev = m->tl[index].prev;
      m->tail = prev;
      m->tl[prev].next = -1;
    }
  else 
    {
      //removing a cell in the list
      int prev = m->tl[index].prev;
      int next = m->tl[index].next;
      m->tl[prev].next = next;
      m->tl[next].prev = prev;
    }
  
  at_debug_print_list(m);

  return 0;
}

//find the first task that dose not have the input "index"
int find_first_task_not_index( shared_mem *m, int index)
{
  int i = m->head;
  while( i != -1 )
    {
      if( i != index )
	return i;
      
      i = m->tl[i].next;
    }

  return -1;
}

//shmem semaphore should be acquired before this call
int at_send_message( shared_mem *m, at_message * msg, int receiver)
{
  STRATA_LOG("autotunerd", "Send message to %d, with type %d, value %d\n", receiver, msg->type, msg->value);
  char recv_sem_name[32] = {0};
  char recv_msg_q_sem_name[32] = {0};
  int recv_sem_suffix = m->tl[receiver].sem_suffix;

  //get receiver's semaphore
  sprintf(recv_sem_name, "%s%d", AT_COMM_SEM_PFX, recv_sem_suffix);
  STRATA_LOG("autotunerd", "Send Message: Opening Comm Sem %s\n", recv_sem_name);
  sem_t * s = sem_open(recv_sem_name, 0, 0644, 0);
  if( s == SEM_FAILED )
    {
      //open sem failed, may be the receiver quit
      STRATA_LOG("autotuner", "Send Message failed to receiver %d due to unable to open comm sem\n", receiver);
      return -1;
    }

  //get receiver's queue semaphore
  sprintf(recv_msg_q_sem_name, "%s%d", AT_MSG_Q_SEM_PFX, recv_sem_suffix);
  STRATA_LOG("autotunerd", "Send Message: Opening Comm Sem %s\n", recv_msg_q_sem_name);
  sem_t * q = sem_open(recv_msg_q_sem_name, 0, 0644, 1);
  if( q == SEM_FAILED )
    {
      //open sem failed, may be the receiver quit
      STRATA_LOG("autotuner", "Send Message failed to receiver %d due to unable to open msg q sem\n", receiver);
      return -1;
    }

  STRATA_LOG("autotunerd", "Send Message: Before writing message\n");
  int posted = 0;
  sem_wait(q);
  if(m->tl[receiver].msg_cnt < (AT_MSG_Q_LEN - 1))
  {
	  m->tl[receiver].messages[m->tl[receiver].msg_cnt++] = *msg;
	  posted = 1;
  	  STRATA_LOG("autotunerd", "Send Message: After writing message to msg_slot %d\n", m->tl[receiver].msg_cnt-1 );
  }
  sem_post(q);
  
  //signal the semphore, tell receiver there is new message
  STRATA_LOG("autotunerd", "Send Message: Sending message, Posted %d\n", posted);
  if(posted)
    sem_post(s);

  //close these semaphores.
  sem_close(s);
  sem_close(q);

  return !posted;
}

int at_receive_message(at_handles * ath, at_message * msg)
{
  shared_mem * m = ath->shmem;

  sem_wait(ath->msg_q_sem);
  *msg = m->tl[ath->index].messages[--(m->tl[ath->index].msg_cnt)];
  
  sem_post(ath->msg_q_sem);
  
  STRATA_LOG("autotunerd", "Message Received for task %d with type %d value %d at slot %d\n", ath->index, msg->type, msg->value, m->tl[ath->index].msg_cnt);

  return msg->type;
}

//main processing function for master thread
int at_master_function(at_handles * ath)
{
  struct timespec ts;
  int iRet;
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;
  int quit = 0;
  int enable_timer_action = 0;
  int timer_length = AT_SAMPLING_INTERVAL; //how long will the timer wait, default 1000 seconds

  STRATA_LOG("autotunerd", "Task %d, %d: Master Message Loop begin\n", ath->index, ath->pid);
  while(!quit)
    {
      //wait for next sampling or message
      clock_gettime(CLOCK_REALTIME, &ts);
      //      if(first_sampling)
      //	ts.tv_sec += AT_1ST_SAMPLING_INTERVAL;
      //else
      //	ts.tv_sec += AT_SAMPLING_INTERVAL;
      ts.tv_sec += timer_length;
      iRet = sem_timedwait(ath->comm_sem, &ts);

      if( !iRet )
	{
	  //we have gotten a new message
	  at_message msg;
	  //sem_wait((sem_t*)ath->shmem_sem);
	  at_receive_message(ath, &msg);
	  //we have got a new message
	  if( msg.type == AT_MSG_TYPE_SLAVE_QUIT )
	    {
	      at_msg_slave_quit_mst(ath, &msg);
	    }
	  else if( msg.type == AT_MSG_TYPE_QUIT )
	    {
	      //user policy specified quitting procedure
	      at_msg_quit_mst(ath, &msg);
	      
	      //standard quitting procedure
	      //m->slave_not_allowed_to_quit = 1;
	      STRATA_LOG("autotuner", "%d: Got quit message for process\n", ath->index, ath->pid);
	      task_unregister(ath, 1);
	      quit = 1;
	      break;
	      //m->slave_not_allowed_to_quit = 0;
	    }
	  else if( msg.type == AT_MSG_TYPE_THREAD_COUNT_CHANGED )
	    {
	      at_msg_th_cnt_chg_mst(ath, &msg);
	    }
	  else if(msg.type == AT_MSG_TYPE_RESET_TIMER)
	    {
	      timer_length = msg.value;
	      enable_timer_action = 1;
	    }
	  else
	    //we do not have other messages now
	    STRATA_LOG("autotuner", "Got unknown message type %d\n", msg.type);
	  //sem_post((sem_t*)ath->shmem_sem);
	  
	}
      else if( errno == ETIMEDOUT )
	{
	  //try to improve cpu usage, later we neen to compare cpu usage priority mode and isolation
	  if(enable_timer_action)
	    at_improve_cpu_usage(ath);

	  if(AT_ENABLE_PROFILING && enable_timer_action)
	    {
	      at_msg_timeout_mst(ath, &quit);
	    }
	  timer_length = AT_SAMPLING_INTERVAL;
	  enable_timer_action = 0;  
	}
      else
	STRATA_LOG("autotuner", "Comm sem wait failed for unknown reason %d\n", errno);
      
    }
  
  return 0;
}

//main function for slave task
int at_slave_function(at_handles * ath)
{
  struct timespec ts;
  int iRet;
  shared_mem * m = ath->shmem;
  task_list_item * t = (m->tl) + ath->index;
  int quit = 0;
  int need_to_quit = 0;
  int raise_to_master = 0;
  int wait_interval = AT_SAMPLING_INTERVAL;

  while(!quit)
    {
      if(need_to_quit)
	{
	  //if we received the quit message, then we should quit. However, master may be working on sampling and we have to reponse to it
	  STRATA_LOG("autotunerd", "Slave trying to acquire shared memory lock to quit\n");
	  iRet = sem_trywait(ath->shmem_sem);
	  if(iRet == 0 )
	    {
	      //if we can acquire the lock, then we can quit, else, wait for new message
	      STRATA_LOG("autotunerd", "Slave got shared meory lock, ready to quit\n");
	      task_unregister(ath, 0);
	      break;
	    }
	  else
	    {
	      //we have to wait until we are allowed to quit, but this time wait shorter
	      STRATA_LOG("autotunerd", "Slave failed to get shared memory lock, wait for another 1 sec\n");
	      wait_interval = 1;
	    }
	}
      
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += wait_interval;
      STRATA_LOG("autotunerd", "Slave waiting for message or time out %d\n", wait_interval);
      iRet = sem_timedwait(ath->comm_sem, &ts);
      
      if(iRet != 0)
	{
	  //no new message, just timer went out
	  STRATA_LOG("autotunerd", "Slave waiting time out\n");
	  continue;
	}

      at_message msg;
      at_receive_message(ath, &msg);

      if( msg.type == AT_MSG_TYPE_TEST )
	{
	  at_msg_test_slv(ath, &msg);
	}
      else if( msg.type == AT_MSG_TYPE_QUIT )
	{
	  STRATA_LOG("autotunerd", "Slave %d got msg TYPE_QUIT\n", ath->index);
	  //user policy specified quitting procedure
	  at_msg_quit_slv(ath, &msg);
	  //standard quitting procedure
	  need_to_quit = 1;
	}
      else if( msg.type == AT_MSG_TYPE_MASTER_QUIT )
	{
	  STRATA_LOG("autotunerd", "Slave %d got msg TYPE_MASTER_QUIT\n", ath->index);
	  //user policy specified master-quitting procedure
	  at_msg_mst_quit_slv(ath, &msg);
	  //standard quitting procedure
	  quit = 1;
	  raise_to_master = 1;
	  m->master_id = ath->index;
	  ath->is_master = 1;
	}
      else if( msg.type == AT_MSG_TYPE_RUN_AS_PLAN )
	{
	  at_msg_run_plan_slv(ath, &msg);
	}
      else
	//got unrecognized message
	STRATA_LOG("autotuner", "Slave got unknown message type %d", msg.type);
    }

  if(raise_to_master && !need_to_quit)
    {
      //this task becomes master
      STRATA_LOG("autotunerd", "Slave %d becomes master\n", ath->index);
      //load balance the system
      at_msg_th_cnt_chg_mst(ath, NULL);
      //do master's job
      at_master_function(ath);
    }
  else if(raise_to_master && need_to_quit)
  {
    //although this slave is assigned to be master, it is also going to quit	  
    STRATA_LOG("autotunerd", "Newly appointed master %d(%d) is going to quit to quit\n", ath->index, ath->pid);
    task_unregister(ath, 1);
  }


  return 0;
}

void * autotuner_thread( void * p )
{
  at_handles * ath = (at_handles *) p;

  if(ath->is_master)
    //master, start master working function
    at_master_function(ath);
  else
    //slave, start slave working function
    at_slave_function(ath);

  //after return from working functions, this task should have been unregisterred already
  return NULL;
}

//initialization of autotuner
int autotuner_init()
{
  STRATA_LOG("autotunerd", "Autotuner Initialization\n");

  //get environment variables
  at_get_all_flags();
  
  if(!AT_ENABLED)
    return 0;

  //setup this semaphores, shared memory and register this thread
  task_setup(&athandle);

  //create autotuner thread
  at_create_thread(&(athandle.at_thread), NULL, autotuner_thread, &athandle);

  //perfmon initialization
  init_reeact_perfm_app();

  return 0;
}

int autotuner_cleanup()
{
  STRATA_LOG("autotunerd", "Autotuner Cleanup\n");

  if(!AT_ENABLED)
    return 0;
  
  //tell autotuner thread to quit
  at_message msg;
  msg.sender_id = athandle.index; 
  msg.type = AT_MSG_TYPE_QUIT;
  msg.value = 0;
  
  at_send_message(athandle.shmem, &msg, athandle.index);
  
  //wait for autotuner thread to quit
  void * p;
  at_join_thread(athandle.at_thread, &p);
  

  return 0;
}

void strata_extra_cleanup()
{
	autotuner_cleanup();
}

