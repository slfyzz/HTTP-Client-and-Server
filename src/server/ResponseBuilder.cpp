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

#include "Response.h"

std::string getStatusMessage(Status_Code status_code) {
    switch(status_code) {
        case BAD_REQ_COD: return "Bad Request";
        case OK_COD: return "OK";
        case NOT_FOUND_COD: return "Not Found";
        case LENGTH_REQUIRED_COD: return "Length is Required";
    }
    return "Not supported";
}


std::string getResponseMessage(Status_Code status_code, int content_len) {
    std::string msg = std::string("HTTP/1.1 ").append(std::to_string(status_code)).append(" ").append(getStatusMessage(status_code));
    if (content_len >= 0) {
        msg.append("\r\nContent-Length: ").append(std::to_string(content_len));
    }   
    return msg.append(config::EOH);
}
