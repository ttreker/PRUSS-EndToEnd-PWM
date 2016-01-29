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
void error(const char *msg)
{
  perror(msg);
  exit(0);
}

// Short function to load a single unsigned int from a sysfs entry
unsigned int readFileValue(char filename[]){
   FILE* fp;
   unsigned int value = 0;
   fp = fopen(filename, "rt");
   if(fp == NULL)
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
  unsigned int numberOutputSamples = dataSize / 2;
  off_t target;
  off_t targetMemBlock;
  off_t targetMemOffset;

  addr = readFileValue(MMAP1_LOC "addr");
  if(addr == 0)
  {
    fprintf(stderr, "ERROR failed to read buffer address\n");
    fflush(stderr);
    return -1;
  }
  target = addr;
  targetMemBlock = target & ~MAP_MASK;
  targetMemOffset = target & MAP_MASK;

  if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
  {
    fprintf(stderr, "Failed to open /dev/mem!\n");
    fflush(stderr);
    return -1;
  }

  map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, targetMemBlock);
  if(map_base == (void *) -1) 
  {
    fprintf(stderr,"Failed to map base address of uio_pruss extended RAM buffer\n");
    fflush(stderr);
    close(fd);
    return -1;
  }

  int i=0;
  int n;

  virt_addr = map_base + targetMemOffset;
  n = write(newsockfd,virt_addr,dataSize);
  if(n == -1)
  {
    fprintf(stderr, "ERROR data dump socket write failed\n");
    fflush(stderr);
    close(fd);
    return -1;
  }
  printf("Bytes written: 0x%X\n", n);

  if(munmap(map_base, MAP_SIZE) == -1)
  {
    fprintf(stderr,"Failed to unmap memory");
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
     fprintf(stderr, "ERROR reading buffer size");
     return -1;
  }

  // Send the  sample size to the client immediately upon connection.
  n = write(newsockfd,&dataSize,4);
  if (n < 0)
  {
    fprintf(stderr, "ERROR writing to socket");
    return -1;
  }
  else // Wait for the "GO" command
  {
    bzero(buffer,16);
    n = read(newsockfd,buffer,16);
    if (n < 0)
    {
      fprintf(stderr, "ERROR reading \"GO\" command from socket (from client)");
      return -1;
    }
    else if (!strcmp( "GO", buffer)) // If we got "GO" then dump samples
    {
      dsret = dumpSamples(newsockfd, dataSize);
    }
    else
    {
      fprintf(stderr, "ERROR no \"GO\" command; just garbage\n");
      return -1;
    }
  }

  // If we got this far, then we called dumpSamples() so check its return
  // value. If it  was not 0 then return with an error
  if(dsret)
  {
    fprintf(stderr, "ERROR dumping samples failed\n");
    return -1;
  }

  // dumpSamples() succeeded so wait for a "DONE" command
  bzero(buffer,16);
  n = read(newsockfd,buffer,16);
  if (n < 0)
  {
    fprintf(stderr, "ERROR reading \"DONE\" command from socket (from client)");
    return -1;
  }
  else if (!strcmp( "DONE", buffer)) // If got "DONE", our client dance is done
  {
    return 0;
  }
  else
  {
    fprintf(stderr, "ERROR no \"DONE\" command; just garbage\n");
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
    error("ERROR opening socket");

  // Initialize the server structure that specifies, amongy other things,
  // the port number.
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  // Prevent TIME_WAIT and ensure immediate port reuse after termination
  int on = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ))
  {
    close(sockfd);
    error("ERROR on setting socket option");
  }
  // Bind the socket to the port
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) == -1)
  {
    close(sockfd);
    error("ERROR on socket binding");
  }

  // Start the server listening for connections
  if(listen(sockfd,5) == -1)
  {
    close(sockfd);
    error("ERROR on socket listen:");
  }

  // Accept an incoming connection
  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
  if (newsockfd == -1)
  {
     close(sockfd);
     error("ERROR on connection accept");
  }
  else // No errors so dance with client to dump data
  {
    int cdret = clientDance(newsockfd);
    close(sockfd);
    close(newsockfd);
    if(cdret)
    {
      error("ERROR during clientDance()");
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
    fprintf(stderr,"ERROR no port provided\n");
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
  printf("The DDR External Memory pool has location: 0x%x and size: 0x%x bytes\n", uioBufMetaD[0], bufSize);
  uioBufMetaD[1] = bufSize/8;
  printf("-> this space has capacity to store upto %d (%x) samples\n", uioBufMetaD[1], uioBufMetaD[1]);

  // Allocate and initialize memory
  prussdrv_init ();
  prussdrv_open (PRU_EVTOUT_0);

  // Write uio_pruss kernel buffer metadata to PRUSS shared memory
  prussdrv_pru_write_memory(PRUSS0_SHARED_DATARAM, 0, uioBufMetaD, 8);  

  // Map the PRU's interrupts
  prussdrv_pruintc_init(&pruss_intc_initdata);

  // Load and execute the PRU program on the PRU
  prussdrv_exec_program (PRU1, "./ddr-pru1.bin");
  prussdrv_exec_program (PRU0, "./data-pru0.bin");
  printf("PRU0 and PRU1 are now running\n");

  // Wait for event completion from PRU, returns the PRU_EVTOUT_0 number
  int n = prussdrv_pru_wait_event (PRU_EVTOUT_0);
  printf("All Samples Dumped. Received event count %d.\n\n", n);
  prussdrv_pru_clear_event (PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);


  // Disable PRU and close memory mappings 
  prussdrv_pru_disable(PRU0);
  prussdrv_pru_disable(PRU1);

  prussdrv_exit ();

  // Now start the server; wait; dump; return here and exit
  printf("Starting the server...\n");
  doServer(portno);
  printf("Data successfully dumped. Program ending.\n\n");

  return EXIT_SUCCESS;
}
