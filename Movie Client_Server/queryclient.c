#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "includes/queryprotocol.h"

int createAndConnectSocket(struct addrinfo *star) {
  int socketId = socket(star->ai_family, star->ai_socktype, star->ai_protocol);
  if (socketId < 0) {
    perror("issue creating socketId in RunQuery\n");
    exit(1);
  }
  int connection = connect(socketId, star->ai_addr, star->ai_addrlen);
  if (connection == -1) {
    perror("error with the connection from client to server\n");
    exit(1);
  }
  return socketId;
}

struct addrinfo *createAddrInfo(char *ipAddress, char *port) {
  struct addrinfo res, *star;
  memset(&res, 0, sizeof res);
  res.ai_family = AF_UNSPEC;
  res.ai_socktype = SOCK_STREAM;
  res.ai_flags = AI_PASSIVE;
  int address = getaddrinfo(ipAddress, port, &res, &star);
  if (address < 0) {
    perror("issue creating addrinfo struct in RunQuery\n");
    exit(1);
  }
  return star;
}



void RunQuery(char *query, char *ipAddress, char *port) {
  struct addrinfo *star = createAddrInfo(ipAddress, port);
  int socketId = createAndConnectSocket(star);
  char resp[1000];
  int len = read(socketId, resp, 999);
  resp[len] = '\0';
  if (len == -1 || strlen(resp) != len) {
    perror("erro here!");
    exit(1);
  }
  if (CheckAck(resp) == -1) {
    perror("message was bad, not acknowledged\n");
    exit(1);
  }
  int sendId = send(socketId, query, strlen(query), 0);
  if (sendId == -1) {
    perror("error sending message to server\n");
    exit(1);
  }
  printf("waiting to recieve response from server\n");
  int responses;
  int recID = recv(socketId, &responses, sizeof(responses), 0);
  if (recID == -1) {
    perror("error recieving message\n");
    exit(1);
  }
  printf("This is the number of responses %d\n", responses);
  int ackId = SendAck(socketId);
  if (ackId == -1) {
    perror("error sending acknowledged\n");
    exit(1);
  }
  int x = responses;
  while (x > 0) {
    int nullTerm = recv(socketId, resp, 999, 0);
    resp[nullTerm] = '\0';
    printf("%s\n", resp);
    SendAck(socketId);
    x--;
  }
  close(socketId);
}

void KillServer(char *ipAddress, char *port) {
  struct addrinfo *star = createAddrInfo(ipAddress, port);
  int socketId = createAndConnectSocket(star);
  int x = SendKill(socketId);
  if (x == -1) {
    perror("issue sending kill from client to server\n");
    exit(1);
  }
  close(socketId);
  printf("client leaving server\n");
  exit(EXIT_SUCCESS);
}

void RunPrompt(char *ipAddress, char *port) {
  char input[1000];
  while (1) {
    printf("Enter a term to search for, q to quit or k to kill: ");
    scanf("%s", input);
    if (strlen(input) == 1) {
      if (input[0] == 'q') {
        exit(0);
      } else {
       if ((input[0] == 'k') || input[0] == 'K') {
        KillServer(ipAddress, port);
       }
      }
    }
    RunQuery(input, ipAddress, port);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    perror("your inputs suck");
    exit(1);
  }
  RunPrompt(argv[1], argv[2]);
  return 0;
}
