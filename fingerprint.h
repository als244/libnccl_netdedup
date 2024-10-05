#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include "common.h"

#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>

typedef enum fingerprint_type{
	SHA256_HASH = 0, // 32 bytes
	SHA512_HASH = 1, // 64 byte
	SHA1_HASH = 2, // 20 bytes
	MD5_HASH = 3, // 16 bytes
	BLAKE3_HASH = 4, // 32 bytes
} FingerprintType;


typedef struct fingerprinting_settings {
	uint8_t fingerprint_num_bytes;
	uint64_t rabin_p;
	uint8_t rabin_m_bits;
	uint8_t window_bytes;
	uint8_t lower_bits;
	uint32_t min_chunk_size_bytes;
	uint32_t max_chunk_size_bytes;
	uint8_t magic_val; 
	uint64_t rabin_table[256];
} Fingerprinting_Settings;


typedef struct fingerprint {
	uint64_t content_size;
	uint8_t fingerprint[FINGERPRINT_NUM_BYTES];
} Fingerprint;


void print_hex(uint8_t * fingerprint, int num_bytes);
void print_sha256(uint8_t * fingerprint);
uint64_t fingerprint_to_least_sig64(uint8_t * fingerprint, int fingerprint_num_bytes);
void do_fingerprinting_sha256(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint);
void do_fingerprinting(void * data, uint64_t num_bytes, uint64_t * ret_num_fingerprints, uint8_t * fingerprints, uint64_t * content_sizes,
	uint64_t rabin_p, uint64_t rabin_m_bits, uint64_t * rabin_table, uint64_t window_bytes, uint8_t lower_bits, uint64_t min_chunk_size_bytes, uint64_t max_chunk_size_bytes, uint64_t magic_val);

uint64_t * gen_rabin_table_alloc(uint64_t rabin_p, uint64_t window_bytes);
void gen_rabin_table(uint64_t rabin_p, uint64_t window_bytes, uint64_t * rabin_table);

void save_fingerprinting_settings(Fingerprinting_Settings * settings, uint8_t fingerprint_num_bytes, uint64_t rabin_p, uint8_t rabin_m_bits, uint8_t window_bytes, uint8_t lower_bits, 
															uint32_t min_chunk_size_bytes, uint32_t max_chunk_size_bytes, uint8_t magic_val);
// void do_fingerprinting(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint, FingerprintType fingerprint_type);
// void do_fingerprinting_evp(void * data, uint64_t num_bytes, uint8_t * ret_fingerprint, FingerprintType fingerprint_type);

#endif
