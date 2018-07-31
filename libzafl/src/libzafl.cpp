/*
 * Copyright (c) 2018 - Zephyr Software LLC
 *
 * This file may be used and modified for non-commercial purposes as long as
 * all copyright, permission, and nonwarranty notices are preserved.
 * Redistribution is prohibited without prior written consent from Zephyr
 * Software.
 *
 * Please contact the authors for restrictions applying to commercial use.
 *
 * THIS SOURCE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Zephyr Software
 * e-mail: jwd@zephyr-software.com
 * URL   : http://www.zephyr-software.com/
 *
 */


#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>

#include "libzafl.hpp"

// these are externally visible so that Zipr transformations can access directly
u8* zafl_trace_map;
unsigned short zafl_prev_id;

static s32 shm_id;                    /* ID of the SHM region             */
static int __afl_temp_data;
static pid_t __afl_fork_pid;

#define PRINT_ERROR(string) (void)(write(2, string, strlen(string))+1) 
#define PRINT_DEBUG(string) (void)(write(1, string, strlen(string))+1) 

static void zafl_setupSharedMemory();
static bool shared_memory_is_setup = false;

void __attribute__((constructor)) zafl_initAflForkServer();

static void zafl_setupSharedMemory()
{
	zafl_prev_id = 0;
	zafl_trace_map = NULL;

	char *shm_env_var = getenv(SHM_ENV_VAR);
	if(!shm_env_var) {
		PRINT_ERROR("Error getting shm\n");
		return;
	}
	shm_id = atoi(shm_env_var);
	zafl_trace_map = (u8*)shmat(shm_id, NULL, 0);
	if(zafl_trace_map == (u8*)-1) {
		PRINT_ERROR("shmat");
		return;
	}
	PRINT_DEBUG("libzafl: shared memory segment is setup\n");
	shared_memory_is_setup = true;
}

void zafl_initAflForkServer()
{
	if (!shared_memory_is_setup)
		zafl_setupSharedMemory();

	if (!zafl_trace_map) {
		zafl_trace_map = (u8*)malloc(MAP_SIZE);
		printf("no shmem detected: fake it: zafl_trace_map = %p, malloc_size(%d)\n", zafl_trace_map, MAP_SIZE);
	}

	int n = write(FORKSRV_FD+1, &__afl_temp_data,4);
	if( n!=4 ) {
		PRINT_ERROR("Error writting fork server\n");
		perror("zafl_initAflForkServer()");
		printf("zafl_trace_map = %p,   FORKSVR_FD(%d)\n", zafl_trace_map, FORKSRV_FD);
		return;
	}

	while(1) {
		n = read(FORKSRV_FD,&__afl_temp_data,4);
		if(n != 4) {
		    PRINT_ERROR("Error reading fork server\n");
		    return;
		}

		__afl_fork_pid = fork();
		if(__afl_fork_pid < 0) {
		    PRINT_ERROR("Error on fork()\n");
		    return;
		}
		if(__afl_fork_pid == 0) {
		    // child
		    close(FORKSRV_FD);
		    close(FORKSRV_FD+1);
		    break;
		} else {
		    // parent
		    n = write(FORKSRV_FD+1,&__afl_fork_pid, 4);
		    pid_t temp_pid = waitpid(__afl_fork_pid,&__afl_temp_data,2);
		    if(temp_pid == 0) {
			return;
		    }
		    n = write(FORKSRV_FD+1,&__afl_temp_data,4);
		}
	}
}

// for debugging purposes only
// basic block instrumentations will be inlined via a Zipr transformation
void zafl_bbInstrument(unsigned short id) {
	zafl_trace_map[zafl_prev_id ^ id]++;
	zafl_prev_id = id >> 1;
}
