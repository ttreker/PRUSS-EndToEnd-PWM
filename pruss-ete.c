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
#include <errno.h>
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

// Short function to load a single unsigned int from a sysfs entry
unsigned int readFileValue(char filename[]){
   FILE* fp;
   unsigned int value = 0;
   fp = fopen(filename, "rt");
   fscanf(fp, "%x", &value);
   fclose(fp);
   return value;
} // End readFileValue()

int dumpSamples(int newsockfd, unsigned int dataSize) {
  int fd;
  void *map_base, *virt_addr;
  unsigned long readResult, writeval;
  unsigned int addr = readFileValue(MMAP1_LOC "addr");
  unsigned int numberOutputSamples = dataSize / 2;
  off_t target = addr;
  off_t targetMemBlock = target & ~MAP_MASK;
  off_t targetMemOffset = target & MAP_MASK;


  if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
  {
    printf("Failed to open memory!");
    fflush(stdout);
    return -1;
  }

  map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, targetMemBlock);
  if(map_base == (void *) -1) 
  {
    printf("Failed to map base address");
    fflush(stdout);
    return -1;
  }

  int i=0;
  int n;

  virt_addr = map_base + targetMemOffset;
  n = write(newsockfd,virt_addr,dataSize);
  printf("Bytes written: 0x%X\n", n);

  if(munmap(map_base, MAP_SIZE) == -1)
  {
    printf("Failed to unmap memory");
    fflush(stdout);
    return -1;
  }
  close(fd);

  return 0;
  
} // End dumpSamples()

int clientDance(int newsockfd)
{
  char buffer[16];
  int n;
  unsigned int dataSize = readFileValue(MMAP1_LOC "size");

  // Write the sample size
  n = write(newsockfd,&dataSize,4);
  if (n < 0)
  {
    error("ERROR writing to socket");
    close(newsockfd);
  }
  else
  {
    bzero(buffer,16);
    n = read(newsockfd,buffer,16);
    if (n < 0)
    {
      error("ERROR reading from socket");
      close(newsockfd);
    }
    else if (!strcmp( "GO", buffer))
    {
      dumpSamples(newsockfd, dataSize);
    }
    else
    {
      if (strlen(buffer) <= 256)
      {
        printf("Return command: %s\n", buffer);
        printf("Return command length: %d\n", strlen(buffer));
        close(newsockfd);
        error("ERROR return command unrecognized");
      }
      else
      {
        close(newsockfd);
        error("Command is garbage\n");
      }
    }
  }

} // End clientDance

int doServer(int portno)
{
  int sockfd, newsockfd;
  socklen_t clilen;
  struct sockaddr_in serv_addr, cli_addr;
  int n;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) < 0) 
    error("ERROR on binding");
  listen(sockfd,5);
  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd, 
      (struct sockaddr *) &cli_addr, 
      &clilen);
  if (newsockfd < 0) 
    error("ERROR on accept");
  else // No errors so dump to client
    clientDance(newsockfd);

  close(newsockfd);
  close(sockfd);
  return 0; 
} // End doServer()


int main (int argc, char **argv)
{
   if(getuid()!=0){
      printf("You must run this program as root. Exiting.\n");
      exit(EXIT_FAILURE);
   }

  int portno;

  if(getuid()!=0)
  {
    printf("You must run this program as root. Exiting.\n");
    exit(EXIT_FAILURE);
  }

  if (argc < 2)
  {
    fprintf(stderr,"ERROR, no port provided\n");
    exit(1);
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
   printf("All Samples Dumped. Received event count %d.\n", n);
   prussdrv_pru_clear_event (PRU_EVTOUT_0, PRU0_ARM_INTERRUPT);


// Disable PRU and close memory mappings 
   prussdrv_pru_disable(PRU0);
   prussdrv_pru_disable(PRU1);

   prussdrv_exit ();

  // Now start the server; wait; dump; return here and exit
  doServer(portno);

   return EXIT_SUCCESS;
}
