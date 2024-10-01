#ifndef FINGERPRINT_CACHE_H
#define FINGERPRINT_CACHE_H

#include "common.h"
#include "fingerprint.h"
#include "fingerprint_table.h"


typedef struct fingerprint_cache_stats {
	uint64_t total_recv_bytes;
	uint64_t populated_from_cache_bytes;
	uint64_t total_fingerprints;
	uint64_t total_found_fingerprints;
} Fingerprint_Cache_Stats;

typedef struct fingerprint_cache {
	// mapping from fingerprint (fingerprint_num_bytes) => 
	// index within cache
	Fingerprinting_Settings fingerprinting_settings;
	Fingerprint_Cache_Stats stats;
	Fingerprint_Table table;
	uint64_t capacity_bytes;
	uint8_t cache[FINGERPRINT_CACHE_NUM_PAGES * FINGERPRINT_CACHE_PAGE_SIZE];
	uint64_t num_free_pages;
	uint64_t free_pages[FINGERPRINT_CACHE_NUM_PAGES];
	uint64_t free_pages_read_ind;
	uint64_t free_pages_write_ind;
	pthread_mutex_t cache_lock;
} Fingerprint_Cache;


// Assumes memory has already been allocated for fingerprinting table/cache
void init_fingerprint_cache(Fingerprint_Cache * init_cache);


// Returns the fingerprint entry into the cache
// If already exists, then doesn't insert, but still returns the entry
int insert_fingerprint(Fingerprint_Cache * cache, Fingerprint * new_fingerprint, void * content, Fingerprint_Entry * ret_entry);


// assumes ret_entry has backing memory for the fingerprint_entry allocated
// returns 1 upon success and 0 upon not found
int lookup_fingerprint(Fingerprint_Cache * cache, uint8_t * fingerprint, Fingerprint_Entry * ret_entry);


void copy_fingerprint_content(void * dest_buffer, Fingerprint_Cache * cache, Fingerprint_Entry * entry);

#endif