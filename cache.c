#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

/* Implementing a Block Cache for mdadm */

// Global Variables declaration (given)
static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

// Declaring CONSTANTS
const int MIN_NUM_ENTRIES = 2;		// Minimum number of cache entry
const int MAX_NUM_ENTRIES = 4096;	// Maximum number of cache entries


//// Cache CREATE Function - Allocates dynamic space in memory for the required no. of block entries
int cache_create(int num_entries) {

  // This function to return 1 on Success and -1 on Failure

  // Return -1, if Cache is already created
  if ((cache != NULL) || (cache_size != 0))
       return -1;

  // Return -1, if number of Cache entries to be created is Less than the Minimum entries required.
  if (num_entries < MIN_NUM_ENTRIES)
      return -1;

  // Return -1, if number of Cache entries to be created is More than the Maximum possible entries.
  if (num_entries > MAX_NUM_ENTRIES)
      return -1;

  // Dynamically allocate space for the required number of entries in cache
  cache = calloc(num_entries, sizeof(cache_entry_t));

  // set the size of the Cache i.e. 'cache_size' to the number of cache entries
  cache_size = num_entries;

  int i;

  // Initialize the validity of the created cache entries
  for (i = 0; i < cache_size; i++)
      cache[i].valid = false;
  
  // return 1 on Success
  return 1;
  
}


//// Cache DESTROY Function - Frees the dynamic space allocated for Cache
int cache_destroy(void) {

  // This function to return 1 on Success and -1 on Failure

  // Return -1, if no Cache exist
  if ((cache == NULL) || (cache_size == 0))
      return -1;
  
  // Free the dynamically allocated space
  free(cache);
  
  // set size of Cache to zero, and set the cache to NULL
  cache_size = 0;
  cache = NULL;

  // return 1 on Success
  return 1;
  
}


//// Cache LOOKUP Function - Looks up the Block identified by disk_num and block_num in the Cache.
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {

  // This function to return 1 on Success and -1 on Failure

  //// Validate Input parameters

  // Return -1, if no Cache exist
  if ((cache == NULL) || (cache_size == 0))
      return -1;

  if (buf == NULL)
      return -1;

  num_queries += 1; 	// On every Lookup call, increment the global variable 'num_queries'
  
  int i;

  for (i = 0; i < cache_size; i++) {

      // Lookup the Block identified by disk_num and block_num, & check the validity of cache entry
      if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].valid == true)) {

    	  // Lookup Success! Found a valid Cache ! Identified by keys: disk_num and block_num
    	  if (cache[i].block != NULL )
    	      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE); // Copy data from block to buffer 'buf'

	  clock += 1;			// On success, increment the global variable 'clock'
	  cache[i].access_time = clock; // set access_time field to indicate recent use of entry
	  cache[i].valid = true;	// set 'valid' field to indicate the cache entry as valid

	  num_hits += 1;		// On success, increment the global variable 'num_hits'

	  // On success, return 1
	  return 1;
      }

  } // end-of For Loop

  // When the Lookup is Unsuccessful, return -1
  return -1;
  
}


//// Cache INSERT Function
//// Insert the block identified by disk_num and block_num into the Cache
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  
  // This function to return 1 on Success and -1 on Failure

  //// Validate Input parameters

  if (buf == NULL)
      return -1;

  if (sizeof(buf) <= 0)
      return -1;
  
  // Return -1, if disk number is not between 0 and 15
  if ( (disk_num < 0) || (disk_num > (JBOD_NUM_DISKS - 1)) ) 	// JBOD_NUM_DISKS = 16
      return -1;

  // Return -1, if block number is not between 0 and 255
  if ( (block_num < 0) || (block_num > (JBOD_BLOCK_SIZE - 1)) )	// JBOD_BLOCK_SIZE = 256
      return -1;
  
  int i, j;

  // Check the Cache and appropriately Update or Insert the entry block
  for (i = 0; i < cache_size; i++) {

      // When there is a cache entry, "Update the block" identified by disk_num and block_num
      if ( (cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) ) {

          // Cache entry found !

   	  // Compare the data found in cache entry with that in buffer 'buf'
    	  if (memcmp(cache[i].block, buf, JBOD_BLOCK_SIZE) == 0) {
              // Return -1, if this same cache block data is already existing in the Cache
    	      return -1;
    	  } 
	  else {
	    
	      // "Update the block" identified by disk_num and block_num in the Cache
	      cache_update(disk_num, block_num, buf);
	      
	      return 1; // Return 1, on success
	  }
      }

      // When there is no entry identified by disk_num and block_num in the Cache,
      // then, "Insert the block" into the Cache
      if (cache[i].valid == false) {

    	  cache[i].disk_num = disk_num;
          cache[i].block_num = block_num;

          //// "Insert the block" into the Cache
          if (buf != NULL) {
              // Copy data in buffer 'buf' to the corresponding cache entry
              memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
	  }

	  clock += 1;			// Increment the global variable 'clock'
	  cache[i].access_time = clock;	// set access_time field to indicate recent use of entry
	  cache[i].valid = true;	// set 'valid' field to indicate the cache entry as valid

	  return 1; // Return 1, on success
      }

  } // end-of For Loop


  //// When the Cache is FULL
  //// Evict the 'Least Recently Used' entry, then Insert the New entry.

  int min_indx = -1;
  int indx = 0;
  
  // Fetch the 'Least Recently Used' entry
  for (j = 1; j < cache_size; j++) {
    
      if (cache[j].access_time < cache[indx].access_time) {
          min_indx = j;
          indx = min_indx;
      }
      else
          min_indx = indx;

  } // end-of For Loop

  // Insert the data from buffer 'buf' to the identified LRU entry block of the Cache
  if (min_indx >= 0) {

      cache[min_indx].disk_num = disk_num;
      cache[min_indx].block_num = block_num;

      // Copy data in buffer 'buf' to the corresponding cache entry block
      if (buf != NULL)
          memcpy(cache[min_indx].block, buf, JBOD_BLOCK_SIZE);

      clock += 1;				// Increment the global variable 'clock'
      cache[min_indx].access_time = clock;	// set access_time field to indicate recent use of entry
      cache[min_indx].valid = true;		// set 'valid' field to indicate the cache entry as valid

      // On success, return 1
      return 1;
      
  }

  // On failure, return -1
  return -1;
  
}


//// Cache UPDATE Function
//// When the entry exist in Cache, Update its block content with the new data in 'buf'
void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  //// Validate Input parameters

  if (sizeof(buf) <= 0)
      return;

  if (buf == NULL)
      return;

  // Return -1, if no Cache exist
  if ((cache == NULL) || (cache_size == 0))
      return;
	  
  int i;
	  
  // Check the Cache and appropriately Update the block entry
  for (i = 0; i < cache_size; i++) {

      // When there is a cache entry, "Update the block" identified by disk_num and block_num
      if ((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num)) {

    	  // Cache entry found !

    	  if (buf != NULL) {
              // Copy data from buffer 'buf' to the corresponding cache entry
  	      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
    	  }

	  clock += 1;			// Increment the global variable 'clock'
	  cache[i].access_time = clock;	// set access_time field to indicate recent use of entry
	  cache[i].valid = true;	// set 'valid' field to indicate the cache entry as valid

	  break;
      }

  } // end-of For Loop
	  
}


//// Cache ENABLED Function
bool cache_enabled(void) {
  // This function returns 'true' if cache is enabled and 'false' if not enabled

  // Return true, if Cache exist
  if ((cache != NULL) || (cache_size != 0))
      return true;
  
  return false; // Return false, if no Cache exist
}


// Function cache_print_hit_rate (given)
void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

