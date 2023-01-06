#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>     
#include <sys/types.h>
#include "LIST.h"

#define MAXMSGLENGTH 256
pthread_t threads[4]; // create 4 threads
static int terminate=0; //use it to stop the program when enter'!'

typedef struct message {
    int type;
    char msg[MAXMSGLENGTH];
} message;

typedef struct sockInfo {
    int sockFD;     
    struct addrinfo *destAddrInfo;
} sockInfo;

static LIST *printList; 
static LIST *sendList; 

static sockInfo *sockInfoPassToThread;

static sem_t printSem;
static sem_t sendSem;

static pthread_mutex_t printMutex;
static pthread_mutex_t sendMutex;

struct addrinfo *address(char *port, char *remoteAddr) {
    struct addrinfo hints;
    struct addrinfo *servinfo;
    int rv;
    
    memset(&hints, 0, sizeof (hints));
    if (!remoteAddr){
        hints.ai_flags = AI_PASSIVE; 
    }
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_DGRAM;

    rv = getaddrinfo(remoteAddr, port, &hints, &servinfo) != 0;
        
    return servinfo;
}

//get message form keyboard
void *getMsg(void *t) {
    char msg[MAXMSGLENGTH+1];
    int numbytes;
    while (!terminate) {
        if (fgets(msg, MAXMSGLENGTH, stdin)) {
                numbytes = strlen(msg);
                if (numbytes) {
                    if (msg[0] == '!'&&numbytes==2) {    
                        terminate = 1;
                    } else {
                        pthread_mutex_lock(&printMutex);
                        listInput(0, msg, printList);
                        pthread_mutex_unlock(&printMutex);
                    }
                    pthread_mutex_lock(&sendMutex);
                    listInput(1, msg, sendList);
                    pthread_mutex_unlock(&sendMutex);
                    sem_post(&printSem);
                    sem_post(&sendSem);
                }
            }
        }
    pthread_exit(NULL);
}

int setHostSocket(char *port) {
    struct addrinfo *p, *servinfo;
    int sockfd = 0;
    
    servinfo = address(port, NULL);
    if (!servinfo) {
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("listener: bind");
            close(sockfd);
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);
    if (!p) {
        return -1;
    } 
    else
        return sockfd;
}

struct addrinfo *setDestAddr(char *port, char *remoteAddr) {
    struct addrinfo *p1, *p2;

    p1 = address(port, remoteAddr);
    if (!p1)
        return NULL;
    else {
        p2 = p1->ai_next;
        while (p2) {
            free(p1);
            p1 = p2;
            p2 = p2->ai_next;
        }
        return p1;
    }
}

//put message into List
void listInput(int type, char *str, LIST *l) {
    message *messages = malloc(sizeof(message));
    strcpy(messages->msg, str);
    messages->type = type;
    ListPrepend(l, messages);
}

//send information to other
void *sentInfo(void *t) {
    message *Info;
    int sockFD = ((sockInfo *) t)->sockFD;
    struct addrinfo *destAddrInfo = ((sockInfo *) t)->destAddrInfo;
    int numbytes;
    while (!terminate ) {      
        sem_wait(&sendSem);
        pthread_mutex_lock(&sendMutex);
        Info = ListTrim(sendList);
        pthread_mutex_unlock(&sendMutex);
        if (Info != NULL ) {
            if ((numbytes = sendto(sockFD, Info->msg, strlen(Info->msg), 0,
                                   destAddrInfo->ai_addr, destAddrInfo->ai_addrlen)) == -1) {
                perror("talker: sendto");
                exit(1);
            }
            
            free(Info);
        }
    }
    pthread_exit(NULL);
}

//receive information from other
void *recvInfo(void *t) {
    int rv;   
    struct sockaddr_storage their_addr; 
    socklen_t addr_len;
    char msg[MAXMSGLENGTH];
    sockInfo *mySockInfo = (sockInfo *) t;
    int sockFD = mySockInfo->sockFD;
    int numbytes;
    while (!terminate) {
        addr_len = sizeof(their_addr);          
        memset(msg, 0, sizeof msg);
        if ((numbytes = recvfrom(sockFD, msg, MAXMSGLENGTH - 1, 0, (struct sockaddr *) &their_addr, &addr_len)) == -1) {
            perror("recvfrom");
        } 
        else {
            msg[numbytes] = '\0';
            if (msg[0] == '!'&&strlen(msg)==2) {    
                terminate = 1;
            } 
            else {
                pthread_mutex_lock(&printMutex);
                listInput(1, msg, printList);
                pthread_mutex_unlock(&printMutex);
            }            
            sem_post(&printSem);
        }
    }   
    sem_post(&sendSem);
    pthread_exit(NULL);
}

//show message to screen
void *display(void *t) {
    message *print;
    while (!terminate) {
        sem_wait(&printSem);
        pthread_mutex_lock(&printMutex);
        print = ListTrim(printList);
        pthread_mutex_unlock(&printMutex);
        if (print != NULL) {
            if (print->type == 1)
                printf("He/She: %s", print->msg);
            else
                printf("Me: %s", print->msg);
            free(print);
        }
    }
    printf("exit!\n");
    pthread_cancel(threads[0]);
    pthread_exit(NULL);
    
}


int main(int argc, char *argv[]) {   
    int rv; 
    sockInfoPassToThread = malloc(sizeof(sockInfo));

    char *myPort= argv[1];
    char *remoteAddr=argv[2];          
    char *remotePort= argv[3];     
    int sockFD;

    if ((sockFD = setHostSocket(myPort)) == -1) {
        fprintf(stderr, "Failed to create and bind receiving socket. Maybe try a different Port # ?\nBye!\n");
        return 1;
    }
    else {
        sockInfoPassToThread->sockFD = sockFD;
    }
    if ((sockInfoPassToThread->destAddrInfo = setDestAddr(remotePort, remoteAddr)) == NULL) {
        fprintf(stderr, "Failed to create destination addrinfo struct. Try a different Port # next time\nBye!\n");
        return 1;
    }

    printList = ListCreate();
    sendList = ListCreate();
    
    //create sem and mutex
    sem_init(&printSem, 0, 0);
    sem_init(&sendSem, 0, 0);
    pthread_mutex_init(&printMutex, NULL);
    pthread_mutex_init(&sendMutex, NULL);

    //create 4 threads
    pthread_create(&threads[0], NULL, getMsg, NULL);
    pthread_create(&threads[1], NULL, display, NULL);
    pthread_create(&threads[2], NULL, sentInfo, (void *) sockInfoPassToThread);    
    pthread_create(&threads[3], NULL, recvInfo, (void *) sockInfoPassToThread);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    pthread_join(threads[2], NULL);

    freeaddrinfo(sockInfoPassToThread->destAddrInfo);
    close(sockInfoPassToThread->sockFD);
    ListFree(printList, free);
    ListFree(sendList, free);

    //distroy mutex and sem
    pthread_mutex_destroy(&printMutex);
    pthread_mutex_destroy(&sendMutex);
    sem_destroy(&printSem);
    sem_destroy(&sendSem);

    return 0;
}
