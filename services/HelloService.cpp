// ---------------------------------------------------------------------
// pion:  a Boost C++ framework for building lightweight HTTP interfaces
// ---------------------------------------------------------------------
// Copyright (C) 2007-2012 Cloudmeter, Inc.  (http://www.cloudmeter.com)
//
// Distributed under the Boost Software License, Version 1.0.
// See http://www.boost.org/LICENSE_1_0.txt
//

#include "HelloService.hpp"
#include <pion/http/response_writer.hpp>

using namespace pion;

namespace pion {        // begin namespace pion
namespace plugins {     // begin namespace plugins

    
// HelloService member functions

/// handles requests for HelloService
void HelloService::operator()(http::request_ptr& http_request_ptr, tcp::connection_ptr& tcp_conn)
{
    static const std::string HELLO_HTML = "<html><body>Hello World!</body></html>";
    http::response_writer_ptr writer(http::response_writer::create(tcp_conn, *http_request_ptr,
                                                            boost::bind(&tcp::connection::finish, tcp_conn)));
    writer->writeNoCopy(HELLO_HTML);
    writer->writeNoCopy(http::types::STRING_CRLF);
    writer->writeNoCopy(http::types::STRING_CRLF);
    writer->send();
}


}   // end namespace plugins
}   // end namespace pion


/// creates new HelloService objects
extern "C" PION_API pion::plugins::HelloService *pion_create_HelloService(void)
{
    return new pion::plugins::HelloService();
}

/// destroys HelloService objects
extern "C" PION_API void pion_destroy_HelloService(pion::plugins::HelloService *service_ptr)
{
    delete service_ptr;
}