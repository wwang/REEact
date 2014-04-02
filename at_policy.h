#ifndef __STRATA_AUTOTUNER_POLICY__
#define __STRATA_AUTOTUNER_POLICY__

//remove current application thread from thread table
int autotuner_rm_this_thread_from_table();
//add current application thread to thread table
int autotuner_add_this_thread_to_table();
//Master message function:MSG_SLAVE_QUIT
int at_msg_slave_quit_mst(at_handles * ath, at_message * msg);
//Master message function:MSG_QUIT
int at_msg_quit_mst(at_handles * ath, at_message * msg);
//Master function: message wait time out
int at_msg_timeout_mst(at_handles * ath, int * quit);
//Slave message function: MSG_TEST
int at_msg_test_slv(at_handles * ath, at_message * msg);
//Slave message function: MESG_RUN_AS_PLAN
int at_msg_run_plan_slv(at_handles * ath, at_message * msg);
//Slave message function: MSG_MASTER_QUIT
int at_msg_mst_quit_slv(at_handles * ath, at_message * msg);
//Slave message funtion: MSG_QUIT
int at_msg_quit_slv(at_handles * ath, at_message * msg);
//master message function for AT_MSG_TYPE_THREAD_COUNT_CHANGED
int at_msg_th_cnt_chg_mst(at_handles * ath, at_message * msg);
//pthread_join wil be transfered here
int at_pthread_join(pthread_t thread, void **value_ptr);
//improve system cpu usage if needed
int at_improve_cpu_usage(at_handles * ath);
#endif
