#ifndef CONFIG_H
#define CONFIG_H



#define MAX_NET_DEDUP_DEVS 16

#define MAX_COMMS_NET_DEDUP_SOCKET_DEV 65536
#define SOCKET_LISTEN_BACKLOG 1024

#define MAX_FINGERPRINTS_PER_REQ (1ULL << 12)


// The following info gets loggin in NCCL_DEBUG_FILE
#define TO_LOG_NCCL_API_INIT 1
#define TO_LOG_NCCL_API_CONN_ESTABLISH 0
#define TO_LOG_NCCL_API_RDMA_MR 0
#define TO_LOG_NCCL_API_SENDS 0
#define TO_LOG_NCCL_API_RECVS 0
#define TO_LOG_NCCL_API_ALL_TESTS 0
#define TO_LOG_NCCL_API_TEST_COMPLETED 0
#define TO_LOG_NCCL_API_FLUSH 0
#define TO_LOG_NCCL_API_RECV_CONSUMED 0
#define TO_LOG_NCCL_API_CLOSE_CONN 0

// High-level important info
#define TO_LOG_CAPTURE_STATS 0
#define TO_LOG_GENERAL_HEADERS 0
#define TO_LOG_FINGERPRINT_HEADERS 0 
#define TO_LOG_FINGERPRINT_COMPUTATION 0



	
// logs all entries and exit to internal functions 
#define TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE 0
#define TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE 0

// this gets printed to stdout (for now)
#define TO_PRINT_FINGERPRINT_INFO 0
#define TO_PRINT_FINGERPRINT_CACHE 0


// these are for managing reading the sender-side receiver buffers
// in order to read data before the response from receiver-side regarding
// missing fingerprints

// The sender-side will store data in these skipped buffers which will then be
// taken from upon subsequent application reads instead of going into the kernel
// (the data the application is looking for will be in these buffers and not kernel)

// This is necsesary in order to perseve the in-order data that the application expects
#define MAX_FDS 1024

#define FINGERPRINT_CACHE_PATH "/netcache"

#define FINGERPRINT_CACHE_GB 30
#define FINGERPRINT_CACHE_NUM_BYTES (FINGERPRINT_CACHE_GB * (1ULL << 30))

#define FINGERPRINT_CACHE_PAGE_SIZE (1ULL << 12)
#define FINGERPRINT_CACHE_NUM_PAGES (FINGERPRINT_CACHE_NUM_BYTES / FINGERPRINT_CACHE_PAGE_SIZE)

#define FINGERPRINT_TABLE_SIZE (1ULL << 30)
#define FINGERPRINT_KEY_SIZE_BYTES FINGERPRINT_NUM_BYTES



// from "Protocol-Independent Techinque for Eliminating Redundant Network Traffic (Spring & Wetherall, SIGCOMM '00)"
#define RABIN_P 1048583
#define RABIN_M_BITS 60

// Spring & Wetherall used window_bytes = 64 and lower_bits = 5 back in 2000
// From "A Low-Bandwiddth Network File System (Muthitaccharoen et al., 2001"
// 	- used window_bytes = 48
//	- lower_bits = 13

#define WINDOW_BYTES 64
#define LOWER_BITS 13
#define MAGIC_VAL 0


#define MIN_CHUNK_SIZE_BYTES (1UL << 11)
#define MAX_CHUNK_SIZE_BYTES (1UL << 16)


// The way that fingerprinting works it is possible to exceed the MAX CHUNK SIZE slightly
// So to be safe when saving to cache ensure that we will have enough room

// The max chunk size will not be larger than MAX_CHUNK_SIZE_BYTE + MIN_CHUNK_SIZE_BYTES
#define SAFETY_NUM_PAGES_OVERFLOW_MAX_CHUNK_SIZE 2
#define SAFE_MAX_CHUNK_SIZE_BYTES MAX_CHUNK_SIZE_BYTES + MIN_CHUNK_SIZE_BYTES


#define FINGERPRINT_CONTENT_MAX_PAGES (MAX_CHUNK_SIZE_BYTES / FINGERPRINT_CACHE_PAGE_SIZE) + SAFETY_NUM_PAGES_OVERFLOW_MAX_CHUNK_SIZE

// Determines the threshold deciding to send normal message or proceed with fingerprinting
#define THRESHOLD_MULTIPLE_MIN_CHUNK_SIZE 5
#define FINGERPRINT_MSG_SIZE_THRESHOLD THRESHOLD_MULTIPLE_MIN_CHUNK_SIZE * MIN_CHUNK_SIZE_BYTES

#endif
