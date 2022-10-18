#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

// Function declarations
uint32_t encode_operation(jbod_cmd_t cmd, int disk_num, int block_num);
void translate_address(uint32_t linear_addr, int *disk_num, int *block_num, int *offset);
void seek(int disk_num, int block_num);

// Global Variables declaration
int Mount_flag = 0; 	// Initializing to 0 as the device is initially in 'Unmounted' state

// Declaring CONSTANTS
const int MAX_SIZE = 1024; 		// Maximum size (in bytes) of 'len' to read or write
const int COMMAND_BIT_START_POS = 26;	// JBOD: Command field - start position
const int DISKID_BIT_POS = 22; 		// JBOD: Disk ID field - start position
const int BLOCKID_BIT_POS = 0;		// JBOD: Block ID field - start position


//// MOUNT Function - Mount the linear device
int mdadm_mount(void) {
  
    // This function to return 1 on Success and -1 on Failure

    // If disk is already in 'Mounted' state, then return -1
    if (Mount_flag == 1)
    	return -1;

    // Perform JBOD Mount operation
    uint32_t op = encode_operation(JBOD_MOUNT, 0, 0); // global value: JBOD_MOUNT = 0
    if (jbod_client_operation(op, NULL) == 0)
    	Mount_flag = 1; // If JBOD Mount is successful, then set global 'Mount_flag' to 1
    else
    	Mount_flag = 0;

    // Check status of 'Mount' operation
    if (Mount_flag == 1)
        return 1;		// when Mount is successful, return 1
    else
        return -1;
}

//// UNMOUNT Function - Unmount the linear device
int mdadm_unmount(void) {
  
    // This function to return 1 on Success and -1 on Failure

    // If disk is already in 'Unmounted' state, then return -1
    if (Mount_flag == 0)
    	return -1;

    // Perform JBOD Unmount operation
    uint32_t op = encode_operation(JBOD_UNMOUNT, 0, 0); // global value: JBOD_UNMOUNT = 1

    if (jbod_client_operation(op, NULL) == 0)
        Mount_flag = 0; // If JBOD Unmount is successful, then set global 'Mount_flag' to 0
    else
        Mount_flag = 1; // If err in Unmount, then let the global 'Mount_flag' remain unchanged.

    // Check status of 'Unmount' operation
    if (Mount_flag == 0)
        return 1;		// when Unmount is successful, return 1
    else
        return -1;
}

//// READ Function - Reads the block in current I/O position into the buffer
//// Read 'len' bytes into 'buf' starting at 'addr'
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  
    // Return the 'number of bytes read' on Success, and -1 on Failure

    // If the disk is in 'Unmounted' state, then return -1
    if (Mount_flag == 0)
    	return -1;

    //// Validate the Input parameters

    // Return -1; on read from an out-of-bound linear address
    // JBOD_NUM_DISKS = 16, JBOD_DISK_SIZE = 65536
    if ((addr + len) >= (JBOD_NUM_DISKS * JBOD_DISK_SIZE))
    	return -1;

    // Return -1; on read larger than the specified bytes MAX_SIZE
    if (len > MAX_SIZE)
    	return -1;

    if (len && buf == NULL)
    	return -1;

    // Declaring & initializing the local variables
    int curr_addr = addr; // current address set after every read operation
    uint32_t length = len;
    uint32_t available_block_length = 0;
    uint32_t copied_buf_length = 0;
    uint8_t tmp[JBOD_BLOCK_SIZE]; // JBOD_BLOCK_SIZE = 256
    int disk_number;
    int block_number;
    int offset;
    int read_flag = 0;	// read_flag set to 1 or 2; based on the data read from disks & blocks.
    int retval; 	// return value of cache_lookup() function.
    int returnval; 	// return value of cache_insert() function.

    // For reading bytes:- Translate linear address, Seek disk & block numbers, Read & process data.
    while (curr_addr < (addr + len)) {

        // Translate the linear address
        translate_address(curr_addr, &disk_number, &block_number, &offset);

        // Seek the respective Disk and its Block numbers
        seek(disk_number, block_number);

        // If cache exist, then read & retrieve data from Cache
        if (cache_enabled() == true)
            retval = cache_lookup(disk_number, block_number, tmp); // returns 1 on success

        // If data NOT found in cache or if NO cache exist, then read from JBOD
        if ((retval != 1) || (cache_enabled() == false))
	    jbod_client_operation(encode_operation(JBOD_READ_BLOCK, 0, 0), tmp); // JBOD_READ_BLOCK = 4

        //// Processing data:- To copy the data read; into the buffer 'buf'

        available_block_length = JBOD_BLOCK_SIZE - offset;

        // For data read from the same disk or block
        if ((len < available_block_length) && (read_flag != 2)) {

            // Copy the bytes read at tmp+offset position; into the buffer 'buf'
            memcpy(buf, tmp + offset, len);
	    
            // If block was NOT found in cache, then insert the data read; into the Cache
	    if (retval != 1)
	        returnval = cache_insert(disk_number, block_number, buf);

	    read_flag = 1; // For data read from the same disk & block, set read_flag to 1
            break;
        }

        // For data read from across blocks or disks
        if (length > available_block_length) {

            // Copy the bytes read at tmp+offset position; into appropriate location of 'buf'
            memcpy(buf + copied_buf_length, tmp + offset, available_block_length);
	    
            // If block was NOT found in cache, then insert the data read; into the Cache
	    if (retval != 1)
	        returnval = cache_insert(disk_number, block_number, buf);

            copied_buf_length += available_block_length;
            length -= available_block_length; 	// length; pending to be copied into 'buf'

            read_flag = 2; // For data read across blocks or disks, set read_flag to 2
        }
	
        // For data that is read in the end
        else if ((length <= available_block_length) && (read_flag > 1)) {

            // Copy the bytes read at tmp+offset position; into appropriate location of 'buf'
            memcpy(buf + copied_buf_length, tmp + offset, length);

            // If block was NOT found in cache, then insert the data read; into the Cache
	    if (retval != 1)
	        returnval = cache_insert(disk_number, block_number, buf);

            copied_buf_length += length;
            length -= copied_buf_length;	// length; pending to be copied into 'buf'

            read_flag = 0;
        }

        // Compute the next address location to Continue with reading data
        curr_addr += available_block_length;
	
    } // end-of while loop

    // On success, return the 'number of bytes'
    return len;
}


//// HELPER Functions ////

// Helper function-1: encode_operation()
uint32_t encode_operation(jbod_cmd_t cmd, int disk_num, int block_num) {

    // Construct 'op' value.
    // For example:
    // when cmd = 0 : '0000 00-00 000-0 0000 0000 0000 0-000 0000' (initial mount)
    // when cmd = 1 : '0000 01-00 000-0 0000 0000 0000 0-000 0000' (initial unmount)
    // say, op = (cmd << 26 | disk_num << 22 | block_num << 0);

    uint32_t op;
    op = (cmd << COMMAND_BIT_START_POS | disk_num << DISKID_BIT_POS | block_num << BLOCKID_BIT_POS);

    return op;
}

// Helper function-2: translate_address()
void translate_address(uint32_t curr_addr, int *disk_number, int *block_number, int *offset) {

    // Translate linear address to appropriate disk & block numbers, and offset position.
    int disk_offset;
    disk_offset = curr_addr % JBOD_DISK_SIZE;

    *disk_number = curr_addr / JBOD_DISK_SIZE;	    // JBOD_DISK_SIZE = 65536
    *block_number = disk_offset / JBOD_BLOCK_SIZE;  // JBOD_BLOCK_SIZE = 256
    *offset = disk_offset % JBOD_BLOCK_SIZE;
}

// Helper function-3: seek()
void seek(int disk_number, int block_number) {

    // Seek to a specific disk. JBOD_SEEK_TO_DISK = 2
    jbod_client_operation(encode_operation(JBOD_SEEK_TO_DISK, disk_number, 0), NULL);

    // Seek to a specific block in current disk. JBOD_SEEK_TO_BLOCK = 3
    jbod_client_operation(encode_operation(JBOD_SEEK_TO_BLOCK, 0, block_number), NULL);
}


//// Write function - Writes the data from buffer into the block in current I/O position
//// Writes 'len' bytes from the buffer 'buf' to the storage system, starting at address 'addr'
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {

    // Return the 'number of bytes read' on Success, and -1 on Failure

    // If the disk is in 'Unmounted' state, then return -1
    if (Mount_flag == 0)
    	return -1;

    //// Validate the input parameters

    // Return -1; on attempting Write to an out-of-bound linear address.
    // JBOD_NUM_DISKS = 16, JBOD_DISK_SIZE = 65536
    if ((addr + len) > (JBOD_NUM_DISKS * JBOD_DISK_SIZE))
    	return -1;

    // Return -1; on read larger than the specified bytes MAX_SIZE
    if (len > MAX_SIZE)
    	return -1;

    if (len && buf == NULL)
    	return -1;

    // Declaring & initializing the local variables
    int curr_addr = addr;
    uint32_t length = len;
    uint32_t available_block_length = 0;
    uint32_t copied_buf_length = 0;
    uint8_t tmp[JBOD_BLOCK_SIZE]; // JBOD_BLOCK_SIZE = 256
    int disk_number;
    int block_number;
    int offset;
    int flag = 0;   // flag set to 1 or 2; based on data to be written to disks & blocks
    int returnval;  // return value of cache_insert() function.
    
    //// For Writing bytes:- Translate linear address, Seek disk & block numbers,
    //// then, Fetch the data block and process to Write the data

    // Translate the linear address
    translate_address(curr_addr, &disk_number, &block_number, &offset);

    // Seek the respective disk and block numbers
    seek(disk_number, block_number);

    // Read JBOD. JBOD_READ_BLOCK = 4
    jbod_client_operation(encode_operation(JBOD_READ_BLOCK, 0, 0), tmp);

    while (curr_addr < addr + len) {

        //// Processing data:- to Write data from const buffer 'buf' into JBOD and Cache

    	available_block_length = JBOD_BLOCK_SIZE - offset;

        // For data to be written in the same disk or block
        if ((len <= available_block_length) && (flag == 0)) {

            // Copy the bytes from const buffer 'buf'; into appropriate location of 'tmp'
            memcpy(tmp + offset, buf, len);

            // Seek the disk & block number, to Write data from 'tmp' into JBOD
            seek(disk_number, block_number);
            jbod_client_operation(encode_operation(JBOD_WRITE_BLOCK, 0, 0), tmp); // JBOD_WRITE_BLOCK = 5
	    
            // If there exist any cache, then write / Insert data into the Cache from 'tmp'
	    if (cache_enabled() == true)
		returnval = cache_insert(disk_number, block_number, tmp);

            flag = 1; 	// when data is written into the same disk and block, set flag to 1
            break;
        }

        // For data to be written across blocks or disks
        if (length > available_block_length) {

            // Copy the bytes at buf+copied_buf_length position; into appropriate location of 'tmp'
            memcpy(tmp + offset, buf + copied_buf_length, available_block_length);

            // Seek the disk & block number, to Write data from 'tmp' into JBOD
            seek(disk_number, block_number);
            jbod_client_operation(encode_operation(JBOD_WRITE_BLOCK, 0, 0), tmp);
	    
            // If there exist any cache, then write / Insert data into the Cache from 'tmp'
	    if (cache_enabled() == true)
	        returnval = cache_insert(disk_number, block_number, tmp);

            copied_buf_length += available_block_length;
            length -= available_block_length;	// length; pending to be copied

            flag = 2;	// when data is written across blocks or disks, set flag to 2
        }
	
        // For data to be written in the end
        else if ((length > 0) && (length <= available_block_length) && (flag == 2)) {

            // Copy the bytes at buf+copied_buf_length position; into appropriate location of 'tmp'
            memcpy(tmp + offset, buf + copied_buf_length, length);

            // Seek the disk & block number, to Write data from 'tmp' into JBOD
            seek(disk_number, block_number);
            jbod_client_operation(encode_operation(JBOD_WRITE_BLOCK, 0, 0), tmp);
	    
            // If there exist any cache, then write / Insert data into the Cache from 'tmp'
	    if (cache_enabled() == true)
	        returnval = cache_insert(disk_number, block_number, tmp);

            copied_buf_length += available_block_length;
            length -= available_block_length;	// length; pending to be copied

            flag = -1;
            break;
        }

        // Compute the next address location to Continue with writing data
        curr_addr += available_block_length;

        // Translate this curr_address
        translate_address(curr_addr, &disk_number, &block_number, &offset);

        // Seek the respective Disk and its Block
        seek(disk_number, block_number);

        // Read JBOD
        jbod_client_operation(encode_operation(JBOD_READ_BLOCK, 0, 0), tmp);

    } // end-of while loop

    // On success, return the 'number of bytes'
    return len;
}

/* End-of Program */
