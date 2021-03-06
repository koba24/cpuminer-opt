#include "jha-gate.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
//#include "avxdefs.h"

#if defined(JHA_4WAY)

#include "algo/blake/blake-hash-4way.h"
#include "algo/skein/skein-hash-4way.h"
#include "algo/jh/jh-hash-4way.h"
#include "algo/keccak/keccak-hash-4way.h"
#include "algo/groestl/aes_ni/hash-groestl.h"

//static __thread keccak512_4way_context jha_kec_mid
//                                   __attribute__ ((aligned (64)));

void jha_hash_4way( void *out, const void *input )
{
    uint64_t hash0[8] __attribute__ ((aligned (64)));
    uint64_t hash1[8] __attribute__ ((aligned (64)));
    uint64_t hash2[8] __attribute__ ((aligned (64)));
    uint64_t hash3[8] __attribute__ ((aligned (64)));
    uint64_t vhash[8*4] __attribute__ ((aligned (64)));
    uint64_t vhash0[8*4] __attribute__ ((aligned (64)));
    uint64_t vhash1[8*4] __attribute__ ((aligned (64)));
    __m256i mask0, mask1;
    __m256i* vh = (__m256i*)vhash;
    __m256i* vh0 = (__m256i*)vhash0;
    __m256i* vh1 = (__m256i*)vhash1;

    blake512_4way_context  ctx_blake;
    hashState_groestl      ctx_groestl;
    jh512_4way_context     ctx_jh;
    skein512_4way_context  ctx_skein;
    keccak512_4way_context ctx_keccak;

    keccak512_4way_init( &ctx_keccak );
    keccak512_4way( &ctx_keccak, input, 80 );
    keccak512_4way_close( &ctx_keccak, vhash );

//    memcpy( &ctx_keccak, &jha_kec_mid, sizeof jha_kec_mid );
//    keccak512_4way( &ctx_keccak, input + (64<<2), 16 );
//    keccak512_4way_close( &ctx_keccak, vhash );

    // Heavy & Light Pair Loop
    for ( int round = 0; round < 3; round++ )
    {
//       memset_zero_256( vh0, 20 );
//       memset_zero_256( vh1, 20 );

      // positive logic, if maski select vhi
      // going from bit to mask reverses logic such that if the test bit is set
      // zero will be put in mask0, meaning don't take vh0. mask1 is
      // inverted so 1 will be put in mask1 meaning take it.
      mask0 = mm256_negate_64(
                     _mm256_and_si256( vh[0], _mm256_set1_epi64x( 0x1 ) ) );
      mask1 = mm256_not( mask0 );

//       mask = _mm256_sub_epi64( _mm256_and_si256( vh[0],
//                     _mm256_set1_epi64x( 0x1 ) ), _mm256_set1_epi64x( 0x1 ) );

       // groestl (serial) v skein

       mm256_deinterleave_4x64( hash0, hash1, hash2, hash3, vhash, 512 );

       init_groestl( &ctx_groestl, 64 );
       update_and_final_groestl( &ctx_groestl, (char*)hash0,
                     (char*)hash0, 512 );

       init_groestl( &ctx_groestl, 64 );
       update_and_final_groestl( &ctx_groestl, (char*)hash1,
                                          (char*)hash1, 512 );

       init_groestl( &ctx_groestl, 64 );
       update_and_final_groestl( &ctx_groestl, (char*)hash2,
                                          (char*)hash2, 512 );
       init_groestl( &ctx_groestl, 64 );
       update_and_final_groestl( &ctx_groestl, (char*)hash3,
                                          (char*)hash3, 512 );

       mm256_interleave_4x64( vhash0, hash0, hash1, hash2, hash3, 512 );

       // skein

       skein512_4way_init( &ctx_skein );
       skein512_4way( &ctx_skein, vhash, 64 );
       skein512_4way_close( &ctx_skein, vhash1 );

       // merge vectored hash
       for ( int i = 0; i < 8; i++ )
       {
          vh[i] = _mm256_or_si256( _mm256_and_si256( vh0[i], mask0 ),
                                   _mm256_and_si256( vh1[i], mask1 ) );
/*
          vha256[i] = _mm256_maskload_epi64( 
                                      vhasha + i*4, mm256_not( mask ) );
          vhb256[i] = _mm256_maskload_epi64(
                                      vhashb + i*4, mask );
          vh256[i]  = _mm256_or_si256( vha256[i], vhb256[i] );
*/
       }

       // blake v jh

       blake512_4way_init( &ctx_blake );
       blake512_4way( &ctx_blake, vhash, 64 );
       blake512_4way_close( &ctx_blake, vhash0 );

       jh512_4way_init( &ctx_jh );
       jh512_4way( &ctx_jh, vhash, 64 );
       jh512_4way_close( &ctx_jh, vhash1 );

       // merge hash
       for ( int i = 0; i < 8; i++ )
       {
          vh[i] = _mm256_or_si256( _mm256_and_si256( vh0[i], mask0 ),
                                   _mm256_and_si256( vh1[i], mask1 ) );
/*
          vha256[i] = _mm256_maskload_epi64(
                                      vhasha + i*4, mm256_not( mask ) );
          vhb256[i] = _mm256_maskload_epi64(
                                      vhashb + i*4, mask );
          vh256[i]  = _mm256_or_si256( vha256[i], vhb256[i] );
*/
       }
    }

    mm256_deinterleave_4x64( out, out+32, out+64, out+96, vhash, 256 );

//    memcpy( output,       hash0, 32 );
//    memcpy( output+32,    hash1, 32 );
//    memcpy( output+64,    hash2, 32 );
//    memcpy( output+96,    hash3, 32 );

}

int scanhash_jha_4way( int thr_id, struct work *work, uint32_t max_nonce,
                       uint64_t *hashes_done )
{
     uint32_t hash[8*4] __attribute__ ((aligned (64)));
     uint32_t vdata[20*4] __attribute__ ((aligned (64)));
     uint32_t endiandata[20] __attribute__((aligned(64)));
	uint32_t *pdata = work->data;
	uint32_t *ptarget = work->target;
	const uint32_t first_nonce = pdata[19];
	const uint32_t Htarg = ptarget[7];
	uint32_t n = pdata[19];
     uint32_t *nonces = work->nonces;
     bool *found = work->nfound;
     int num_found = 0;
     uint32_t *noncep0 = vdata + 73;   // 9*8 + 1
     uint32_t *noncep1 = vdata + 75;
     uint32_t *noncep2 = vdata + 77;
     uint32_t *noncep3 = vdata + 79;

	uint64_t htmax[] = {
		0,
		0xF,
		0xFF,
		0xFFF,
		0xFFFF,
		0x10000000
	};
	uint32_t masks[] = {
		0xFFFFFFFF,
		0xFFFFFFF0,
		0xFFFFFF00,
		0xFFFFF000,
		0xFFFF0000,
		0
	};

   // we need bigendian data...
   for ( int i=0; i < 19; i++ )
      be32enc( &endiandata[i], pdata[i] );

   uint64_t *edata = (uint64_t*)endiandata;
   mm256_interleave_4x64( (uint64_t*)vdata, edata, edata, edata, edata, 640 );

   // precalc midstate for keccak
//   keccak512_4way_init( &jha_kec_mid );
//   keccak512_4way( &jha_kec_mid, vdata, 64 );

   for ( int m = 0; m < 6; m++ )
   {
      if ( Htarg <= htmax[m] )
      {
         uint32_t mask = masks[m];
         do {
              found[0] = found[1] = found[2] = found[3] = false;
              be32enc( noncep0, n   );
              be32enc( noncep1, n+1 );
              be32enc( noncep2, n+2 );
              be32enc( noncep3, n+3 );

              jha_hash_4way( hash, vdata );

              pdata[19] = n;

              if ( ( !(hash[7] & mask) )
                   && fulltest( hash, ptarget ) )
              {
                 found[0] = true;
                 num_found++;
                 nonces[0] = n;
                 work_set_target_ratio( work, hash );
              }
              if ( ( !((hash+8)[7] & mask) )
                   && fulltest( hash+8, ptarget ) )
              {
                 found[1] = true;
                 num_found++;
                 nonces[1] = n+1;
                 work_set_target_ratio( work, hash+8 );
              }
              if ( ( !((hash+16)[7] & mask) )
                 && fulltest( hash+16, ptarget ) )
              {
                 found[2] = true;
                 num_found++;
                 nonces[2] = n+2;
                 work_set_target_ratio( work, hash+16 );
              }
              if ( ( !((hash+24)[7] & mask) )
                   && fulltest( hash+24, ptarget ) )
              {
                 found[3] = true;
                 num_found++;
                 nonces[3] = n+3;
                 work_set_target_ratio( work, hash+24 );
              }
              n += 4;
         } while ( ( num_found == 0 ) && ( n < max_nonce )
                     && !work_restart[thr_id].restart );

         break;
      }
   }

   *hashes_done = n - first_nonce + 1;
   return num_found;
}
#endif
