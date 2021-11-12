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
#include <fstream>
#include <regex>

using namespace std;

#define DEFAULT_PORT "80"

const string EOH = "\r\n\r\n";


int sendAll(const std::string &res, int fd) {

    int bytes_sent = 0;
    int remains = res.size();
    while (remains > 0 && (bytes_sent = send(fd, res.c_str() + (res.size() - remains), remains, 0)) != -1) {
        remains -= bytes_sent;
    }
    if (bytes_sent == -1) return -1;
    return 0;

}

int getFileSize(const std::string &filename) {
    FILE *p_file = NULL;
    // paths are sent in the following format "/blabla", so we need to add the dot to search locally.
    string fname = "." + filename;
    p_file = fopen(fname.c_str(),"rb");
    if (p_file == NULL) return -1;

    fseek(p_file,0,SEEK_END);
    int size = ftell(p_file);
    fclose(p_file);
    return size;
}

int sendFile(const string &fileName, int fileSize, int fd) {
    
    // read the file as a stream of binary data.
    // paths are sent in the following format "/blabla", so we need to add the dot to search locally.
    ifstream file("." + fileName, std::ifstream::binary);
    
    // check the existence of the file.
    if (file.fail() || fileSize < 0) {
        return -1;
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
            return -1;
        }
    }
    file.close();
    return 0;
}

int rcvAll(int fd, int len, std::string &body) {
    char buff[1024];
    int numOfBytes = 0;

    // if body is already filled with some bytes.
    len -= body.size();
    
    // the len is already 0
    if (len == 0) return body.length();
    // the body contains data from next request.
    else if (len < 0) {
        return body.size() + len;
    }

    while ((numOfBytes = recv(fd, buff, min(1023, len), 0)) != -1) {
        buff[numOfBytes] = '\0';
        if (numOfBytes == 0) return 0;
        len -= numOfBytes;
        body.append(buff, numOfBytes);
        if (len <= 0) return body.length();
    }
    return -1;
}

int checkEOH(const string& buffer, int from) {
    // check if EOH is found, and return the starter index of matched substring.
    for (int i = std::max(from - (int)EOH.length(), 0); i < buffer.length() - (int)EOH.length() + 1; i++) {
        int j = 0;
        for (;j < EOH.length(); j++) {
            if (buffer[i + j] != EOH[j]) {
                break;
            }
        }
        if (j == EOH.length()) {
            return i;
        }
    }

    return -1;
}

int getContentLength (const string &header) {
    regex len_regex("Content-Length: ?([0-9]+)");
    smatch len_match;
    if (!regex_search(header, len_match, len_regex)) {
        return -1;
    }
    return stoi(len_match[1].str());
}

int rcvHTTPResponse(int sockfd, string &response) {
    char buff[1024] = {0};
    int bytes = 0, end = -1;
    while ((bytes = recv(sockfd, buff, 1023, 0)) != -1) {
        // something wrong.
        if (bytes <= 0) return bytes;
        
        int prevLen = response.size();
        response.append(buff, bytes);

        // to check if we get the end of the response. and no need to get the body if there's no body.
        if ((end = checkEOH(buff, prevLen)) != -1) {
            string body = "";
            if (end + 4 < response.length()) {
                body = response.substr(end + EOH.length());
                response = response.substr(0, end + EOH.length());
            }
            int cont_len = getContentLength(response.substr(0, end + 4));
            if (cont_len != -1) {
                if (rcvAll(sockfd, cont_len, body) == -1) {
                    return -1;
                }
            }
            response.append(body);
            return 1;
        }
    }
    return -1;
}

int setup_socket(const string &hostName, const string &port) {
    
    struct addrinfo *serverinfo, hints;
    int status, sockfd, yes = 1;

    // setting hints to zero
    memset(&hints, 0, sizeof hints);

    hints.ai_family = AF_INET; // requirements in the lab.
    hints.ai_socktype = SOCK_STREAM; // TCP

    // getting the addressinfo of the given hostname.
    if ((status = getaddrinfo(hostName.c_str(), port.c_str(), &hints, &serverinfo) != 0)) {
        perror("Address info error\n");
        exit(1);
    }

    // Get the valid info and try to connect.
    struct addrinfo *info = NULL;
    for (info = serverinfo; info != NULL; info = serverinfo->ai_next) {
        if ((sockfd = socket(info->ai_family, info->ai_socktype, info->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        if (connect(sockfd, info->ai_addr, info->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    // nothing is found.
    if (info == NULL) {
        perror("Can't find hostname");
        exit(1);
    }

    freeaddrinfo(serverinfo);
    return sockfd;
}

vector<string> split(const string &str) {
    vector<string> ans;
    std::stringstream splitter(str);
    string temp;
    // split by whitespaces.
    while (std::getline(splitter, temp, ' ')) {
        if (!temp.empty())
            ans.push_back(temp);
    }

    return ans;
}

int getQuery (const vector<string> &comp, string &query, int len) {

    // not GET or POST
    if (comp[0] != "GET" && comp[0] != "POST") return -1;
    
    query.append(comp[0]).append(" ").append(comp[1]).append(" HTTP/1.1");
    // if Length is provided and it's a post operation, then we need to send the content-length header.
    if (len != -1 && comp[0] != "GET") {
        query.append("\r\nContent-Length: ").append(to_string(len));
    }

    // to indicate the end of the query.
    query.append("\r\n\r\n");

    // to indicate the type of the query.
    return comp[0] == "GET" ? 0 : 1;
}


int main(int argc, char *argv[]) {

    // default hostname and ports.
    string defaultHostname = (argc > 1 ? argv[1] : "");
    string defaultPort = (argc > 2 ? argv[2] : DEFAULT_PORT);

    // file stream of commands
    std::ifstream commandFile("commands.txt");
    string command, hostname, port;

    int status, socket = -1;

    // placeholder for data to be sent.
    // string data = "BLA BLA BLA";

    // read command by command.
    while (std::getline(commandFile, command)) {
        
        // split the command by whitespaces.
        vector<string> components = split(command);
        
        // Getting the hostname, and the port of the command.
        string currHostname = components.size() > 2 ? components[2] : defaultHostname, 
            currPort = components.size() > 3 ? components[3] : defaultPort;
        
        // if it's different than the previous command, 
        // then we will need to close the connection and start new connection.
        if (currHostname != hostname || port != currPort) {
            if (socket >= 0) { 
                close(socket);
                // just a flag to know if the socket is valid.
                socket = -1;
            }
            hostname = currHostname;
            port = currPort;
            if (hostname.empty() || port.empty()) {
                cout << "No hostname provided for the following request: " << command << '\n';
                continue;
            }
            socket = setup_socket(hostname, port);
        }
        
        int data_len = components[0] == "GET" ? -1 : getFileSize(components[1]);

        string query;
        // Build HTTP request.
        if ((status = getQuery(components, query, data_len)) == -1) {
            printf("Invalid query, maybe not supported!\n");
            continue;
        }

        cout << " Request : " << query;

        // Trying to send all the query.
        if (sendAll(query, socket) == -1) {
            perror("Client: send");
            continue;
        }

        // 0 means it's a GET Operation.
        // otherwise, it's a POST operation.
        if (status == 1) {
            if (sendFile(components[1], data_len, socket) == -1) {
                perror("Client: send");
            }
        }

        // Get the response.
        string res;
        status = rcvHTTPResponse(socket, res);
        cout << "Response : " << res << '\n';
        // Server closed the connection
        if (status == 0) {
            cout << "Server closed the connection\n";
            close(socket);
            // just a flag to know if the socket is valid.
            socket = -1;
        } else if (status < 0) {
            // error!
            perror("Client: receive");
        }
    }
    if (socket >= 0) close(socket);

    return 0;
}