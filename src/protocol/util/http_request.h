#pragma once

#include <map>
#include <string>

namespace caspar { namespace http {

struct HTTPResponse
{
    unsigned int                       status_code;
    std::string                        status_message;
    std::map<std::string, std::string> headers;
    std::string                        body;
};

// Public functions
HTTPResponse request(const std::string& host, const std::string& port, const std::string& path);

// Public url_encode for other files
std::string url_encode(const std::string& str);

}} // namespace caspar::http