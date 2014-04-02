/********************************************************************
 * Every function in this file should be called out side Strata
 *******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_cpu_util.h"

#define SYSTEM_CPU_STAT_FILE "/proc/stat"

//return the cpu time has been used so far for cpuid
int at_get_cpu_usge_sofar(cpu_usage * cu, int cpuid)
{
  return 0;
}

//return the percentage of cpu usage during last "period" nanosecond for cpuid
int at_get_last_cpu_usage(double * perc, int period, int cpuid)
{
  return 0;
}

int at_process_cpu_line( char * line, cpu_usage * cpu)
{
  printf("Got CPU line: %s\n", line);

  int i1, i2, i3, i4, i5, i6, i7, i8, i9;

  int iRet = sscanf(line, "cpu%d %d %d %d %d %d %d %d %d %d", &cpu->cpuid, &cpu->usermode, &cpu->nice, &cpu->system, &cpu->idle, &cpu->iowait, &cpu->irq, &cpu->softirq, &cpu->steal, &cpu->guest);

  //printf("Got CPU Line %d: cpu%d %d %d %d %d %d %d %d %d %d\n", iRet, cpu->cpuid, cpu->usermode, cpu->nice, cpu->system, cpu->idle, cpu->iowait, cpu->irq, cpu->softirq, cpu->steal, cpu->guest);

  cpu->total = cpu->usermode + cpu->nice + cpu->system + cpu->idle + cpu->iowait + cpu->irq + cpu->softirq + cpu->steal + cpu->guest;
  
  if( iRet != 10 )
    return -1; //this is not a valid cpu usage line

  return 0;
}


//return the percentage of cpu usage during last "period" nanosecond for first n processors
//return value indicates how many processors in the system
int at_get_last_ncpu_usage(double * perc, int period, int n)
{
  //cpu usage for each processor before sleep
  cpu_usage * cpus_1, * cpus_2;
  
  FILE * statfile = fopen(SYSTEM_CPU_STAT_FILE, "r");

  if(statfile == NULL)
    {
      printf("Autotuner: Open file %s failed\n", SYSTEM_CPU_STAT_FILE);
      return -1;
    }

  int i, iRet, nproc;
  //buffer to store one line, 256 characters should be enough
  char buf[256] = {0};
  //get the first line which is the summary of all cpu's utilization
  char * fRet = fgets(buf, 256, statfile);
  
  if( fRet == NULL )
    {
      printf("Autotuner: Read file %s failed\n", SYSTEM_CPU_STAT_FILE);
      fclose(statfile);
      return -1;
    }

  //ignore the first line, we are interested in each processor
  cpus_1 = malloc(sizeof(cpu_usage)*n);
  memset(cpus_1, 0 , sizeof(cpu_usage)*n);
  for(i = 0; i < n; i++)
    {
      memset(buf, 0, 256);
      
      fRet = fgets(buf, 256, statfile);
      if( fRet == NULL )
	{
	  printf("Autotuner: Read file %s failed\n", SYSTEM_CPU_STAT_FILE);
	  free(cpus_1);
	  fclose(statfile);
	  return -1;
	}
      
      iRet = at_process_cpu_line(buf, cpus_1 + i);

      if(iRet == -1) //if this is not a valid cpu usage line then probably we have already read all cpus on this machine
	break;
    }

  //number of processors
  nproc = i;

  fclose(statfile);

  usleep(period);

  //get cpu usage after period
  statfile = fopen(SYSTEM_CPU_STAT_FILE, "r");

  if(statfile == NULL)
    {
      printf("Autotuner: Open file %s failed\n", SYSTEM_CPU_STAT_FILE);
      return -1;
    }

  //get the first line which is the summary of all cpu's utilization
  fRet = fgets(buf, 256, statfile);
  
  if( fRet == NULL )
    {
      printf("Autotuner: Read file %s failed\n", SYSTEM_CPU_STAT_FILE);
      fclose(statfile);
      return -1;
    }

  //ignore the first line, we are interested in each processor
  cpus_2 = malloc(sizeof(cpu_usage)*n);
  memset(cpus_2, 0 , sizeof(cpu_usage)*n);
  for(i = 0; i < nproc; i++)
    {
      memset(buf, 0, 256);
      
      fRet = fgets(buf, 256, statfile);
      if( fRet == NULL )
	{
	  printf("Autotuner: Read file %s failed\n", SYSTEM_CPU_STAT_FILE);
	  free(cpus_1);
	  free(cpus_2);
	  fclose(statfile);
	  return -1;
	}
      
      iRet = at_process_cpu_line(buf, cpus_2 + i);

      if(iRet == -1) //if this is not a valid cpu usage line then probably we have already read all cpus on this machine
	break;
    }

  fclose(statfile);

  //compute cpu unitlization for each processor
  double total_time = 0;
  double idle_time = 0;
  for(i = 0; i < n; i++)
    {
      total_time = cpus_2[i].total - cpus_1[i].total;
      idle_time = cpus_2[i].idle - cpus_1[i].idle + cpus_2[i].iowait - cpus_1[i].iowait;
      
      if(total_time == 0 ) //proc has not changed its output yet, need to sample at a lower frequency
	{
	  free(cpus_1);
	  free(cpus_2);
	  return -1;
	}
      perc[i] = 1 - idle_time / total_time;
    }
  
  free(cpus_1);
  free(cpus_2);
  
  return nproc;
}
