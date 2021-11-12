#include <stdio.h>
#include <vector>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>



using namespace std;

// default port
#define PORT "3490"

// default number of not-yet-accepted connections in socket queue. 
#define QUEUE_LEN 10


// starter function for the thread/worker to handle the request.
void* deal_with_connection(void *fd);

// To Calc timeout dynamically and give a pool of threads.
void resetThreads();
pthread_t* getAvailableThread(int fd);

/**
 * @brief Get the socket address struct either in sockaddr_in(IPv4) or sockaddr_in6(IPv6) based on socket family attribute.
 * @param sa general socket address 
 */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &((struct sockaddr_in *) sa)->sin_addr;
    }

    return &((struct sockaddr_in6 *) sa)->sin6_addr;
}

/**
 * @brief Setup localhost socket and bind it to the default/given port.
 * 
 * @return int socket file descriptor.
 */
int setup_socket(const string &port) {
    
    struct addrinfo *serverinfo, hints;
    int status, sockfd, yes = 1;

    // setting hints to zero
    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_UNSPEC;   // we can use either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP used.
    hints.ai_flags = AI_PASSIVE; // to make getaddrinfo fill info of the localhost.


    if ((status = getaddrinfo(NULL, port.c_str(), &hints, &serverinfo) != 0)) {
        perror("Address info error\n");
        exit(1);
    }

    struct addrinfo *info = NULL;
    for (info = serverinfo; info != NULL; info = serverinfo->ai_next) {
        if ((sockfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        // to avoid "Port is already used".
        if ((status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        // bind the socket to the port.
        if ((status = bind(sockfd, info->ai_addr, sizeof *(info->ai_addr))) == -1) {
            perror("server: bind");
            close(sockfd);
            continue;
        }
        break;
    }

    // in case no info is valid.
    if (info == NULL) {
        perror("Can't bind the socket to the port");
        exit(1);
    }

    // We don't need it anymore.
    freeaddrinfo(serverinfo);
    
    return sockfd;
}


int main(int argc, char *argv[]) {

    string port = (argc > 1 ? string(argv[1]): PORT);

    // To hold socket addresses for clients (connections).
    struct sockaddr_storage outside_sockets;

    // Buffer used to store client names (to know who requested to connect).
    char buffer[INET6_ADDRSTRLEN];
    // hold connection socket file descriptor
    long connectionFD;

    // setup local socket.
    int sockfd = setup_socket(port);

    cout << "Starting the server with port : " << port << '\n';

    // set number of connections to zero.
    resetThreads();

    cout << "Statring listening on the port\n";
    // Listen to connections.
    if (listen(sockfd, QUEUE_LEN) == -1) {
        perror("server: listening");
        exit(1);
    }

    unsigned int size = sizeof outside_sockets;
    pthread_t* thread_ptr = NULL;
    while (1) {

        // Trying to accept a connection.
        if ((connectionFD = accept(sockfd, (struct sockaddr *) &outside_sockets, &size)) != -1) {

            thread_ptr = getAvailableThread(connectionFD);

            // To know the client.
            inet_ntop(outside_sockets.ss_family, get_in_addr((struct sockaddr *)&outside_sockets), buffer, sizeof buffer);
            
            cout << "Server: get connected to " << buffer << " with ID: " << connectionFD << '\n';
        
            // create a thread to deal with that connection. 
            pthread_create(thread_ptr, NULL, deal_with_connection, (void *)connectionFD);
        }  
    } 

    printf("Closing the server.");
    close(sockfd);
    return 0;
}