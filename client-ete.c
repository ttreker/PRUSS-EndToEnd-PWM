#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

// Struct to hold a sample
typedef struct sample_st
{
  uint32_t low;
  uint32_t high;
} sample_t;

// Prints and error message passed in
// and appends an errno string to the
// end (perror(); exits with failure.
void syserror(const char *msg)
{
  perror(msg);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
  int sockfd, portno, n, i;
  unsigned int count, numSamps;
  struct sockaddr_in serv_addr;
  struct hostent *server;
  unsigned char *buffer, *bptr;
  uint16_t readResult;

  // Check for proper number of arguments
  if (argc < 3) {
    fprintf(stderr,"\nusage %s hostname port\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // Extract the port argument and open Internet socket
  portno = atoi(argv[2]);
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    syserror("\nERROR opening socket");

  // Look up the host
  char * hoststr = argv[1];
  server = gethostbyname(hoststr);
  if (server == NULL)
  {
    close(sockfd);
    fprintf(stderr,"\nERROR, no such host: %s\n\n", argv[1]);
    exit(EXIT_FAILURE);
  }

  // Zero out the server address structure, then populate it
  // with required parameters
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
  serv_addr.sin_port = htons(portno);

  // Connect to the host
  printf("\nConnecting to host %s...\n");
  if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
  {
    close(sockfd);
    syserror("\nERROR connecting");
  }
  printf("Connection established\n\n");

  // Host sends sample count immediately upon connection, so read it.
  n = read(sockfd,&count,4);
  if (n < 0)
  {
    close(sockfd);
    syserror("\nERROR reading count from socket");
  }
  else // Got count
  {
    printf("Sample count is: 0x%X\n\n", count);
  }

  // Allocate an input buffer
  if((buffer = malloc(count)) == NULL)
  {
    close(sockfd);
    syserror("\nERROR allocating buffer");
  }

  // Tell the host to "GO"
  n = write(sockfd,"GO",strlen("GO"));
  if (n < 0) 
  {
    close(sockfd);
    free(buffer);
    syserror("\nERROR writing command \"GO\" to socket");
  }

  // Read the data in a loop, one chunk at a time, until all 
  // samples have been read.
  int cnt = 0;
  int countleft = count;
  bptr = buffer;
  printf("Reading the samples...\n");
  do
  {
    n = read(sockfd,bptr,countleft);
    printf("Bytes read: 0x%X\n", n);
    if (n < 0) 
    {
      close(sockfd);
      free(buffer);
      syserror("\nERROR reading data stream from socket");
    }
    countleft -= n;
    cnt += n;
    bptr += n;

  } while (cnt < count);
  printf("All samples read successfully\n\n");

  // Tell host all data received so it can close its socket.
  n = write(sockfd,"DONE",strlen("DONE"));
  if (n < 0) 
  {
    close(sockfd);
    free(buffer);
    syserror("\nERROR writing acknowledgment \"DONE\" to socket");
  }

  // Prepare to dump the buffer to a file by opening that file.
  FILE *outfd;
  const char * outfile = "./tcpcap.dat";
  outfd = fopen(outfile,"w");
  if(outfd == NULL)
  {
    close(sockfd);
    free(buffer);
    syserror("\nERROR opening output file tcpcap.dat");
  }

  // Dump out the samples to a formatted ascii file in hex
  int fpfret, fpftot = 0;
  sample_t *samples = (sample_t *)buffer;
  printf("Low/High sample values at buffer index 5: %X  %X\n\n", samples[5].low, samples[5].high);
  numSamps = count/sizeof(sample_t);
  printf("Writing samples to file %s...\n", outfile);
  for(i=0; i<numSamps; i++)
  {
    fpfret = fprintf(outfd, "%5d: 0x%X 0x%X; Sum=0x%X\n",i, samples[i].low, samples[i].high, samples[i].low + samples[i].high);
    if(fpfret < 0)
    {
       close(sockfd);
       free(buffer);
       fclose(outfd);
       fprintf(stderr,"\nFailed to write all data to file. %d bytes were written\n\n",fpftot);
       exit(EXIT_FAILURE);
    }
    else
       fpftot+= fpfret;
  }
  printf("All samples written. Program complete.\n\n");

  close(sockfd);
  free(buffer);
  fclose(outfd);

  return 0;
}
