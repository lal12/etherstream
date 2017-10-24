/*
 * libetherstream.packet.hpp
 *
 *  Created on: 05.07.2017
 *  Author: 	Luca Lindhorst <info@lucalindhorst.de>
 */

#pragma once

extern "C"{
#include <linux/if_ether.h>
}
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cstring>

namespace ethstream{

#define ETHSTR_SERVICE_SHELL 1
#define ETHSTR_SERVICE_FILE_TRANSFER 2

#define ETH_P_ETHSTREAM 0xFFF0

enum pktType{
	tCONNECT = 0,
	tDATA = 1,
	tACK = 2,
	tCLOSE = 3
};
const uint8_t maxPktType = tCLOSE;
#define SRVCLIBIT 0x8000
#define CLIENTBIT_CLEAR(x) ((x) & ~SRVCLIBIT)
#define SERVERBIT_SET(x) ((x) | SRVCLIBIT)
#define SERVERBIT_ISSET(x) (!!((x) & SRVCLIBIT))
#define REALPKTNO(x) CLIENTBIT_CLEAR(x)

struct __attribute__((__packed__)) PktBase{
	uint8_t type;
	uint32_t connection;
	uint16_t pktNo;
};

struct __attribute__((__packed__)) CONNECT{
	uint8_t type = tCONNECT;
	uint32_t connection;
	uint16_t pktNo = 0;
	uint8_t sentCount = 1;
	uint16_t service = 0;
	uint16_t flags = 0;
};

struct __attribute__((__packed__)) DATA{
	uint8_t type = tDATA;
	uint32_t connection;
	uint16_t pktNo;
	uint8_t sentCount = 1;
	uint16_t checksum;
	uint16_t length;
	char data[];
};
const uint32_t maxDataLen = ETH_FRAME_LEN - sizeof(struct DATA) - sizeof(ethhdr);


struct __attribute__((__packed__)) ACK{
	uint8_t type = tACK;
	uint32_t connection;
	uint16_t pktNo;
	uint8_t receivedCount;
	union{
		struct{
			uint16_t pkgIgnored: 1; // Pkg was ignored due to an error
			uint16_t connClosed: 1; // Connection was closed due to an error
			uint16_t unknownConn: 1; // Connection id is unknown
			uint16_t chksumErr: 1; // Checksum is wrong
			uint16_t connOpen: 1; // Connection already open (as answer to an CONNECT Request)
			uint16_t pkgOrderError: 1; // Packet has an out of order number
			uint16_t reserved: 10;
		}__attribute__((packed)) bits;
		uint16_t val = 0;
	}errflags;
};

struct __attribute__((__packed__)) CLOSE{
	uint8_t type = tCLOSE;
	uint32_t connection;
	uint16_t pktNo;
};

typedef struct {uint8_t bytes[ETH_ALEN] = {0};} mac_t;

std::ostream& operator<<(std::ostream& os, mac_t mac){
	uint8_t* b = mac.bytes;
	char buf[18];
	snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
	os << buf;
	return os;
}

uint16_t fletcher16(int len, void* data){
	uint8_t sum1 = 0;
	uint8_t sum2 = 0;
	uint8_t* d = (uint8_t*)data;
	for(int i=0; i<len; i++){
		sum1 += d[i];
		sum2 += sum1;
	}
	return (sum1 << 8) + sum2;
}

};
