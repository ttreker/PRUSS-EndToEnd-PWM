#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

typedef struct sample_st
{
  uint32_t low;
  uint32_t high;
} sample_t;

void error(const char *msg)
{
  perror(msg);
  exit(1);
}

int main(int argc, char *argv[])
{
  int sockfd, portno, n, i;
  unsigned int count, numSamps;
  struct sockaddr_in serv_addr;
  struct hostent *server;
  unsigned char *buffer, *bptr;
  sample_t *samples;
  uint16_t readResult;

  if (argc < 3) {
    fprintf(stderr,"usage %s hostname port\n", argv[0]);
    exit(1);
  }

  portno = atoi(argv[2]);
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");
  server = gethostbyname(argv[1]);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host: %s\n\n", argv[1]);
    exit(1);
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, 
      (char *)&serv_addr.sin_addr.s_addr,
      server->h_length);
  serv_addr.sin_port = htons(portno);
  if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
    error("ERROR connecting");

  n = read(sockfd,&count,4);
  if (n < 0)
  {
    close(sockfd);
    error("ERROR reading count from socket");
  }
  else
  {
    printf("\n");
    printf("Count is: 0x%X\n", count);
  }

  if((buffer = malloc(count)) == NULL)
  {
    close(sockfd);
    error("ERROR allocating buffer");
  }

  n = write(sockfd,"GO",strlen("GO"));
  if (n < 0) 
  {
    close(sockfd);
    free(buffer);
    error("ERROR writing command \"GO\" to socket");
  }

  int cnt = 0;
  bptr = buffer;
  do
  {
    n = read(sockfd,bptr,count);
    printf("Bytes read: 0x%X\n", n);
    if (n < 0) 
    {
      close(sockfd);
      free(buffer);
      error("ERROR reading data stream from socket");
    }
    cnt += n;
    bptr += n;

  } while (n > 0 && cnt < count);

  n = write(sockfd,"DONE",strlen("DONE"));
  if (n < 0) 
  {
    close(sockfd);
    free(buffer);
    error("ERROR writing acknowledgment \"DONE\" to socket");
  }

  FILE *outfd;
  outfd = fopen("./tcpcap.dat","w");
  if(outfd == NULL)
  {
    close(sockfd);
    free(buffer);
    error("ERROR opening output file tcpcap.dat");
  }

  int fpfret, fpftot = 0;
  samples = (sample_t *)buffer;
  printf("\nSample buffer index 5: %X  %X\n\n", samples[5].low, samples[5].high);
  numSamps = count/sizeof(sample_t);
  for(i=0; i<numSamps; i++)
  {
    fpfret = fprintf(outfd, "%5d: 0x%X 0x%X\n",i, samples[i].low, samples[i].high);
    if(fpfret < 0)
    {
       close(sockfd);
       free(buffer);
       fclose(outfd);
       fprintf(stderr,"Failed to write all data to file. %d bytes were written\n\n",fpftot);
    }
    else
       fpftot+= fpfret;
  }

  close(sockfd);
  free(buffer);
  fclose(outfd);

  return 0;
}
