#include "fingerprint_cache.h"

// Assumes memory has already been allocated for fingerprinting table/cache
void init_fingerprint_cache(Fingerprint_Cache * init_cache) {

	// 1.) Save Fingerprinting Settings
	save_fingerprinting_settings(&(init_cache -> fingerprinting_settings), FINGERPRINT_NUM_BYTES, 
									RABIN_P, RABIN_M_BITS, WINDOW_BYTES, LOWER_BITS, MIN_CHUNK_SIZE_BYTES, MAX_CHUNK_SIZE_BYTES, MAGIC_VAL);

	// 2.) Initialize Blank Table

	init_fingerprint_table(&init_cache -> table);

	// 3.) Setup Blank Cache
	init_cache -> capacity_bytes = FINGERPRINT_CACHE_NUM_BYTES;

	// 4.) Setup the cache page tracker
	init_cache -> num_free_pages = FINGERPRINT_CACHE_NUM_PAGES;
	init_cache -> free_pages_read_ind = 0;
	init_cache -> free_pages_write_ind = 0;
	for (uint64_t i = 0; i < FINGERPRINT_CACHE_NUM_PAGES; i++){
		(init_cache -> free_pages)[i] = i;
	}

	// 4.) Init Shared Lock
	pthread_mutex_init(&(init_cache -> cache_lock), 0);

	// 5.) Initialize stats
	init_cache -> stats.total_recv_bytes = 0;
	init_cache -> stats.populated_from_cache_bytes = 0;
	init_cache -> stats.total_fingerprints = 0;
	init_cache -> stats.total_found_fingerprints = 0;

	return;
}


// Assume ret pages has been allocated already
int get_free_cache_pages(Fingerprint_Cache * cache, int num_pages, uint64_t * ret_pages){

	if ((num_pages > FINGERPRINT_CONTENT_MAX_PAGES) || (num_pages > cache -> num_free_pages)){
		return -1;
	}

	uint64_t cur_free_page_ind = cache -> free_pages_read_ind;
	for (int i = 0; i < num_pages; i++){
		ret_pages[i] = (cache -> free_pages)[cur_free_page_ind];
		cur_free_page_ind = ((cur_free_page_ind + 1) % FINGERPRINT_CACHE_NUM_PAGES);
	}

	cache -> num_free_pages -= num_pages;
	cache -> free_pages_read_ind = cur_free_page_ind;
	return 0;
}


// returns the page number that content starts at
int insert_fingerprint_cache(Fingerprint_Cache * cache, Fingerprint * new_fingerprint, void * content, Fingerprint_Entry * ret_entry){

	int ret;

	uint64_t content_size = new_fingerprint -> content_size;
	uint64_t content_num_pages = MY_CEIL(content_size, FINGERPRINT_CACHE_PAGE_SIZE);


	// get pages
	// this will popoulate the entry with the page numbers allocated for the fingerprint
	ret = get_free_cache_pages(cache, content_num_pages, ret_entry -> content_page_nums);
	if (unlikely(ret)){
		fprintf(stderr, "Error: unable to get free pages from cache to write fingerprint content\n");
		return -1;
	}

	// set content size in entry
	ret_entry -> content_size = content_size;


	// for all but the last page copy the full page
	void * cache_page_loc;
	uint64_t cache_page_num;
	void * cur_content = content;
	for (uint64_t i = 0; i < content_num_pages - 1; i++){
		cache_page_num = (ret_entry -> content_page_nums)[i];
		cache_page_loc = (void *) (((uint64_t) (cache -> cache)) + cache_page_num * FINGERPRINT_CACHE_PAGE_SIZE);
		memcpy(cache_page_loc, cur_content, FINGERPRINT_CACHE_PAGE_SIZE);
		cur_content += FINGERPRINT_CACHE_PAGE_SIZE;
	}

	// handle the last page which may have less bytes
	uint64_t remain_bytes = content_size % FINGERPRINT_CACHE_PAGE_SIZE;
	if (remain_bytes == 0){
		remain_bytes = FINGERPRINT_CACHE_PAGE_SIZE;
	}
	cache_page_num = (ret_entry -> content_page_nums)[content_num_pages - 1];
	cache_page_loc = (void *) (((uint64_t) (cache -> cache)) + cache_page_num * FINGERPRINT_CACHE_PAGE_SIZE);
	memcpy(cache_page_loc, cur_content, remain_bytes);

	return 0;
}


int insert_fingerprint(Fingerprint_Cache * cache, Fingerprint * new_fingerprint, void * content, Fingerprint_Entry * ret_entry) {

	// for now just doing ugly locking for correctness

	pthread_mutex_lock(&(cache -> cache_lock));

	Fingerprint_Entry * entry_ref;

	// if the inserted passed in null
	if (!ret_entry){
		Fingerprint_Entry entry;
		memset(&entry, 0, sizeof(Fingerprint_Entry));
		entry_ref = &entry;
	}
	else{
		entry_ref = ret_entry;
	}



	// this will allocate pages in the cache and copy the fingerprint content
	// it wll also populate the entry with content size and the page numbers
	// that hold the content
	int ret = insert_fingerprint_cache(cache, new_fingerprint, content, entry_ref);
	if (unlikely(ret)){
		fprintf(stderr, "Error: could not insert fingerprint to cache\n");
		pthread_mutex_unlock(&(cache -> cache_lock));
		return -1;
	}

	uint64_t result = find_fingerprint_table(&(cache -> table), new_fingerprint -> fingerprint, entry_ref);

	// we didn't find it
	if (result == FINGERPRINT_TABLE_SIZE){
		ret = insert_fingerprint_table(&(cache -> table), new_fingerprint -> fingerprint, entry_ref);
		if (ret){
			fprintf(stderr, "Error: could not insert fingerprint to table\n");
			pthread_mutex_unlock(&(cache -> cache_lock));
			return -1;
		}
	}

	pthread_mutex_unlock(&(cache -> cache_lock));
	return 0;
}


// assumes ret_entry has backing memory for the fingerprint_entry allocated
// returns 1 upon success and 0 upon not found
int lookup_fingerprint(Fingerprint_Cache * cache, uint8_t * fingerprint, Fingerprint_Entry * ret_entry){

	// for now just doing ugly locking for correctness
	pthread_mutex_lock(&(cache -> cache_lock));
	
	// will populate the fingerprint_table
	uint64_t ret = find_fingerprint_table(&(cache -> table), fingerprint, ret_entry);

	pthread_mutex_unlock(&(cache -> cache_lock));
	return ret < FINGERPRINT_TABLE_SIZE;
}


// ASSUMES DEST BUFFER HAS MEMORY ALLOCATED!
void copy_fingerprint_content(void * dest_buffer, Fingerprint_Cache * cache, Fingerprint_Entry * entry) {

	uint64_t content_size = entry -> content_size;

	uint64_t content_num_pages = MY_CEIL(content_size, FINGERPRINT_CACHE_PAGE_SIZE);

	uint64_t * content_page_nums = entry -> content_page_nums;
	void * cur_dest = dest_buffer;

	if (TO_PRINT_FINGERPRINT_CACHE){
		printf("Copying from fingerprint cache\n\tContent Size: %lu\n\tContent Num Pages: %lu\n\n", content_size, content_num_pages);
	}

	// for all but the last page copy the full page
	void * cache_page_loc;
	uint64_t cache_page_num;
	for (uint64_t i = 0; i < content_num_pages - 1; i++){
		cache_page_num = content_page_nums[i];

		if (TO_PRINT_FINGERPRINT_CACHE){
			printf("%lu.) Copying page #%lu\n", i, cache_page_num);
		}

		cache_page_loc = (void *) (((uint64_t) (cache -> cache)) + (cache_page_num * FINGERPRINT_CACHE_PAGE_SIZE));
		memcpy(cur_dest, cache_page_loc, FINGERPRINT_CACHE_PAGE_SIZE);
		cur_dest += FINGERPRINT_CACHE_PAGE_SIZE;
	}

	// handle the last page which may have less bytes
	uint64_t remain_bytes = content_size % FINGERPRINT_CACHE_PAGE_SIZE;

	if (TO_PRINT_FINGERPRINT_CACHE){
		printf("Content size; %lu\n", content_size);
		printf("Remaining bytes: %lu\n", remain_bytes);
	}

	if (remain_bytes == 0){
		remain_bytes = FINGERPRINT_CACHE_PAGE_SIZE;
	}
	cache_page_num = content_page_nums[content_num_pages - 1];
	cache_page_loc = (void *) (((uint64_t) (cache -> cache)) + (cache_page_num * FINGERPRINT_CACHE_PAGE_SIZE));
	memcpy(cur_dest, cache_page_loc, remain_bytes);

	if (TO_PRINT_FINGERPRINT_CACHE){
		printf("Finsihing copying from fingerprint cache!\n\n");
	}
	
	return;
}