/*
 * libetherstream.listener.hpp
 *
 *  Created on: 05.07.2017
 *  Author: 	Luca Lindhorst <info@lucalindhorst.de>
 */

#pragma once

#include <string>

#include "libetherstream.packet.hpp"
#include "libetherstream.socket.hpp"
#include "libetherstream.connection.hpp"

namespace ethstream{

class Listener : private Socket{
private:
	uint16_t service;
	std::string iface;
public:
	Listener(std::string iface, uint16_t service): Socket(iface), service(service), iface(iface){
	}
	ServerConnection* listen(){
		mac_t src;
		auto pkt = recvPkt(src);
		if(pkt && pkt->type == tCONNECT){
			CONNECT* cpkt = (CONNECT*)(pkt.get());
			if(service == cpkt->service)
				return new ServerConnection(iface, src, pkt->connection);
		}
		return nullptr;
	}
	mac_t getLMac(){
		return Socket::getLMac();
	};
};

};
