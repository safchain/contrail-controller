/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_CONNECTION_H_
#define BFD_CONNECTION_H_

#include <boost/asio/ip/address.hpp>

namespace BFD {
class ControlPacket;

class Connection {
 public:
    virtual void SendPacket(const boost::asio::ip::address &dstAddr, const ControlPacket *packet) = 0;
    virtual ~Connection() {}
};

}  // namespace BFD

#endif /* BFD_CONNECTION_H_ */
