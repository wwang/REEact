#ifndef __AUTOTUNER_CPU_UTILITY__
#define __AUTOTUNER_CPU_UTILITY__

//structure to store cpu usage at a given time
//see "man proc" for detailed information of each member
typedef struct _cpu_usage{
  int cpuid;
  int usermode;
  int nice;
  int system;
  int idle;
  int iowait;
  int irq;
  int softirq;
  int steal;
  int guest;
  int total;
}cpu_usage;

//return the cpu time has been used so far for cpuid
int at_get_cpu_usge_sofar(cpu_usage * cu, int cpuid);
//return the percentage of cpu usage during last "period" nanosecond for cpuid
int at_get_last_cpu_usage(double * perc, int period, int cpuid);
//return the percentage of cpu usage during last "period" nanosecond for first n processors
int at_get_last_ncpu_usage(double * perc, int period, int n);

#endif
