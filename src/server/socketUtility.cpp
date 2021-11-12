#include <stdio.h>
#include <fstream>
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
#include <regex>
#include <math.h>


int sendAll(const std::string &res, int fd) {

    int bytes_sent = 0;
    int remains = res.size();
    // Assuming that the string don't change, then the pointer to the first char won't case any issue.
    while (remains > 0 && (bytes_sent = send(fd, res.c_str() + (res.size() - remains), remains, 0)) != -1) {
        remains -= bytes_sent;
    }
    if (bytes_sent == -1) return -1;
    return 0;
}

int rcvAll(int fd, int len, std::string &body) {
    char buff[1024];
    int numOfBytes = 0;

    // if body is already filled with some bytes.
    len -= body.size();
    
    if (len == 0) return body.length();
    // the body contains data from next request.
    else if (len < 0) {
        return body.size() + len;
    }

    while ((numOfBytes = recv(fd, buff, std::min(1023, len), 0)) != -1) {
        buff[numOfBytes] = '\0';
        if (numOfBytes == 0) return 0;
        len -= numOfBytes;
        body.append(buff, numOfBytes);
        if (len <= 0) return body.length();
    }

    return -1;
}