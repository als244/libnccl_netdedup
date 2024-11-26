#include "fingerprint_table.h"

inline uint64_t fingerprint_hash_func_64(uint64_t key) {
	key = (key << 21) - key - 1;
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key % FINGERPRINT_TABLE_SIZE;
}

inline uint64_t inline_fingerprint_to_least_sig64(uint8_t * fingerprint){
	uint8_t * least_sig_start = fingerprint + FINGERPRINT_NUM_BYTES - sizeof(uint64_t);
	uint64_t result = 0;
    for(int i = 0; i < 8; i++){
        result <<= 8;
        result |= (uint64_t)least_sig_start[i];
    }
    return result;
}

// assumes memory for fingerprint_table has already been allocated
int init_fingerprint_table(Fingerprint_Table * fingerprint_table) {

	fingerprint_table -> cnt = 0;
	fingerprint_table -> size = FINGERPRINT_TABLE_SIZE;

	// also uses an extra byte per entry to indicate if 
	// there is an item in that slot (otherwise wouldn't be able
	// to distinguish the key/value should be 0's or if they are empty)

	// this will be a bit vector where each item is a uint64_t
	// it is of size ceil(fingerprint_table -> size / 64) == size shifted by 

	// the index into the table is the high order bits 56 bits of the bucket index within
	// fingerprint_table -> items

	// and the bit position within each vector is the low order 6 bits
	// initialize everything to empty
	int bit_vector_els = MY_CEIL(FINGERPRINT_TABLE_SIZE, 64);

	for (int i = 0; i < bit_vector_els - 1; i++){
		(fingerprint_table -> is_empty_bit_vector)[i] = 0xFFFFFFFFFFFFFFFF;
	}

	int last_vec_els = FINGERPRINT_TABLE_SIZE & 0x3F;

	if (last_vec_els == 0){
		(fingerprint_table -> is_empty_bit_vector)[bit_vector_els - 1] = 0xFFFFFFFFFFFFFFFF;
	}
	else{
		(fingerprint_table -> is_empty_bit_vector)[bit_vector_els - 1] = (1UL << last_vec_els) - 1;
	}

	return 0;
}

// returns size upon failure to find slot. should never happen because checked if null
// beforehand

// leaving the option to set to_get_empty to true, meaning that we should search for the next non-null slot
// in the table

// This is valuable during resizing
uint64_t get_next_ind_fingerprint_table(uint64_t * is_empty_bit_vector, uint64_t table_size, uint64_t start_ind){

	// need to pass in a valid starting index
	if (unlikely(start_ind >= table_size)){
		return table_size;
	}

	// assert (is_empty_bit_vector) == (table_size >> 6) + 1

	// Determine the next largest empty index in the table (wraping around at the end)
	// The low order 6 bits of hash_ind refer to the bit position within each element of the
	// the bit vector.  The high-order 56 bits refer the index within the actual vector

	uint64_t bit_vector_size = MY_CEIL(FINGERPRINT_TABLE_SIZE, 64);
	// higher order bits the hash index
	uint64_t start_vec_ind = start_ind >> 6;
	// low order 6 bits
	uint8_t start_bit_ind = start_ind & (0xFF >> 2); 
		
	// before we start we need to clear out the bits strictly less than bit ind
	// if we don't find a slot looking through all the other elements in the bit 
	// vector we will wrap around to these value
	uint64_t search_vector = is_empty_bit_vector[start_vec_ind] & (0xFFFFFFFFFFFFFFFF << start_bit_ind);

	// need to ensure search vector bits

	// 64 because each element in bit-vector contains 64 possible hash buckets that could be full
	// Will add the returned next closest bit position to this value to obtain the next empty slot
	// With a good hash function and load factor hopefully this vector contains the value or at least
	// in the next few searches and won't need more than 1 search vector
		
	
	uint64_t cur_vec_ind = start_vec_ind;
	uint8_t least_sig_one_bit_ind;
	uint64_t insert_ind;

	// less than or equal because we might need the low order bits that we originally masked out

	// With good hash function and load factor this should only be 1 iteration when doing inserts
	// However for resizing we may find a long 

	// can be equal if we wrap around to the same starting index and
	// look at low order bits
	while(cur_vec_ind <= start_vec_ind + bit_vector_size){

		if (search_vector == 0){
			cur_vec_ind++;
			// if the cur_vec_ind would be wrapped around we don't
			// need to do any masking because we just care about the low
			// order bits which weren't seen the first go-around
			search_vector = is_empty_bit_vector[cur_vec_ind % bit_vector_size];
			continue;
		}

		// returns index of the least significant 1-bit
		least_sig_one_bit_ind = __builtin_ctzll(search_vector);
		
		insert_ind = 64 * (cur_vec_ind % bit_vector_size) + least_sig_one_bit_ind;
		return insert_ind;
	}

	// indicate that we couldn't find an empty slot (or full slot if to_flip_empty flag is true)
	return table_size;
}


// returns 0 on success, -1 on error

// does memcopiess of key and value into the table array
// assumes the content of the key cannot be 0 of size key_size_bytes
int insert_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * entry) {

	uint64_t size = fingerprint_table -> size;
	uint64_t cnt = fingerprint_table -> cnt;

	// should only happen when cnt = max_size
	// otherwise we would have grown the table after the 
	// prior triggering insertion
	if (unlikely(cnt == size)){
		printf("Error: Fingerprint table full\n\tTable Count: %lu\n\tTable Size: %lu\n\n", cnt, size);
		return -1;
	}


	// 1.) Lookup where to place this item in the table

	// acutally compute the hash index

	uint64_t fingerprint_least_sig_64 = inline_fingerprint_to_least_sig64(fingerprint);
	uint64_t hash_ind = fingerprint_hash_func_64(fingerprint_least_sig_64);

	// we already saw cnt != size so we are guaranteed for this to succeed
	uint64_t insert_ind = get_next_ind_fingerprint_table(fingerprint_table -> is_empty_bit_vector, fingerprint_table -> size, hash_ind);
	
	uint64_t key_size_bytes = FINGERPRINT_KEY_SIZE_BYTES;
	uint64_t value_size_bytes = FINGERPRINT_VALUE_SIZE_BYTES;


	// Now we want to insert into the table by copying key and value 
	// into the appropriate insert ind and then setting the 
	// is_empty bit to 0 within the bit vector


	// 2.) Copy the key and value into the table 
	//		(memory has already been allocated for them within the table)

	void * items = fingerprint_table -> items;

	// setting the position for the key in the table
	// this is based on the insert_index that was returned to us
	void * key_pos = (void *) (((uint64_t) items) + (insert_ind * (key_size_bytes + value_size_bytes)));
	// advance passed the key we will insert
	void * value_pos = (void *) (((uint64_t) key_pos) + key_size_bytes);

	// Actually place in table
	memcpy(key_pos, fingerprint, key_size_bytes);
	memcpy(value_pos, entry, value_size_bytes);


	// 3.) Update bookkeeping values
	cnt += 1;
	fingerprint_table -> cnt = cnt;


	// clearing the entry for this insert_ind in the bit vector

	// the bucket's upper bits represent index into the bit vector elements
	// and the low order 6 bits represent offset into element. Set to 0 

	// needs to be 1UL otherwise will default to 1 byte
	(fingerprint_table -> is_empty_bit_vector)[insert_ind >> 6] &= ~(1UL << (insert_ind & (0xFF >> 2)));

	return 0;


}



uint64_t find_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * ret_entry){

	uint64_t value_size_bytes = FINGERPRINT_VALUE_SIZE_BYTES;

	if (fingerprint_table -> items == NULL){
		return FINGERPRINT_TABLE_SIZE;
	}

	uint64_t size = fingerprint_table -> size;
	uint64_t key_size_bytes = FINGERPRINT_KEY_SIZE_BYTES;
	
	uint64_t fingerprint_least_sig_64 = inline_fingerprint_to_least_sig64(fingerprint);
	uint64_t hash_ind = fingerprint_hash_func_64(fingerprint_least_sig_64);

	// get the next null value and search up to that point
	// because we are using linear probing if we haven't found the key
	// by this point then we can terminate and return that we didn't find anything

	// we could just walk along and check the bit vector as we go, but this is easily
	// (at albeit potential performance hit if table is very full and we do wasted work)
	uint64_t * is_empty_bit_vector = fingerprint_table -> is_empty_bit_vector;
	
	uint64_t next_empty = get_next_ind_fingerprint_table(is_empty_bit_vector, size, hash_ind);
	
	uint64_t cur_ind = hash_ind;


	void * cur_table_key = (void *) (((uint64_t) fingerprint_table -> items) + (cur_ind * (key_size_bytes + value_size_bytes)));

	uint64_t items_to_check;
	if (cur_ind <= next_empty){
		items_to_check = next_empty - cur_ind;
	}
	// the next empty slot needs to be
	// wrapped around
	else{
		items_to_check = (size - cur_ind) + next_empty + 1;
	}


	int key_cmp;
	uint64_t i = 0;
	while (i < items_to_check) {

		// compare the key
		key_cmp = memcmp(fingerprint, cur_table_key, key_size_bytes);
		// if we found the key
		if (key_cmp == 0){

			// if we want the key, we want the value immediately after, so we add key_size_bytes
			// to the current key
			void * table_value = (void *) (((uint64_t) cur_table_key) + key_size_bytes);
			memcpy(ret_entry, table_value, value_size_bytes);
			return cur_ind;
		}

		// update the next key position which will be just 1 element higher so we can add the size of 1 item

		

		// next empty might have a returned a value that wrapped around
		// if the whole table
		cur_ind = (cur_ind + 1) % size;


		// being explicity about type casting for readability...
		cur_table_key = (void *) (((uint64_t) fingerprint_table -> items) + (cur_ind * (key_size_bytes + value_size_bytes)));

		i += 1;
	}
	
	// We didn't find the element
	return FINGERPRINT_TABLE_SIZE;
}


// returns 0 upon successfully removing, -1 on error finding. 

// Note: Might want to have different return value
// from function to indicate a fatal error that could have occurred within resized (in the gap of freeing larger
// table and allocating new, smaller one)

// if copy_val is set to true then copy back the item
int remvove_fingerprint_table(Fingerprint_Table * fingerprint_table, uint8_t * fingerprint, Fingerprint_Entry * ret_entry) {


	// remove is equivalent to find, except we need to also:
	//	a.) Confirm that other positions can still be found (by replacing as needed)
	//	b.) mark the empty bit/decrease count
	//	c.) potentially shrink


	// 1.) Search for item!

	// if the item existed this will handle copying
	// because we are removing from the table we need to copy the value
	uint64_t empty_ind = find_fingerprint_table(fingerprint_table, fingerprint, ret_entry);

	// item didn't exist so we immediately return
	if (empty_ind == FINGERPRINT_TABLE_SIZE){
		return -1;
	}


	// 2.) Ensure that we will still be able to find other items that have collided
	// 		with a hash that is >= to the index we removed
	uint64_t size = fingerprint_table -> size;
	uint64_t key_size_bytes = FINGERPRINT_KEY_SIZE_BYTES;
	uint64_t value_size_bytes = FINGERPRINT_VALUE_SIZE_BYTES;
	uint64_t * is_empty_bit_vector = fingerprint_table -> is_empty_bit_vector;
	uint64_t next_empty = get_next_ind_fingerprint_table(is_empty_bit_vector, size, empty_ind);

	uint64_t items_to_check;


	// Now we are only checking AFTER the "removed" index
	if (empty_ind < next_empty){
		items_to_check = next_empty - empty_ind - 1;
	}
	// the next empty slot needs to be
	// wrapped around
	else{
		items_to_check = (size - empty_ind - 1) + next_empty;
	}

	uint64_t i = 0;
	uint64_t hash_ind;
	void * cur_table_key;
	uint64_t cur_ind = (empty_ind + 1) % size;
	void * empty_table_key = (void *) (((uint64_t) fingerprint_table -> items) + (empty_ind * (key_size_bytes + value_size_bytes)));
	
	uint64_t fingerprint_least_sig_64;
	while (i < items_to_check){ 

		cur_table_key = (void *) (((uint64_t) fingerprint_table -> items) + (cur_ind * (key_size_bytes + value_size_bytes)));

		fingerprint_least_sig_64 = inline_fingerprint_to_least_sig64(cur_table_key);
		// get the hash index for the entry in the table to see if it could still be found
		hash_ind = fingerprint_hash_func_64(fingerprint_least_sig_64);

		// Ref: https://stackoverflow.com/questions/9127207/hash-table-why-deletion-is-difficult-in-open-addressing-scheme

		// If cur_table_key wouldn't be able to be found again we need to move it to the 
		// empty_ind position to ensure that it could be
		if (((cur_ind > empty_ind) && (hash_ind <= empty_ind || hash_ind > cur_ind))
			|| ((cur_ind < empty_ind) && (hash_ind <= empty_ind && hash_ind > cur_ind))){
			
			// perform the replacement
			memcpy(empty_table_key, cur_table_key, key_size_bytes + value_size_bytes);

			// now reset the next time we might need to replace
			empty_ind = cur_ind;
			empty_table_key = cur_table_key;
		}

		i += 1;

		// update position of cur table key and index
		cur_ind = (cur_ind + 1) % size;
		

	}

	// 3.) Do proper bookkeeping. Mark the last empty slot (after re-shuffling) as empty now

	// otherwise we need to update
	fingerprint_table -> cnt -= 1;

	// clearing the entry for this insert_ind in the bit vector

	// remove element from table
	memset(empty_table_key, 0, key_size_bytes + value_size_bytes);

	// the bucket's upper bits represent index into the bit vector elements
	// and the low order 6 bits represent offset into element. 

	// Set to 1 to indicate this bucket is now free 
	(fingerprint_table -> is_empty_bit_vector)[empty_ind >> 6] |= (1UL << (empty_ind & (0xFF >> 2)));

	return 0;

}
