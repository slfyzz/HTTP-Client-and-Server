#ifndef RESPONSE_H_
#define RESPONSE_H_
#include <string>

enum Response_Status {
    BAD_REQ,
    SEND,
    CLOSE,
    ERROR,
    CONTINUE
};

enum Status_Code {
    BAD_REQ_COD = 400,
    OK_COD = 200,
    NOT_FOUND_COD = 404,
    LENGTH_REQUIRED_COD = 411
};

namespace config {
    const std::string EOH = "\r\n\r\n";
}


#endif