/*
 * (C) Copyright 2010
 * Steve Chang
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

#include "rtph264.h"

void sig_handler(int s)
{
  switch(s) {
    case SIGINT:  RtpH264_Stop();
      break;
  }
}

#include <linux/if.h>
#include <sys/ioctl.h>

static in_addr_t ifconfig(char *device)
{
  int s;
  struct ifreq req;
  struct sockaddr_in *addrp;

  if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0){
    //dtrace("fail to open socket\n");
    return 0;
  }

  addrp = (struct sockaddr_in*) &(req.ifr_addr);
  addrp->sin_family = AF_INET;
  strcpy(req.ifr_name,device);
  if(ioctl(s,SIOCGIFADDR,&req)){
    //printf("fail to get inet address\n");
    goto error;
  }
  return addrp->sin_addr.s_addr;

error:
  //printf("errno = %d\n",errno);
  close(s);
  return 0;
}

typedef enum
{
   ArgID_HELP,
   ArgID_IP,
   ArgID_PORT,
   ArgID_DEVICE,
//   ArgID_FILE
} ArgID;

#define STR32 32
typedef struct Args 
{
  in_addr_t ip;
  unsigned short port;
  char device[STR32];
} Args;

#define DEFAULT_ARGS { 0, 8000, "eth0"}

static void Usage(void)
{
    fprintf(stderr, "Usage: scv [options]\n\n"
        "Options:\n"
        "-h | --help           Print usage information (this message)\n"
        "-i | --ip             Binding ip\n"
        "-p | --port           Listen port : default 8000\n"
        "-d | --device         Device\n"
        "At a minimum the IP and port *must* be given\n\n");
}

#include <getopt.h>

static void ParseArgs(int argc, char *argv[], Args *argsp)
{
  const char shortOptions[] = "hi:p:d:";

  const struct option longOptions[] = {
    {"help",      no_argument,       NULL, ArgID_HELP },
    {"ip",        required_argument, NULL, ArgID_IP },
    {"port",      required_argument, NULL, ArgID_PORT  },
    {"device",    required_argument, NULL, ArgID_DEVICE },
    {0, 0, 0, 0}
  };

  int index;
  int argID;

  for(;;) {
    argID = getopt_long(argc, argv, shortOptions, longOptions, &index);

    if(argID == -1)
      break;
    
    switch(argID) {
      case ArgID_IP:
      case 'i':
        argsp->ip = inet_addr(optarg);
        break;
      case ArgID_PORT:
      case 'p':
        if(sscanf(optarg, "%hu", &argsp->port) == -1)
          argsp->port = 8000;        
        break;
      case ArgID_DEVICE:
      case 'd':
        snprintf(argsp->device, STR32, "%s", optarg);
        if((argsp->ip = ifconfig(optarg)) == 0)  {
          printf("Error fetch %s IP...\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      case ArgID_HELP:
      case 'h':
      default:
        printf("%s:%d\n", __FUNCTION__, __LINE__);
        Usage();
        exit(EXIT_SUCCESS);
    }
  }

  if(optind < argc) {
    Usage();
    exit(EXIT_FAILURE);
  }  
}

int CreateUdpSocket(in_addr_t ip, unsigned short port)
{
  struct sockaddr_in addr;
  int sfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(sfd == -1)  {
    printf("Could not create a UDP socket\n");
    goto udp_socket_fail;
  }

  memset((char*) &(addr),0, sizeof((addr)));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  addr.sin_addr.s_addr = ip;
  printf("Binding to interface %s\n", inet_ntoa(addr.sin_addr));

  if(bind(sfd,(struct sockaddr*)&addr, sizeof(addr)) != 0)  {
    printf("Could not bind socket\n");
    goto udp_socket_fail;
  }

  return sfd;
  
udp_socket_fail:
  return -1;
}

void OnPicture(unsigned char *data, int lineSize, int width, int height)
{
//  printf("[ %dx%d ] : lineSize = %d\n", width, height, lineSize);
}

int main(int argc, char **argv)
{
  Args args = DEFAULT_ARGS;

  /* Parse the arguments given to the app */
  ParseArgs(argc, argv, &args);
    
  int sfd = CreateUdpSocket(args.ip, args.port);
  if(!sfd)  {
    fprintf(stderr, "could not open socket\n");
    exit(EXIT_FAILURE);
  }
  
  signal(SIGINT, sig_handler);
  
  RtpH264_Init();
  
  RtpH264_Run(sfd, OnPicture);
  
  RtpH264_Deinit();
  
  return 0;
}