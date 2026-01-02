#include "http_request.h"
#include <common/except.h>

#include <boost/asio.hpp>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace caspar { namespace http {

namespace {

// Encode a single segment of a URL (no slashes)
std::string url_encode_segment(const std::string& str)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            escaped << c;
        else
            escaped << '%' << std::uppercase << std::setw(2) << int(c) << std::nouppercase;
    }

    return escaped.str();
}

// Encode full path, preserving '/' between segments
std::string url_encode_path(const std::string& path)
{
    std::ostringstream encoded;
    std::istringstream iss(path);
    std::string segment;

    while (std::getline(iss, segment, '/')) {
        if (!segment.empty()) {
            encoded << '/' << url_encode_segment(segment);
        }
    }

    if (!path.empty() && path.back() == '/')
        encoded << '/';

    return encoded.str();
}

} // anonymous namespace

// Public url_encode function used by other files
std::string url_encode(const std::string& str)
{
    // Quick tweak: decode any existing % encoding first to avoid double-encoding
    std::string decoded;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            std::istringstream hex_stream(str.substr(i + 1, 2));
            int val;
            if (hex_stream >> std::hex >> val) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += str[i];
    }

    return url_encode_path(decoded);
}

// --- request() function stays mostly the same ---
HTTPResponse request(const std::string& host, const std::string& port, const std::string& path)
{
    using boost::asio::ip::tcp;
    using namespace boost;

    HTTPResponse res;
    asio::io_service io_service;

    tcp::resolver resolver(io_service);
    tcp::resolver::query query(host, port);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    tcp::socket socket(io_service);
    boost::system::error_code error;
    asio::connect(socket, endpoint_iterator, error);

    if (error == asio::error::connection_refused) {
        res.status_code = 503;
        res.status_message = "Connection refused";
        return res;
    }
    if (error) {
        CASPAR_THROW_EXCEPTION(io_error() << msg_info(error.message()));
    }

    // Use the public encoder (safe for thumbnails)
    std::string encoded_path = url_encode(path);
    std::cout << "DEBUG: Requesting path: " << encoded_path << std::endl;

    // Send HTTP GET request
    asio::streambuf request_buf;
    std::ostream request_stream(&request_buf);
    request_stream << "GET " << encoded_path << " HTTP/1.1\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n\r\n";

    asio::write(socket, request_buf);

    // Read status line
    asio::streambuf response_buf;
    asio::read_until(socket, response_buf, "\r\n");

    std::istream response_stream(&response_buf);
    std::string http_version;
    response_stream >> http_version;
    response_stream >> res.status_code;
    std::getline(response_stream, res.status_message);

    if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
        CASPAR_THROW_EXCEPTION(io_error() << msg_info("Invalid HTTP response"));
    }

    if (res.status_code < 200 || res.status_code >= 300) {
        CASPAR_THROW_EXCEPTION(io_error() << msg_info("HTTP request failed with status " + std::to_string(res.status_code)));
    }

    // Read headers
    asio::read_until(socket, response_buf, "\r\n\r\n");
    std::string header;
    while (std::getline(response_stream, header) && header != "\r") {
        // optional: store headers in res.headers
    }

    // Read body
    std::ostringstream body;
    if (response_buf.size() > 0)
        body << &response_buf;

    while (asio::read(socket, response_buf, asio::transfer_at_least(1), error)) {
        body << &response_buf;
    }

    if (error != asio::error::eof) {
        CASPAR_THROW_EXCEPTION(io_error() << msg_info(error.message()));
    }

    res.body = body.str();
    return res;
}

}} // namespace caspar::http
