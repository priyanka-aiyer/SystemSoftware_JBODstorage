#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* Implementing  Client component to connect to JBOD server & Execute JBOD operations over network */ 


// Global Variables declaration
int cli_sd = -1; // Client socket descriptor for the connection to the server


// Function nread() - attempts to read n bytes from fd;
// returns true on success and false on failure 
static bool nread(int fd, int len, uint8_t *buf) {

	int retval;
	int num_read = 0;

	// To read 'len' number of bytes from socket handle, into the buffer  
	while (num_read < len) {

	  retval = read(fd, &buf[num_read], len - num_read );

	  // On read error, return false
	  if (retval < 0)
	      return false;

	  // On end-of read
	  if (retval == 0)   
	      break;

	  // When all bytes are read
	  if (retval == len) 
	      return true;
	      
	  num_read += retval;
	}

	// On success, return true
	return true;

}

// Function nwrite() - attempts to write n bytes to fd;
// returns true on success and false on failure 
static bool nwrite(int fd, int len, uint8_t *buf) {

	int returnval;
	int num_write = 0;

	// To write bytes of length 'len' to socket handle, from buffer; received from send_packet()  
	while (num_write < len) {

	  returnval = write(fd, &buf[num_write], (len - num_write) );

	  // On write error, return false
	  if (returnval < 0)
             return false;

	  num_write += returnval;
	}

	// On success, return true	
	return true; 
}

// Function recv_packet() - attempts to receive a packet from fd;
// returns true on success and false on failure
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {

	uint16_t len ;
	int length = HEADER_LEN;
	
	uint8_t header[HEADER_LEN];           // array size is 8
	uint8_t new_header[JBOD_BLOCK_SIZE];  // array size is 256

	// Read bytes from socket handle into buffer 'header'
	if (nread(fd, length, header) == false)
	    return false;

	// If read is successful, fetch the header data i.e. 8 bytes
	int offset = 0;
	memcpy(&len, &header[offset], sizeof(len));
	offset += sizeof(len);
	memcpy(op, &header[offset], sizeof(*op));
	offset += sizeof(*op);
	memcpy(ret, &header[offset], sizeof(*ret));
	offset += sizeof(*ret);

	// Convert header info from network bytes to host data
	len = ntohs(len);
	*op = ntohl(*op);
	*ret = ntohs(*ret);

	// If length field of 'header' fetched is more than HEADER_LEN
	if (len > HEADER_LEN) {
	    int new_length = JBOD_BLOCK_SIZE ;
	    
	    // Continue to read bytes from socket handle into buffer 'new_header' i.e. 256 bytes
  	    if (nread(fd, new_length, new_header) == false)
	        return false;

	    // If reading block is successful, then fetch the data directly to 'block'
	    for (int j = 0; j < JBOD_BLOCK_SIZE; j++) {
		 memset(block+j, new_header[j], 1);
	    }

            offset += sizeof(*block);	    
	}
	    
	// On success, return true
	return true;

}

// Function send_packet() - attempts to send a packet to sd; 
// returns true on success and false on failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

	int offset = 0;
	uint16_t len, length;
	uint16_t ret = 0;

	uint8_t header[HEADER_LEN + JBOD_BLOCK_SIZE]; // array size is 264

	// Check if there exist any data in the block
	if (block == NULL)
	    length = HEADER_LEN;
	else
	    length = HEADER_LEN + JBOD_BLOCK_SIZE;

	// Convert header info i.e. 8 bytes, from host to network bytes
	len = htons(length);
	op = htonl(op);
	ret = htons(ret);

	// Construct buffer 'header' struct data
	memcpy(&header[offset], &len, sizeof(len));
	offset += sizeof(len);
	memcpy(&header[offset], &op, sizeof(op));
	offset += sizeof(op);
	memcpy(&header[offset], &ret, sizeof(ret));
	offset += sizeof(ret);

	// If the block has data, then include this data in buffer 'header'
	if (block != NULL) {

	    for (int j = 0; j < JBOD_BLOCK_SIZE; j++) {
	         memset(&header[offset], block[j], 1);
	         offset += 1;
	    }

	} // end-of-IF

	
	// Write bytes to socket handle from the buffer 'header'
	if (nwrite(sd, length, header) == false)
	    return false;
  
	// On success, return true
	return true;

}

// Function jbod_connect() - attempts to connect to server and set the global cli_sd variable to socket;
// returns true if successful and false if not
bool jbod_connect(const char *ip, uint16_t port) {

	struct sockaddr_in caddr;

	caddr.sin_family = AF_INET;
	caddr.sin_port = htons(JBOD_PORT);

	if (inet_aton(JBOD_SERVER, &caddr.sin_addr) == 0 ) {
	    return false;
	}

	cli_sd = socket(PF_INET , SOCK_STREAM, 0 );
	if (cli_sd == -1) {
	    printf("Error in socket creation [%s]\n", strerror(errno));
	    return false;
	}

	if (connect(cli_sd, (const struct sockaddr *) &caddr, sizeof(caddr) ) == -1 ) {
	    printf("Error in socket connect [%s]\n", strerror(errno));
	    return false;
	}
	
	//printf("Connected to the JBOD server\n");
	
	// On success, return 0
	return true;

}

 
// Function jbod_disconnect() - disconnects from the JBOD server, and resets cli_sd
void jbod_disconnect(void) {

	close(cli_sd);
	cli_sd = -1;
	
	//printf("Closed connection to the JBOD server\n");
}


// Function jbod_client_operation() - sends the JBOD operation to the server, and
// receives and processes the response
int jbod_client_operation(uint32_t op, uint8_t *block) {

        uint16_t ret;

	// Send JBOD Operation to Server; for writing the data packet
	if (send_packet(cli_sd, op, block) == false)
	    return -1;	
	

        // Receive from Server the data packet; as response to the sent JBOD Operation 	
	if (recv_packet(cli_sd, &op, &ret, block) == false)
	    return -1;

	
	// On success, return 0
	return 0;
}
