#include <stdlib.h>

#include "all.h"
#include "at_opt.h"

//do we want to enable perfmon profiling module, or just use load balancing
int AT_ENABLE_PROFILING = 1;
//do we want to enable the whole perfmon policy
int AT_ENABLED = 1;


int at_get_all_flags()
{
  //AT_ENABLE_PROFILING
  char * enable_profiling = getenv("AT_ENABLE_PROFILING");
  if( enable_profiling != NULL && enable_profiling[1] == 0)
    {
      if(enable_profiling[0] == '1')
	AT_ENABLE_PROFILING = 1;
      else if(enable_profiling[0] == '0')
	AT_ENABLE_PROFILING = 0;
    }
  STRATA_LOG("autotunerd", "Get Env AT_ENABLE_PROFILING=%d\n", AT_ENABLE_PROFILING);

  //AT_ENABLED
  char * enable_at = getenv("AT_NULL");
  if( enable_at != NULL && enable_at[1] == '\0')
    {
      if(enable_at[0] == '1')
	AT_ENABLED = 0;
      else if(enable_at[0] == '0')
	AT_ENABLED = 1;
    }
  STRATA_LOG("autotunerd", "Get Env AT_NULL=%d\n", !AT_ENABLED);

  return 0;
}
