#ifndef FINGERPRINT_TABLE_H
#define FINGERPRINT_TABLE_H

#include "common.h"

// NOTE: Assumes SINGLE-THREADED and that this table will be responsible for the memory
//			of key's and values inserted. It memcopies the arguments passed in because
//			we will be creating values on the stack and passing those references into
//			these functions. ** Special case: For very large sized keys/values (in bytes), then we would
//			be careful to not have stack-overflow and would dynamically allocate and then
//			copy again and free the original.


// For more details on role of load/shrink factor and relationship with min/max
// sett config.h for example.

// Note that load_factor = 0.25


// This stores the static values of the 
// the table and can be shared

// Within fast tree we can make a TON
// of fast tables so need to conserve memory
// by having trees at each level point to 
// this struct


// The values within fingerprint_cache -> talbe

// Could make some of the values be 32-bit (or less), but leaving more generic for now
typedef struct fingerprint_entry {
	uint64_t content_size;
	// We know all the pages will be full
	// except the last page
	// we can infer how many pages based on content size
	uint64_t content_page_nums[FINGERPRINT_CONTENT_MAX_PAGES];
} Fingerprint_Entry;


#define FINGERPRINT_VALUE_SIZE_BYTES (sizeof(Fingerprint_Entry))


typedef struct fingerprint_table {
	uint64_t cnt;
	uint64_t size;
	// a bit vector of capacity size >> 6 uint64_t's
	// upon an insert an item's current index is checked
	// against this vector to be inserted

	// initialized to all ones. when something get's inserted
	// it flips bit to 0. 

	// will use __builtin_ffsll() to get bit position of least-significant
	// 1 in order to determine the next empty slot
	uint64_t is_empty_bit_vector[(FINGERPRINT_TABLE_SIZE >> 6) + 1];
	// array that is sized
	// (key_size_bytes + value_size_bytes * size
	// the indicies are implied by the total size
	// Assumes all items inserted have the first
	// key_size_bytes of the entry representing
	// the key for fast comparisons
	uint64_t items[FINGERPRINT_TABLE_SIZE * (FINGERPRINT_KEY_SIZE_BYTES + FINGERPRINT_VALUE_SIZE_BYTES)];
} Fingerprint_Table;


// Assumes memory has already been allocated for fingerprint_table container
int init_fingerprint_table(Fingerprint_Table * fingerprint_table);

// all it does is free fingerprint_table -> items
void destroy_fingerprint_table(Fingerprint_Table * fingerprint_table);


uint64_t inline_fingerprint_to_least_sig64(uint8_t * fingerprint);
uint64_t fingerprint_hash_func_64(uint64_t key);



// returns 0 on success, -1 on error

// does memcopiess of key and value into the table array
// assumes the content of the key cannot be 0 of size key_size_bytes
int insert_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * entry);



// Returns the index at which item was found on success, fingerprint_table -> max_size on not found
//	- returning the index makes remove easy (assuming single threaded)

// A copy of the value assoicated with key in the table
// Assumes that memory of value_sized_bytes as already been allocated to ret_val
// And so a memory copy will succeed

// If to_copy_value is set the copy back the the item. If no item exists and this flag is set, ret_value is set to NULL
uint64_t find_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * ret_entry);


// returns 0 upon success, -1 upon error
// The value will copied into ret_value
int remove_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * ret_entry);




#endif