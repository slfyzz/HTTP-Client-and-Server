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


using namespace std;


const string FILES_PATH = "./public";

// To Calc timeout dynamically.
void freeThread(int fd);
int estimateTimeOut();

/**
 * Socket utility to provide send and receive all data. 
 * Since send() and recv() don't guarantee sending/receiving all the data.
 */
int sendAll(const std::string &res, int fd);
int rcvAll(int fd, int len, std::string &body);

/**
 * to Deal with status code and messages.
 */
string getStatusMessage(Status_Code status_code);
string getResponseMessage(Status_Code status_code, int content_len);

/** 
 * Parse a given request using regex, and returns a vector of components of the request
 * @return description of the return value, vector of strings: 
 * components[0]: the method (GET/POST)
 * components[1]: the file location (path)
 * components[2]: HTTP version
 * components[3]: (optional) and contains the optional headers.
 */
vector<string> parse_request(const string &buf){
    
    vector<string> ans;
    regex str_expr ("(GET|POST) ([^\\s]*) (HTTP\/1\.[01])\r\n(.*\r\n)*\r\n");
    if (!regex_match(buf, str_expr)) return ans;
    
    smatch sm;
    regex_match(buf.cbegin(), buf.cend(), sm, str_expr); 
    for (unsigned i = 1; i < sm.size(); i++) ans.push_back(sm[i].str());

    // Folder of pathes.
    if (ans.size() >= 2) {
        ans[1] = FILES_PATH + ans[1];
    }

    return ans;
}

/**
 * Tries to search for Content-length header in the optional header of the request.
 */
int getContentLength (const string &header) {
    regex len_regex("Content-Length: ?([0-9]+)");
    smatch len_match;
    if (!regex_search(header, len_match, len_regex)) {
        return -1;
    }
    return stoi(len_match[1].str());
}

void writeFile(const std::string &filename, const std::string &body) {
    ofstream file(filename);
    file << body;
    file.close();
}

int getFileSize(const std::string &filename) {
    FILE *p_file = NULL;
    p_file = fopen(filename.c_str(),"rb");
    if (p_file == NULL) return -1;

    fseek(p_file,0,SEEK_END);
    int size = ftell(p_file);
    fclose(p_file);
    return size;
}

int sendFile(const string &fileName, int fd) {
    
    // Get the total length of the file.
    int fileSize = getFileSize(fileName);

    // read the file as a stream of binary data.
    ifstream file(fileName, std::ifstream::binary);
    
    // check the existence of the file.
    if (file.fail() || fileSize < 0) {
        return -1;
    }

    // trying to send the OK message.
    if (sendAll(getResponseMessage(OK_COD, fileSize), fd) == -1) {
        return ERROR;
    }

    const int BUFFER_SIZE = 1024;
    char buff[BUFFER_SIZE] = {0};

    while (file) {
        file.read(buff, BUFFER_SIZE - 1);
        size_t count = file.gcount();
        if (!count) break;
        buff[count] = '\0';
        // sending a part of the file.
        if (sendAll(string(buff, count), fd) == -1) {
            file.close();
            return ERROR;
        }
    }
    file.close();
    return 0;
}

Response_Status GETResponse(const vector<string>& components, int fd) {
    
    // sending the file.
    if (sendFile(components[1], fd) == -1) {
        // in case of the file doesn't exist.
        if (sendAll(getResponseMessage(NOT_FOUND_COD, -1), fd) == -1) return ERROR;
        return SEND;
    }
    return SEND; 
}

Response_Status POSTResponse(const vector<string>& components, int fd, string &body) {
    
    // ASSUMPTION: Post request NEED TO HAVE "Content-Length" attribute to know the length of the body.
    int content_len = 0;
    if ((content_len = getContentLength(components[3])) == -1) {
        // if the length is not provided.
        if (sendAll(getResponseMessage(LENGTH_REQUIRED_COD, -1), fd) == -1) {
            return ERROR;
        }
        return BAD_REQ;
    }
    // receive the data based on the content_len.
    // status if positive, would contain the size of the body.
    int status = rcvAll(fd, content_len, body);
    if (status == -1){
        return ERROR;
    }

    // consuming the body.
    string reqBody = body.substr(0, status);
    if (status < body.length()) {
        body = body.substr(status);
    } else {
        body = "";
    }

    // logging.
    cout << fd << " : Body: " << reqBody << '\n';
    cout << fd << " : End of the body\n";
    writeFile(components[1], reqBody);

    // sending the OK message after getting the body.
    if (sendAll(getResponseMessage(OK_COD, -1), fd) == -1) {
        return ERROR;
    }

    // if we received 0 that means the client closed the connection :(
    return status == 0 ? CLOSE : SEND;
}


Response_Status response(const string &query, int fd, string &body) {

    // logging.
    cout << fd << " : Request: " << query; 
    cout << fd << " : End of Request\n";

    // parse the request to number of components.
    vector<string> components = parse_request(query);

    // no results means Bad request.
    if (components.size() == 0) {
        if (sendAll(getResponseMessage(BAD_REQ_COD, -1), fd) == -1) return ERROR;
        return BAD_REQ;
    }
    else if (components[0] == "POST") {
        return POSTResponse(components, fd, body);

    } else if (components[0] == "GET") {
        return GETResponse(components, fd);
    }
    else {
        if (sendAll(getResponseMessage(BAD_REQ_COD, -1), fd) == -1) return ERROR;
        return BAD_REQ;
    }
}


Response_Status accumulate(string &accumulatedBuffer, char buffer[], int len, int fd) {
    
    int prevLen = accumulatedBuffer.length();
    accumulatedBuffer.append(string(buffer, len));
    
    if (accumulatedBuffer.length() < config::EOH.length()) {
        return CONTINUE;
    }

    // start checking the accumulated before looking for EOH (End of header sign).
    for (int i = std::max(prevLen - (int)config::EOH.length(), 0); i < accumulatedBuffer.length() - config::EOH.length() + 1; i++) {
        int j = 0;
        for (;j < config::EOH.length(); j++) {
            if (accumulatedBuffer[i + j] != config::EOH[j]) {
                break;
            }
        }
        // reach the end of the header.
        if (j == config::EOH.length()) {
            string body = "";
            // which means that we had read from the body too
            if (i + config::EOH.length() < accumulatedBuffer.length()) {
                body = accumulatedBuffer.substr(i + config::EOH.length());
            }
            // get the response based on the current request and the part of the body.
            Response_Status status = response(accumulatedBuffer.substr(0, i + config::EOH.length()), fd, body);
            // reset the accumulated Buffer, response should consume what it needs from the body.
            accumulatedBuffer = body;
            
            if (status == CLOSE || status == ERROR || accumulatedBuffer.length() == 0) return status;
            
            // Check if the remaining buffer contains a new req
            i = -1;
        }
    }
    return CONTINUE;
}

// Main function here.
void* deal_with_connection(void *fd) {
    
    // Using poll method to block the thread until we got a data to read, and to set a timeout.
    struct pollfd fds[1];

    fds[0].fd = (long)fd;
    fds[0].events = POLLIN; // input event.

    int events; // will hold the number of events got fired.
    
    const int BUFFER_SIZE = 2096;
    // Buffer to receive data.
    char buffer[BUFFER_SIZE];
    
    // Each thread would have accBuffer to accumulate the the request message, since 
    // recv can return a part of the request, so we are using accBuffer as a dynamic buffer until 
    // we get any indicator that the message is completely sent.
    string accBuffer = "";
    int timeout;
    while (true) {
        
        timeout = estimateTimeOut();
        cout << fds[0].fd << " : timeout : " << timeout << "ms\n";
        // Get blocked waiting for any events.
        events = poll(fds, 1, timeout);
        
        // Timed out!!!!
        if (events == 0) {
            cout << fds[0].fd << " : Timed out, bye bye\n"; 
            break;
        }   
        if (fds[0].revents & POLLIN) {
            // get some data.
            int received = recv(fds[0].fd, buffer, BUFFER_SIZE - 1, 0);
            // Connection is closed.
            if (received == 0) {
                cout << fds[0].fd << " : Closed by Client, bye bye\n";
                break;
            }
            else if (received < 0) {
                // some error happened
                cout << fds[0].fd << " : ";
                perror("Unkown error with read");
                break;
            }

            buffer[received] = '\0';
            // to accumulate the request.
            Response_Status status = accumulate(accBuffer, buffer, received, fds[0].fd);
            
            if (status == ERROR) {
                cout << fds[0].fd << " : ";
                perror("Unkown error with the request");
                break;
            }
            else if (status == CLOSE) {
                cout << fds[0].fd << " : Closed by Client, bye bye\n";
                break;
            } 
            
        } 
    }
    // that connection going to terminate now.
    freeThread(fds[0].fd);
    close(fds[0].fd);
    return fd;
}
