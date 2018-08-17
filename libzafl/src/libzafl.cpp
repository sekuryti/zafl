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

// externally visible so that Zipr transformations can access directly
u8* zafl_trace_map;
unsigned short zafl_prev_id;

static s32 shm_id;
static int __afl_temp_data;
static pid_t __afl_fork_pid;
static auto debug = false;

#define PRINT_ERROR(string) if (debug) {(void)(write(2, string, strlen(string)));}
#define PRINT_DEBUG(string) if (debug) {(void)(write(1, string, strlen(string)));}

static void zafl_setupSharedMemory();
static bool shared_memory_is_setup = false;

#ifdef ZAFL_AUTO_INIT_FORK_SERVER
void __attribute__((constructor)) zafl_initAflForkServer();
#else
void __attribute__((constructor)) zafl_setupSharedMemory();
#endif

static void zafl_setupSharedMemory()
{
	if (getenv("ZAFL_DEBUG")) debug = true;

	if (shared_memory_is_setup)
		return;

	zafl_prev_id = 0;
	zafl_trace_map = NULL;

	char *shm_env_var = getenv(SHM_ENV_VAR);
	if(!shm_env_var) {
		PRINT_ERROR("Error getting shm environment variable - fake allocate AFL trace map\n");

		// fake allocate until someone calls zafl_initAflForkServer()
		zafl_trace_map = (u8*)malloc(MAP_SIZE); 
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
	static auto fork_server_initialized = false;

	if (fork_server_initialized) return;

	if (getenv("ZAFL_DEBUG")) debug = true;

	zafl_setupSharedMemory();

	if (!zafl_trace_map) {
		zafl_trace_map = (u8*)malloc(MAP_SIZE);
		if (debug)
			printf("no shmem detected: fake it: zafl_trace_map = %p, malloc_size(%d)\n", zafl_trace_map, MAP_SIZE);
	}

	int n = write(FORKSRV_FD+1, &__afl_temp_data,4);
	if( n!=4 ) {
		if (debug)
			perror("zafl_initAflForkServer()");
		return;
	}

	fork_server_initialized = true;

	while(1) {
		n = read(FORKSRV_FD,&__afl_temp_data,4);
		if(n != 4) {
		    perror("Error reading fork server\n");
		    return;
		}

		__afl_fork_pid = fork();
		if(__afl_fork_pid < 0) {
		    perror("Error on fork()\n");
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

// for efficiency, basic block instrumentation is inlined via a Zipr transformation
// this code is used for debugging purposes only
void zafl_bbInstrument(unsigned short id) {
	zafl_trace_map[zafl_prev_id ^ id]++;
	zafl_prev_id = id >> 1;
}
