#include "fingerprint.h"

void gen_rabin_table(uint64_t rabin_p, uint64_t window_bytes, uint64_t * rabin_table){

	uint64_t p_window_bytes = 1;
	for (int i = 1; i < window_bytes; i++){
		p_window_bytes *= rabin_p;
	}
	for (int i = 0; i < 256; i++){
		rabin_table[i] = i * p_window_bytes;
	}
	return;
}

void save_fingerprinting_settings(Fingerprinting_Settings * settings, uint8_t fingerprint_num_bytes, uint64_t rabin_p, uint8_t rabin_m_bits, uint8_t window_bytes, uint8_t lower_bits, 
															uint32_t min_chunk_size_bytes, uint32_t max_chunk_size_bytes, uint8_t magic_val){

	settings -> fingerprint_num_bytes = fingerprint_num_bytes;
	settings -> rabin_p = rabin_p;
	settings -> rabin_m_bits = rabin_m_bits;
	settings -> window_bytes = window_bytes;
	settings -> lower_bits = lower_bits;
	settings -> min_chunk_size_bytes = min_chunk_size_bytes;
	settings -> max_chunk_size_bytes = max_chunk_size_bytes;
	settings -> magic_val = magic_val;
	gen_rabin_table(rabin_p, window_bytes, settings -> rabin_table);
	return;
}

uint64_t init_rabin(uint8_t * data_bytes, uint64_t rabin_p, uint64_t rabin_mask, uint64_t window_bytes, uint8_t * window){
	uint64_t cur_rabin = 0;
	uint64_t cur_rabin_p = 1;
	for (int i = window_bytes - 1; i >= 0; i--){
		// data_bytes[i] * rabin_p^(window_bytes - i)
		cur_rabin += (data_bytes[i] * cur_rabin_p) & rabin_mask;
		cur_rabin_p *= rabin_p;
		window[i] = data_bytes[i];
	}
	return cur_rabin;
}


void handle_sha(uint8_t * data, uint64_t start_ind, uint64_t size, uint8_t * ret_fingerprint) {
	do_fingerprinting_sha256(data + start_ind, size, ret_fingerprint);
	printf("Computed fingerprint:");
	print_sha256(ret_fingerprint);
	printf("\n\n");
}


// ASSUMING THAT FINGERPRINTS HAS PRE-ALLOCATED MEMORY!

// uint8_t max_fingerprints = (num_bytes + min_chunk_size_bytes) / min_chunk_size_bytes;
// uint8_t fingerprints[max_fingerprints * FINGERPRINT_NUM_BYTES];
// uint64_t boundaries[max_fingerprints];

/// ASSUMING num_bytes > min_chunk_size_bytes
void do_fingerprinting(void * data, uint64_t num_bytes, uint64_t * ret_num_fingerprints, uint8_t * fingerprints, uint64_t * content_sizes,
	uint64_t rabin_p, uint64_t rabin_m_bits, uint64_t * rabin_table, uint64_t window_bytes, uint8_t lower_bits, uint64_t min_chunk_size_bytes, uint64_t max_chunk_size_bytes, uint64_t magic_val){

	
	uint8_t * data_bytes = (uint8_t *) data;
	uint64_t rabin_mask = (1UL << rabin_m_bits) - 1;
	uint64_t magic_mask = (1UL << lower_bits) - 1;
	uint64_t magic_check;
	uint64_t num_fingerprints = 0;

	if (num_bytes <= min_chunk_size_bytes){
		handle_sha(data, 0, num_bytes, fingerprints);
		*ret_num_fingerprints = 1;
		*content_sizes = num_bytes;
		return;
	}



	uint8_t window[window_bytes];
	
	

	// continue up to the initial minimum chunk size
	uint64_t cur_start_ind = 0;
	uint64_t remain_bytes = num_bytes;
	uint64_t cur_size = min_chunk_size_bytes;
	
	uint8_t window_slot = 0;
	uint8_t cur_data_byte;
	uint64_t rabin_diff;

	uint8_t * init_window_byte = (uint8_t *) (((uint64_t) data) + min_chunk_size_bytes - 1 - window_bytes);
	uint64_t cur_rabin = init_rabin(init_window_byte, rabin_p, rabin_mask, window_bytes, window);
	cur_rabin = cur_rabin & rabin_mask;

	
	while(remain_bytes > 0){
		// special case of immediate magic match
		magic_check = cur_rabin & magic_mask;
		if ((magic_check == magic_val) || (cur_size >= max_chunk_size_bytes) || (cur_size >= remain_bytes)){
			
			// check if we are done
			if ((cur_size >= remain_bytes) || ((remain_bytes - cur_size) < min_chunk_size_bytes)){
				cur_size = remain_bytes;
			}

			// if we aren't done
			handle_sha(data, cur_start_ind, cur_size, &fingerprints[num_fingerprints * FINGERPRINT_NUM_BYTES]);
			content_sizes[num_fingerprints] = cur_size;
			num_fingerprints++;

			remain_bytes -= cur_size;
			cur_start_ind += cur_size;

			if (remain_bytes > 0){
				init_window_byte = (uint8_t *) (((uint64_t) data) + cur_start_ind + min_chunk_size_bytes - 1 - window_bytes);
				cur_rabin = init_rabin(init_window_byte, rabin_p, rabin_mask, window_bytes, window);
				cur_size = min_chunk_size_bytes;
			}

			continue;
		}
		
		cur_data_byte = data_bytes[cur_start_ind + cur_size];
		rabin_diff = cur_rabin - rabin_table[window[window_slot]];
		cur_rabin = (rabin_diff * rabin_p + cur_data_byte) & rabin_mask;
		window[window_slot] = cur_data_byte;
		window_slot = (window_slot + 1) % window_bytes;
		cur_size++;		
	}

	// if last iteration made is_done non-zero we already incremented by 1
	// only if there was remainder (and is_done == 2) do we want to add another
	*ret_num_fingerprints = num_fingerprints;
	return;
}


// assumes was allocated beforehand
uint64_t * gen_rabin_table_alloc(uint64_t rabin_p, uint64_t window_bytes){

	uint64_t p_window_bytes = 1;
	for (int i = 1; i < window_bytes; i++){
		p_window_bytes *= rabin_p;
	}

	uint64_t * rabin_table = malloc(256 * sizeof(uint64_t));
	for (int i = 0; i < 256; i++){
		rabin_table[i] = i * p_window_bytes;
	}
	return rabin_table;
}


// These are "deprecated" as of OpenSSL 3.0, but they are faster and simpler...

// should figure out how to have global contexts to avoid overhead because doing this repeatedly...
void do_fingerprinting_sha256(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint){
	
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, num_bytes);
	SHA256_Final(ret_fingerprint, &ctx);

 	return;
}

uint64_t fingerprint_to_least_sig64(uint8_t * fingerprint, int fingerprint_num_bytes){
	uint8_t * least_sig_start = fingerprint + fingerprint_num_bytes - sizeof(uint64_t);
	uint64_t result = 0;
    for(int i = 0; i < 8; i++){
        result <<= 8;
        result |= (uint64_t)least_sig_start[i];
    }
    return result;
}


// should figure out how to have global contexts to avoid overhead because doing this repeatedly...
void do_fingerprinting_sha512(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint){
	
	SHA512_CTX ctx;
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, data, num_bytes);
	SHA512_Final(ret_fingerprint, &ctx);

 	return;
}

// should figure out how to have global contexts to avoid overhead because doing this repeatedly...
void do_fingerprinting_sha1(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint){
	
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, data, num_bytes);
	SHA1_Final(ret_fingerprint, &ctx);

 	return;
}

// should figure out how to have global contexts to avoid overhead because doing this repeatedly...
void do_fingerprinting_md5(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint){
	
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx, data, num_bytes);
	MD5_Final(ret_fingerprint, &ctx);

 	return;
}


// ASSUMING THAT RET_FINGERPRINT HAS PRE-ALLOCATED MEMORY!
void do_fingerprinting_old(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint, FingerprintType fingerprint_type){

	// using functions from OpenSSL's libcrypto
	switch(fingerprint_type){
		case SHA256_HASH:
			do_fingerprinting_sha256(data, num_bytes, ret_fingerprint);
			break;
		case SHA512_HASH:
			do_fingerprinting_sha512(data, num_bytes, ret_fingerprint);
			break;
		case SHA1_HASH:
			do_fingerprinting_sha1(data, num_bytes, ret_fingerprint);
			break;
		case MD5_HASH:
			do_fingerprinting_sha1(data, num_bytes, ret_fingerprint);
			break;
		case BLAKE3_HASH:
			fprintf(stderr, "Error: blake3 is not supported yet\n");
			break;
		default:
			fprintf(stderr, "Error: fingerprint_type not supported\n");
			break;
	}
	return;
}


void print_hex(uint8_t * fingerprint, int num_bytes){
	for (int i = 0; i < num_bytes; i++){
		printf("%02x", fingerprint[i]);
	}
	printf("\n");
}

void print_sha256(uint8_t * fingerprint){
	// sha256 = 256 bits = 32 bytes
	int num_bytes = 32;
	for (int i = 0; i < num_bytes; i++){
		printf("%02x", fingerprint[i]);
	}
	printf("\n");
}



uint8_t get_fingerprint_num_bytes(FingerprintType fingerprint_type){
	switch(fingerprint_type){
		case SHA256_HASH:
			return 32;
		case SHA512_HASH:
			return 64;
		case SHA1_HASH:
			return 20;
		case MD5_HASH:
			return 16;
		case BLAKE3_HASH:
			fprintf(stderr, "Error: blake3 is not supported yet\n");
			return 0;
		default:
			fprintf(stderr, "Error: fingerprint_type not supported\n");
			return 0;
	}
}

char * get_fingerprint_type_name(FingerprintType fingerprint_type){
	switch(fingerprint_type){
		case SHA256_HASH:
			return "SHA256";
		case SHA512_HASH:
			return "SHA512";
		case SHA1_HASH:
			return "SHA1";
		case MD5_HASH:
			return "MD5";
		case BLAKE3_HASH:
			return "BLAKE3";
		default:
			return "UNKNOWN";
	}
}

// should figure out how to have global contexts to avoid overhead because doing this repeatedly...
// THE "New"/"Supported" interface, but 30% slower...
void do_fingerprinting_evp(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint, FingerprintType fingerprint_type){
	
	// using this function instead of on stack for compatibility...
	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();

 	// can do switching here based on fingerprint type..\n
 	const EVP_MD *md = EVP_sha256();

 	// declare using sha256
 	EVP_DigestInit_ex(mdctx, md, NULL);
 	
 	// acutally perform the hashing stored in the context
 	EVP_DigestUpdate(mdctx, data, num_bytes);
 	
 	// copy from context to destination specified as argument
 	unsigned int fingerprint_len;
 	EVP_DigestFinal_ex(mdctx, ret_fingerprint, &fingerprint_len);
 	
 	// reset context
 	EVP_MD_CTX_free(mdctx);

 	return;
}