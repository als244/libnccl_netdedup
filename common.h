#ifndef COMMON_H
#define COMMON_H

#include "config.h"
#include "err.h"

// DOING TOP-LEVEL IMPORTS

// OK FOR NOW, BUT UNNECESSARY TO INCLUDE THESE IN ALL FILES....
#define  _GNU_SOURCE


#include <unistd.h>

// Core standard libraries
#include <stdio.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <net/if.h>

// For handling net syscall interceptions with different args
// (not using for now...)
#include <stdarg.h>

// Getting pointer to original functions
#include <dlfcn.h>
#include <sys/types.h>

// for signals
#include <signal.h>

// Looking up if file descriptor is socket
#include <sys/stat.h>

// File permission bits
#include <fcntl.h>
#include <errno.h>

// Timing things
#include <time.h>

// Specifying types
#include <stdint.h>
#include <stdbool.h>

// Memory manipulations
#include <string.h>

// Mmap stuff
#include <sys/mman.h>

// IPC stuff
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
// for fancier Sys V IPC semaphores (can increment/decrement other than 1)
#include <sys/sem.h>

// for libibverbs interceptions
// NOT USING FOR NOW!
// #include <infiniband/verbs.h>


#define FINGERPRINT_NUM_BYTES 32
#define FINGERPRINT_TYPE SHA256_HASH

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)


typedef int (*Item_Cmp)(void * item, void * other_item);
typedef uint64_t (*Hash_Func)(void * item, uint64_t table_size);



/*

// This is used for sys v IPC
// Per man semctl, calling progrma must define this union as follows
// For LINUX!
union semun {
	int val; // Value for SETVAL
	struct semid_ds *buf; // Buffer for IPC_STAT, IPC_SET
	unsigned short *array; // Array for GETALL, SETALL
	struct seminfo *__buf; // Buffer for IPC_INFO
	// (Linux specific)
};

*/

#define MY_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MY_MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define MY_CEIL(a, b) ((a + b - 1) / b)

#define WARN(...) logFunction(NCCL_LOG_WARN, NCCL_ALL, __FILE__, __LINE__, __VA_ARGS__)
#define INFO(FLAGS, ...) logFunction(NCCL_LOG_INFO, (FLAGS), __func__, __LINE__, __VA_ARGS__)

typedef enum {NCCL_LOG_NONE=0, NCCL_LOG_VERSION=1, NCCL_LOG_WARN=2, NCCL_LOG_INFO=3, NCCL_LOG_ABORT=4, NCCL_LOG_TRACE=5} ncclDebugLogLevel;
typedef enum {NCCL_INIT=1, NCCL_COLL=2, NCCL_P2P=4, NCCL_SHM=8, NCCL_NET=16, NCCL_GRAPH=32, NCCL_TUNING=64, NCCL_ENV=128, NCCL_ALLOC=256, NCCL_CALL=512, NCCL_PROXY=1024, NCCL_NVLS=2048, NCCL_BOOTSTRAP=4096, NCCL_REG=8192, NCCL_ALL=~0} ncclDebugLogSubSys;

typedef void (*ncclDebugLogger_t)(ncclDebugLogLevel level, unsigned long flags, const char *file, int line, const char *fmt, ...);

#include "nccl_types.h"

#endif
