#ifndef _STRATA_AUTOTUNER_
#define _STRATA_AUTOTUNER_

#include <pthread.h>

//name of the mutex used to protect shared memory
#define AT_SHMEM_SEM "/STRATA_AUTOTUNER_SHEME"
//prefix of the name of  communication mutexes
#define AT_COMM_SEM_PFX "/STRATA_AUTOTUNER_COMM"
//semaphore to protect each message queue
#define AT_MSG_Q_SEM_PFX "/STRATA_AUTOTUNER_MSG_Q"
//name of shared memory file
#define AT_SHMEM_NAME "/tmp/STRATA_AUTOTUNER_SHMEM"

//msg: no message
#define AT_NO_MSG                0
//msg: start testing
#define AT_MSG_TYPE_TEST         1
//msg: testing result ready
#define AT_MSG_TYPE_DATA_READY   2
//msg: slave task is quitting
#define AT_MSG_TYPE_SLAVE_QUIT   3
//msg: master task is quitting
#define AT_MSG_TYPE_MASTER_QUIT  4
//msg: set the threads with cpu plan
#define AT_MSG_TYPE_RUN_AS_PLAN  5
//msg: quit
#define AT_MSG_TYPE_QUIT         6 
//msg: task's thread count changed
#define AT_MSG_TYPE_THREAD_COUNT_CHANGED   7
//msg: reset the timer to a specified time, and enable timer triggered action
#define AT_MSG_TYPE_RESET_TIMER  8


//length between application start and first sampling
#define AT_1ST_SAMPLING_INTERVAL 5 // 5 sec
//length between two sampling period in seconds
#define AT_SAMPLING_INTERVAL     60 //1 min
//length of each sampling in seconds
#define AT_SAMPLING_LENGTH       1  //1 sec

typedef struct _at_message{
  int value; //valuin of the message, since we are sending messages across processes' boundaries, we can not use pointers
  int type; //message type
  int sender_id; //sender id
}at_message;

#define THREAD_TABLE_LENGTH 48
typedef struct _thread_table_item{
  int valid;
  int prev;
  int next;
  int tid;
  void * pthread_handle;
  int active;
}thread_table_item;

#define AT_MSG_Q_LEN 32
typedef struct _task_list_item{
  int valid; //indicate whether this cell is valid
  int prev; //index of previous task
  int next; //index of next task
  int pid; //process id of current tas
  int sem_suffix; //suffix of its communicate sem
  double avg_ipc; //avg ipc of one permutation
  long long int insn_retired; //total instructions retired in a given period
  int data_ready; //avg ipc value is read
  int msg_cnt; //how many messages
  at_message messages[AT_MSG_Q_LEN]; //messages
  int cpus[THREAD_TABLE_LENGTH]; //list of cpus allocated to this process/task, echo corresponding to a thread

  //members for thread table
  thread_table_item threads_table[THREAD_TABLE_LENGTH];
  int thread_table_head;
  int thread_table_tail;
  int thread_count;
  int active_thread_count;
  int tbl_index; //available thread table cell index, for a proper implementation, it should be two static linked lists over threads_table
  pthread_mutex_t thread_table_lock;
  
  int os_sched_mode_req; //whether this thread has requested OS scheduling mode
}task_list_item;


//data struct that discribing the contents of the shared memory
typedef struct _shared_mem{
  int master_id; //index of the master
  int task_cnt; //how many tasks we have here
  int sem_suffix; //each job will get a new semaphore for itself for communication, this is the surffix of mutex name
  int head; //index of the head of task list
  int tail; //index of the end of task list
  int cur_index; //index of the task list cell that is current available, I don't do a dynamic allocation here, it may be done later
  int slave_not_allowed_to_quit; //if slaves are allowed to quit
  int cpu_usage_priority_mode; //we are now in cpu usage priority mode
  int os_sched_mode; //Using OS native scheduling
  task_list_item tl[256]; //static linked list of tasks/processes
}shared_mem;

//data structure contains persistant variables used for share memory acess
typedef struct _at_handles{
  void * shmem_sem; //mutex for shared memory access synchornization
  void * comm_sem; //semphore for communication
  int shmem_id; //shared memory id
  shared_mem * shmem; //pointer to mapped shared memory
  int pid; //process id of current task
  int index; //task list index of current task
  int is_master; //is this task the master task
  char comm_sem_name[32];
  void * msg_q_sem; //semphoare for protect message queue
  char msg_q_sem_name[32];
  pthread_t at_thread; //actually pthread_t handle of autotuner thread
}at_handles;

//new task setup: may including shared memory initialization, including registeration
int task_setup(at_handles * ath);
//register new task in shared memory: including find new task cell, find new mutex 
int task_register(at_handles* ath);
//autotuner thread function
void * autotuner_func(void *);
//unregister task, may include destruction of shared memory
int task_unregister(at_handles* ath, int need_lock);
//send message
int at_send_message( shared_mem *m, at_message * msg, int receiver);
//receive message
int at_receive_message(at_handles * ath, at_message * msg);

#endif
