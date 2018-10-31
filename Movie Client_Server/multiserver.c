#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "queryprotocol.h"
#include "docset.h"
#include "movieIndex.h"
#include "docidmap.h"
#include "includes/Hashtable.h"
#include "queryprocessor.h"
#include "fileparser.h"
#include "filecrawler.h"


int Cleanup();

DocIdMap docs;
Index docIndex;

char movieSearchResult[1500];


void sigchld_handler(int s) {
  write(0, "Handling zombies...\n", 20);
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0);
  errno = saved_errno;
}


void sigint_handler(int sig) {
  write(0, "Ahhh! SIGINT!\n", 14);
  Cleanup();
  exit(0);
}

void Setup(char *dir) {
  struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("sigaction");
      exit(1);
    }
    struct sigaction kill;
    kill.sa_handler = sigint_handler;
    kill.sa_flags = 0;
    sigemptyset(&kill.sa_mask);
    if (sigaction(SIGINT, &kill, NULL) == -1) {
      perror("sigaction");
      exit(1);
    }
    printf("Crawling directory tree starting at: %s\n", dir);
    docs = CreateDocIdMap();
    CrawlFilesToMap(dir, docs);
    printf("Crawled %d files.\n", NumElemsInHashtable(docs));
    docIndex = CreateIndex();
    printf("Parsing and indexing files...\n");
    ParseTheFiles(docs, docIndex);
    printf("%d entries in the index.\n", NumElemsInHashtable(docIndex));
  }

  int Cleanup() {
    DestroyIndex(docIndex);
    DestroyDocIdMap(docs);
    return 0;
  }

  int createSocket(struct addrinfo *resPtr) {
  int socketFd =
    socket(resPtr->ai_family, resPtr->ai_socktype, resPtr->ai_protocol);
  if (socketFd < 0) {
    perror("creation of socket failed\n");
    exit(1);
  }
  return socketFd;
}

void bindSocket(int socketFd, struct addrinfo *resPtr) {
  int bindId = bind(socketFd, resPtr->ai_addr, resPtr->ai_addrlen);
  if (bindId < 0) {
    perror("error binding the socket on the server\n");
    exit(1);
  }
  printf("bind successful\n");
}
void listenSocket(int socketFd) {
  int listenId = listen(socketFd, 20);
  if (listenId == -1) {
    perror("error listening to the socket on the server side\n");
    exit(1);
  }
  printf("listening for client\n");
}

int acceptSocket(int socketFd) {
  int acceptId = accept(socketFd, NULL, NULL);
  if (acceptId == -1) {
    perror("error accepting the client of the server side\n");
    exit(1);
  }
  printf("successful accept of client\n");
  return acceptId;
}

void sendAcknowledgement(int acceptId) {
  int ackId = SendAck(acceptId);
  if (ackId != 0) {
    perror("acknowledgement from server to cleint failed\n");
    exit(1);
  }
}

void checkAcknowledgement(char resp[]) {
  if (CheckAck(resp) != 0) {
    perror("waiting for ack \n");
    exit(1);
  }
}

void killserver(char resp[], int socketFd, int acceptId) {
  if (CheckKill(resp) == 0) {
      printf("Server must die!\n");
      close(socketFd);
      close(acceptId);
      Cleanup();
      exit(0);
  }
}

struct addrinfo *createAddrInfo(char* cmdLine) {
  struct addrinfo res;
  struct addrinfo *resPtr;
  memset(&res, 0, sizeof res);
  res.ai_family = AF_UNSPEC;
  res.ai_socktype = SOCK_STREAM;
  res.ai_flags = AI_PASSIVE;
  int address = getaddrinfo(NULL, cmdLine, &res, &resPtr);
  if (address < 0) {
    perror("error creating the addrinfo struct\n");
    exit(1);
  }
  return resPtr;
}

void noMovies(int acceptId, char resp[], int len) {
  int responses = 0;
      int numResp = send(acceptId, &responses, sizeof(responses), 0);
      if (numResp < 0) {
        perror("error sending the number of responses from server to client\n");
        exit(1);
      }
      char *emptyMessage = "There are no results for this query\n";
      len = recv(acceptId, resp, len, 0);
      resp[len] = '\0';
      if (CheckAck(resp) != 0) {
        perror("waiting for ack \n");
        exit(1);
      }
      int check = send(acceptId, emptyMessage, sizeof(emptyMessage), 0);
      if (check == -1) {
        perror("error sending message if iterater is NULL\n");
        exit(1);
      }
}

void sendNumMovies(SearchResultIter iter, int acceptId) {
  int responses = NumResultsInIter(iter);
  int numResp = send(acceptId, &responses, sizeof(responses), 0);
  if (numResp < 0) {
    perror("error sending the number of responses\n");
    exit(1);
  }
  printf("sent number of in index to client\n");
}

void sendAllMovies(SearchResultIter iter, SearchResult output,
                   int acceptId, char resp[]) {
  GetNextSearchResult(iter, output);
  char dest[1000];
  GetRowFromFile(output, docs, dest);
  int sendResult = send(acceptId, dest, sizeof(dest)-1, 0);
  if (sendResult == -1) {
    perror("error sending movie to client\n");
    exit(1);
  }
  int waitAck = recv(acceptId, resp, 999, 0);
  if (waitAck == -1) {
    perror("error recviegin acknowledgement\n");
    exit(1);
  }
  resp[waitAck] = '\0';
  char *holder = resp;
  if (CheckAck(holder) != 0) {
    perror("error with ack\n");
    exit(1);
  }
}

void checkArguments(int argc) {
  if (argc != 3) {
    perror("inputs are invalid\n");
    exit(1);
  }
}
  int main(int argc, char **argv) {
    checkArguments(argc);
    Setup(argv[1]);
    struct addrinfo *resPtr = createAddrInfo(argv[2]);
    int socketFd = createSocket(resPtr);
    bindSocket(socketFd, resPtr);
    listenSocket(socketFd);
    while (1) {
    int acceptId = acceptSocket(socketFd);
    if (!fork()) {
      close(socketFd);
      sendAcknowledgement(acceptId);
      printf("waiting for query from client\n");
      char resp[1000];
      int len = recv(acceptId, resp, 999, 0);
      resp[len] = '\0';
      if (len == -1) {
        perror("error recieving message\n");
        exit(1);
      }
      SearchResultIter iter = FindMovies(docIndex, resp);
      if (iter == NULL) {
        noMovies(acceptId, resp, len);
      } else {
          sendNumMovies(iter, acceptId);
          len = recv(acceptId, resp, len, 0);
          resp[len] = '\0';
          checkAcknowledgement(resp);
          SearchResult output = (SearchResult)malloc(sizeof(SearchResult));
          while (SearchResultIterHasMore(iter)) {
            sendAllMovies(iter, output, acceptId, resp);
          }
          printf("all movies sent to client\n");
          free(output);
          DestroySearchResultIter(iter);
      }
      close(acceptId);
      exit(0);
    }
    }
    close(socketFd);
    Cleanup();
    return 0;
  }
