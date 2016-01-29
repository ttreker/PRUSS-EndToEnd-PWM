/*  This program loads the two PRU programs into the PRU-ICSS transfers the configuration
*   to the PRU memory spaces and starts the execution of both PRU programs.
*   pressed. By Derek Molloy, for the book Exploring BeagleBone. Please see:
*        www.exploringbeaglebone.com/chapter13
*   for a full description of this code example and the associated programs.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <prussdrv.h>
#include <pruss_intc_mapping.h>

#define MAP_SIZE 0x10000000
#define MAP_MASK (MAP_SIZE - 1)
#define MMAP1_LOC   "/sys/class/uio/uio0/maps/map1/"
#define PRU0 0
#define PRU1 1

//
// This routine is used to report system errors and then abort (i.e. errors
// that set errno).
//
void syserror(const char *msg)
{
  perror(msg);
  exit(EXIT_FAILURE);
}

// Short function to load a single unsigned int from a sysfs entry
unsigned int readFileValue(char filename[]){
   FILE* fp;
   unsigned int value = 0;
   fp = fopen(filename, "rt");
   if(fp == NULL) // Indicate the open failure by returning 0
      return 0;
   fscanf(fp, "%x", &value);
   fclose(fp);
   return value;
} // End readFileValue()

//
// This routine dumps the samples to the connected client. It returns -1 on
// any errors and 0 on success. It reports errors to stderr, but leaves
// any abort to the calling program.
//
int dumpSamples(int newsockfd, unsigned int dataSize) {
  int fd;
  void *map_base, *virt_addr;
  unsigned long readResult, writeval;
  unsigned int addr;
  off_t target;
  off_t targetMemBlock;
  off_t targetMemOffset;

  // Get the address of the uio_pruss extended RAM buffer
  // Mask it off for a memory block start address physical address in /dev/mem
  // and an offset value referencing the start of the buffer.
  addr = readFileValue(MMAP1_LOC "addr");
  if(addr == 0)
  {
    fprintf(stderr, "\nERROR failed to read buffer address\n");
    fflush(stderr);
    return -1;
  }
  target = addr;
  targetMemBlock = target & ~MAP_MASK;
  targetMemOffset = target & MAP_MASK;

  // Open /dev/mem so it can be mapped
  if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
  {
    fprintf(stderr, "\nFailed to open /dev/mem!\n");
    fflush(stderr);
    return -1;
  }

  // Map the physical memory block location to a local process pointer
  map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, targetMemBlock);
  if(map_base == (void *) -1) 
  {
    fprintf(stderr,"\nFailed to map base address of uio_pruss extended RAM buffer\n");
    fflush(stderr);
    close(fd);
    return -1;
  }

  // Setup a pointer to point to the buffer start location then use it to write
  // the data to the socket.
  int i=0;
  int n;
  virt_addr = map_base + targetMemOffset;
  printf("Sample  5 (low high): 5x%X 5x%X\n\n", ((uint32_t *)virt_addr)[10], ((uint32_t *)virt_addr)[11]);
  n = write(newsockfd,virt_addr,dataSize);
  if(n == -1)
  {
    fprintf(stderr, "\nERROR data dump socket write failed\n");
    fflush(stderr);
    close(fd);
    return -1;
  }
  printf("Bytes written: 0x%X\n", n);

  // Done writing all data so free up the pointer
  if(munmap(map_base, MAP_SIZE) == -1)
  {
    fprintf(stderr,"\nFailed to unmap memory\n");
    fflush(stderr);
    close(fd);
    return -1;
  }
  close(fd);

  return 0;

} // End dumpSamples()

//
// This routine engages in the handshaking with the server that results in the
// data being dumped. It returns -1 on failure and 0 on success. It reports
// errors to standard error, but leaves any abort to the calling routine.
int clientDance(int newsockfd)
{
  char buffer[16];
  int n, dsret;
  unsigned int dataSize;

  // Read the uio_pruss extended RAM buffer size value
  dataSize = readFileValue(MMAP1_LOC "size");
  if(dataSize == 0)
  {
     fprintf(stderr, "\nERROR reading buffer size\n");
     return -1;
  }

  // Send the  sample size to the client immediately upon connection.
  n = write(newsockfd,&dataSize,4);
  if (n < 0)
  {
    fprintf(stderr, "\nERROR writing to socket\n");
    return -1;
  }
  else // Wait for the "GO" command
  {
    bzero(buffer,16);
    n = read(newsockfd,buffer,16);
    if (n < 0)
    {
      fprintf(stderr, "\nERROR reading \"GO\" command from socket (from client)\n");
      return -1;
    }
    else if (!strcmp( "GO", buffer)) // If we got "GO" then dump samples
    {
      dsret = dumpSamples(newsockfd, dataSize);
    }
    else
    {
      fprintf(stderr, "\nERROR no \"GO\" command; just garbage\n");
      return -1;
    }
  }

  // If we got this far, then we called dumpSamples() so check its return
  // value. If it  was not 0 then return with an error
  if(dsret)
  {
    fprintf(stderr, "\nERROR dumping samples failed\n");
    return -1;
  }

  // dumpSamples() succeeded so wait for a "DONE" command
  bzero(buffer,16);
  n = read(newsockfd,buffer,16);
  if (n < 0)
  {
    fprintf(stderr, "\nERROR reading \"DONE\" command from socket (from client)\n");
    return -1;
  }
  else if (!strcmp( "DONE", buffer)) // If got "DONE", our client dance is done
  {
    return 0;
  }
  else
  {
    fprintf(stderr, "\nERROR no \"DONE\" command; just garbage\n");
    return -1;
  }

} // End clientDance

//
// This function starts the server:
//    - The only way that it will return is if there are no errors along the
//      way. Any errors trigger a program abort, circumventing a return.
//    - If there are no errors starting the server, and a valid connection is
//      recieved, it goes on to start handshaking with the client and dumping
//      the data.
//
// This function owns two file descriptors and is responsible for their closure.
//
void doServer(int portno)
{
  int sockfd, newsockfd;
  socklen_t clilen;
  struct sockaddr_in serv_addr, cli_addr;
  int n;

  // Get a handle on a socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    syserror("\nERROR opening socket");

  // Initialize the server structure that specifies, amongy other things,
  // the port number and the interface (which is any in this case).
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  // Prevent TIME_WAIT and ensure immediate port reuse after termination
  // Without this call the port will not be available for reuse until
  // after a TIME_WAIT period, which is often several minutes.
  int on = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ))
  {
    close(sockfd);
    syserror("\nERROR on setting socket option");
  }

  // Bind the socket to the port/interfaces
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) == -1)
  {
    close(sockfd);
    syserror("\nERROR on socket binding");
  }

  // Start the server listening for connections
  if(listen(sockfd,5) == -1)
  {
    close(sockfd);
    syserror("\nERROR on socket listen");
  }

  // Accept an incoming connection
  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
  if (newsockfd == -1)
  {
     close(sockfd);
     syserror("\nERROR on connection accept");
  }
  else // No errors so dance with client to dump data
  {
    int cdret = clientDance(newsockfd);
    close(sockfd);     // Close the sockets
    close(newsockfd);  // good or bad
    if(cdret)
    {
      syserror("\nERROR during clientDance()");
    }
  }

} // End doServer()


int main (int argc, char **argv)
{
  int portno;

  // Check for root
  if(getuid()!=0)
  {
   fprintf(stderr,"You must run this program as root. Exiting!\n");
   abort();
  }

  // Check for port
  if (argc < 2)
  {
    fprintf(stderr,"\nERROR no port provided\n");
    abort();
  }

  // Get the port from the command line
  portno = atoi(argv[1]);

  // Initialize structure used by prussdrv_pruintc_intc
  // PRUSS_INTC_INITDATA is found in pruss_intc_mapping.h
  tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

  // Data for shared memory. Both PRUs need access. 
  uint32_t  uioBufMetaD[2];
  uioBufMetaD[0] = readFileValue(MMAP1_LOC "addr");
  uint32_t bufSize = readFileValue(MMAP1_LOC "size");
  uioBufMetaD[1] = bufSize/8; // Two 32 bit words make up a sample
  printf("\nThe DDR External Memory buffer is at physical location: 0x%x\n", uioBufMetaD[0]);
  printf("The DDR External Memory buffer has a size of:           0x%x (%d) bytes\n", bufSize, bufSize);
  printf("This space has capacity to store upto:                  0x%x (%d) samples\n\n", uioBufMetaD[1], uioBufMetaD[1]);

  // Allocate and initialize memory
  prussdrv_init ();              // Routine only zeros out an internal data structure
  prussdrv_open (PRU_EVTOUT_0);  // Populates the internal structure with pointers and params

  // Write uio_pruss kernel buffer metadata to PRUSS shared memory
  prussdrv_pru_write_memory(PRUSS0_SHARED_DATARAM, 0, uioBufMetaD, 8);  

  // Map the PRU's interrupts
  prussdrv_pruintc_init(&pruss_intc_initdata);

  // Load and execute the PRU programs on each PRU
  prussdrv_exec_program (PRU1, "./ddr-pru1.bin");  // PRU1 must be loaded first
  prussdrv_exec_program (PRU0, "./data-pru0.bin");
  printf("PRU0 and PRU1 are now running...\n");

  // Wait for event completion from PRUSS:
  // PRU1 signals PRU_EVTOUT_0 (interrupt)
  // PRU0 will just halt.
  int n = prussdrv_pru_wait_event (PRU_EVTOUT_0);
  printf("All samples written to DDR buffer. (UIO interrupt count: %d).\n\n", n);
  prussdrv_pru_clear_event (PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);

  // Disable PRU and close memory mappings 
  prussdrv_pru_disable(PRU0);
  prussdrv_pru_disable(PRU1);

  prussdrv_exit ();

  // Now start the server; wait for client; dump data; return here and exit
  printf("Starting the server...\n\n");
  doServer(portno);
  printf("Data successfully dumped. Program complete.\n\n");

  return EXIT_SUCCESS;
}
