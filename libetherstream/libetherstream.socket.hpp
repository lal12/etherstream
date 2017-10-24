/*
 * libetherstream.socket.hpp
 *
 *  Created on: 05.07.2017
 *  Author: 	Luca Lindhorst <info@lucalindhorst.de>
 */

#pragma once

extern "C"{
#include <sys/socket.h>
#include <linux/filter.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <netinet/in.h>
}
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <memory>


#include "libetherstream.packet.hpp"

namespace ethstream{
class Socket{
private:
	int ifIndex = -1;
	mac_t localMac = {{0}};
	void setIface(std::string iface){
		struct ifaddrs *addrs,*tmp;
		getifaddrs(&addrs);
		tmp = addrs;
		while (tmp){
			if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET && iface == tmp->ifa_name){
				std::memcpy(localMac.bytes, &tmp->ifa_addr->sa_data[10], 6);
				break;
			}
			tmp = tmp->ifa_next;
		}
		freeifaddrs(addrs);
		ifIndex = if_nametoindex(iface.c_str());
		if(ifIndex == 0)
			throw std::unique_ptr<std::runtime_error>(new std::runtime_error("Invalid interface (" + iface + ") specified: "+std::string(strerror(errno))));
	};
protected:
	int sock = -1;
	mac_t getLMac(){
		return localMac;
	};
	Socket(std::string iface){
		sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ETHSTREAM));
		if(sock == -1){
			//TODO: Error handling
			//std::cout << "socket error: " << strerror(errno) << " (" << errno << ")" << std::endl;
			//exit(-errno);
			throw "Error in opening Socket";
		}
		setIface(iface);
		const int minPktSz = sizeof(struct ethhdr) + sizeof(struct CONNECT);
		// Docs: http://www.gsp.com/cgi-bin/man.cgi?topic=bpf
		struct sock_filter filter[] = { // A: Accumulator, P: packet data, X: Register, k: passed parameter
			//BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 12), // A <- P[12:2] (2byte)
			//BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K,ETH_P_ETHSTREAM,0,4), // A == ETH_P_ETHSTREAM then jmp 0 else jmp 4 (ret 0)
			//TODO: check for max type
			BPF_STMT(BPF_LD+BPF_W+BPF_LEN, 0), // A <- pkt len
			//BPF_STMT(BPF_RET+BPF_A, 0), // return A -> accept whole packet
			BPF_JUMP(BPF_JMP+BPF_JGE+BPF_K,minPktSz-5,0,1), // A >= minPktSz then jmp 0 else jmp 1 (ret 0)
			BPF_STMT(BPF_RET+BPF_A, 0), // return A -> accept whole packet
			BPF_STMT(BPF_RET+BPF_K, 0), // accept 0 bytes -> drop packet
		};
		struct sock_fprog bpf = {
			.len = sizeof(filter)/sizeof(filter[0]),
			.filter = filter,
		};
		errno = 0;
		int ret = setsockopt(sock, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
		if (ret < 0){
			throw std::unique_ptr<std::runtime_error>(new std::runtime_error("Could not set bpf on socket: "+std::string(strerror(errno))));
		}
	}
	virtual ~Socket(){
		close(sock);
	}
	void sendPacket(mac_t dest, void* data, uint16_t length){
		struct iovec d = {reinterpret_cast<void*>(data), length};
		sendPacket(dest, &d, 1);
	}

	void sendPacket(mac_t dest, struct iovec data[], uint16_t length){
		// Build eth header
		struct ethhdr eh;
		std::memcpy(eh.h_dest, dest.bytes, ETH_ALEN);
		std::memcpy(eh.h_source, localMac.bytes, ETH_ALEN);
		eh.h_proto = htons(ETH_P_ETHSTREAM);
		// Build dest addr
		struct sockaddr_ll addr;
		addr.sll_family = PF_PACKET;
		addr.sll_protocol = ETH_P_ETHSTREAM;
		addr.sll_ifindex = ifIndex;
		addr.sll_halen = ETH_ALEN;
		std::memcpy(addr.sll_addr, dest.bytes, ETH_ALEN);
		// Build iovec buffer
		struct iovec* pktdata = new struct iovec[length+1];
		pktdata[0].iov_base = &eh;
		pktdata[0].iov_len = sizeof(eh);
		std::memcpy(&pktdata[1], data, length*sizeof(struct iovec));
		// Build sendmsg struct
		struct msghdr msg = {0};
		msg.msg_name = &addr;
		msg.msg_namelen = sizeof(addr);
		msg.msg_iov = pktdata;
		msg.msg_iovlen = length+1;
		// Send msg
		if(sendmsg(sock, &msg, MSG_DONTROUTE) == -1){
			//std::cout << "sendmsg error: " << strerror(errno) << " (" << errno << ")" << std::endl;
			//exit(-errno);
			//TODO: Error handling
			throw std::domain_error("Error sending packet: "+std::string(strerror(errno)));
		}
	}
	std::shared_ptr<PktBase> recvPkt(mac_t& src, bool wait = false){
		std::shared_ptr<PktBase> pkt(NULL, [](PktBase* pkt){
			delete[] pkt;
		});
		struct ethhdr* eth = (ethhdr*)new uint8_t[ETH_FRAME_LEN];
		int length = recv(sock, (void*)eth, ETH_FRAME_LEN, wait ? 0 : MSG_DONTWAIT);
		if (length == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
			throw std::unique_ptr<std::runtime_error>(new std::runtime_error("Error receiving packet!"));
		}else if(length > 0){
			//TODO: check validity of packet
			int ethstrLen = length - 14;
			PktBase* pktb = (PktBase*)new uint8_t[ethstrLen];
			pkt.reset(pktb);
			std::memcpy(pktb, &((uint8_t*)eth)[14] /* skip ethhdr */, ethstrLen);
			if(pktb->type > maxPktType){
				pkt = NULL;
			}else{
				std::memcpy(src.bytes, eth->h_source, ETH_ALEN);
			}
		}
		delete[] eth;
		return pkt;
	}
};

class ConnectedSocket : private Socket{
private:
	mac_t foreignMac;
public:
	mac_t getLMac(){
		return Socket::getLMac();
	};
	mac_t getRMac(){
		return foreignMac;
	};
protected:
	uint32_t connection;
	ConnectedSocket(std::string iface, mac_t dest, uint32_t connection): Socket(iface), foreignMac(dest), connection(connection){

	}
	void sendPacket(void* data, uint16_t length){
		Socket::sendPacket(foreignMac, data, length);
	}
	void sendPacket(struct iovec data[], uint16_t length){
		Socket::sendPacket(foreignMac, data, length);
	}
	std::shared_ptr<PktBase> recvPkt(bool wait = false){
		mac_t src;
		auto pkt = Socket::recvPkt(src, wait);
		if(std::memcmp((void*)src.bytes, (void*)foreignMac.bytes, sizeof(mac_t)) == 0)
			return pkt;
		else
			return nullptr;
	}
};

};
