/*
 * libetherstream.connection.hpp
 *
 *  Created on: 05.07.2017
 *  Author: 	Luca Lindhorst <info@lucalindhorst.de>
 */

#pragma once

#include <vector>
#include <chrono>
#include <cstring>
#include <string>

#include "libetherstream.packet.hpp"
#include "libetherstream.socket.hpp"

namespace ethstream{


template<bool isClient>
class WriteConnection: public virtual ConnectedSocket{
private:
	bool waitingACK = false;
	std::vector<char> out;
	std::chrono::time_point<std::chrono::system_clock> lastSent;
protected:
	DATA* lastPkt = nullptr;
	WriteConnection(std::string iface, mac_t remoteMac, uint32_t connection):
		ConnectedSocket(iface, remoteMac, connection){
	}
public:
	void write(const char* data, uint32_t len){
		if(!data){
			//TODO: throw error
			return;
		}
		if(len >= 1){
			out.insert(out.end(), &data[0], &data[len]);
		}
	};
	void write(std::string data){
		write(data.c_str(), (uint32_t)data.length());
	}
	void write(const char* data){
		write(data, (uint32_t)strlen(data));
	}
	void handlePacket(const ACK* apkt, uint16_t pktSize){
		if(lastPkt){
			if(waitingACK){
				if((uint16_t)apkt->errflags.val == 0){
					if(lastPkt->pktNo == apkt->pktNo){
						waitingACK = false;
						out.erase(out.begin(), out.begin()+lastPkt->length);
					}
				}else{ // Resend packet
					//TODO: maybe rebuilt packet, instead of just resending
					lastPkt->sentCount++;
					sendPacket(lastPkt, sizeof(DATA)+lastPkt->length);
					lastSent = std::chrono::system_clock::now();
				}
			}else{}
		}
	}
	void work(){
		if(waitingACK){
			using namespace std::chrono_literals;
			if(std::chrono::system_clock::now() - lastSent > 1s){
				lastPkt->sentCount++;
				sendPacket(lastPkt, sizeof(DATA)+lastPkt->length);
				lastSent = std::chrono::system_clock::now();
			}
		}else{
			if(out.size()){
				uint16_t pktNo = lastPkt ? lastPkt->pktNo + 1 : 1;
				if(REALPKTNO(pktNo) == 0)
					pktNo = 1;
				if(isClient)
					pktNo = CLIENTBIT_CLEAR(pktNo);
				else
					pktNo = SERVERBIT_SET(pktNo);
				delete[] lastPkt;
				int sendSize = std::min((uint32_t)out.size(), maxDataLen);
				lastPkt = (DATA*)new char[sizeof(DATA)+sendSize];
				lastPkt->pktNo = pktNo;
				lastPkt->type = tDATA;
				lastPkt->length = sendSize;
				lastPkt->checksum = fletcher16(sendSize, out.data());
				lastPkt->sentCount = 1;
				lastPkt->connection = connection;
				std::memcpy(lastPkt->data, out.data(), sendSize);
				sendPacket(lastPkt, sizeof(DATA)+sendSize);
				lastSent = std::chrono::system_clock::now();
				waitingACK = true;
			}
		}
	}

};

template<bool isClient>
class ReadConnection: public virtual ConnectedSocket{
private:
	bool noDataYet = true;
	std::vector<char> in;
	ACK lastAPkt;
	std::chrono::time_point<std::chrono::system_clock> lastAckSent;
protected:
	ReadConnection(std::string iface, mac_t remoteMac, uint32_t connection):
		ConnectedSocket(iface, remoteMac, connection){
		lastAPkt.pktNo = 0;
	}
	void handlePacket(const DATA* dpkt, uint16_t pktSize){
		if(!isClient == SERVERBIT_ISSET(dpkt->pktNo))
			return;
		if(dpkt->pktNo == lastAPkt.pktNo && !noDataYet){ // Resend ack
			lastAPkt.receivedCount++;
			sendPacket(&lastAPkt, sizeof(ACK));
		}else{
			lastAPkt.errflags = {0};
			uint16_t expectedNo = REALPKTNO(lastAPkt.pktNo)+1;
			expectedNo = expectedNo ? expectedNo : 1; // Prevent zero
			if(expectedNo != REALPKTNO(dpkt->pktNo)){ //Packet no out of order
				lastAPkt.errflags.bits.pkgOrderError = 1;
				lastAPkt.errflags.bits.pkgIgnored = 1;
				std::cerr << "Packet out of order!" << std::endl;
			}else if(fletcher16(dpkt->length, (void*)dpkt->data) == dpkt->checksum){ // Packet OK!
				noDataYet = false;
				in.insert(in.end(), &dpkt->data[0], &dpkt->data[dpkt->length]);
			}else{ // Checksum error
				lastAPkt.errflags.bits.chksumErr = 1;
				lastAPkt.errflags.bits.pkgIgnored = 1;
				std::cerr << "Checksum error!" << std::endl;
			}
			// TODO: Check if pkt is large enough
			lastAPkt.connection = connection;
			lastAPkt.receivedCount = 1;
			lastAPkt.pktNo = dpkt->pktNo;
			sendPacket(&lastAPkt, sizeof(ACK));
		}
	}
public:
	uint32_t read(char* buf, uint32_t len){
		uint32_t realSz = std::min(len, (uint32_t)in.size());
		std::memcpy(buf, in.data(), realSz);
		in.erase(in.begin(), in.begin()+realSz);
		return realSz;
	};
};

template<bool isClient>
class ConnectionBase: public ReadConnection<isClient>, public WriteConnection<isClient>{
	friend class Listener;
protected:
	bool isConnected = false;
	bool connectionClosed = false;
	ConnectionBase(std::string iface, mac_t remoteMac, uint32_t connection):
		ConnectedSocket(iface, remoteMac, connection),
		ReadConnection<isClient>(iface, remoteMac, connection),
		WriteConnection<isClient>(iface, remoteMac, connection){
	}
	void handlePacket(const PktBase* pkt, uint16_t pktSize){
		if(pkt->type == tACK){
			WriteConnection<isClient>::handlePacket((ACK*)pkt, pktSize);
		}else if(pkt->type == tDATA){
			ReadConnection<isClient>::handlePacket((DATA*)pkt, pktSize);
		}else if(pkt->type == tCONNECT){
			//TODO: error
		}else if(pkt->type == tCLOSE){
			/*ACK apkt;
			apkt.type = tACK;
			apkt.connection = connection;
			uint16_t pktNo = lastPkt ? lastPkt->pktNo + 1 : 1;
			if(REALPKTNO(pktNo) == 0)
				pktNo = 1;
			if(isClient)
				pktNo = CLIENTBIT_CLEAR(pktNo);
			else
				pktNo = SERVERBIT_SET(pktNo);
			apkt.pktNo = pktNo;
			apkt.receivedCount = 1;
			apkt.errflags = 0;
			sendPacket(&apkt, sizeof(ACK));*/
			isConnected = false;
			connectionClosed = true;
		}
	}
public:
	bool connected(){
		return isConnected;
	}
	void close(){
		if(isConnected){
			CLOSE closepkt;
			closepkt.type = tCLOSE;
			closepkt.connection = ConnectedSocket::connection;
			uint16_t pktNo = WriteConnection<isClient>::lastPkt ? WriteConnection<isClient>::lastPkt->pktNo + 1 : 1;
			if(REALPKTNO(pktNo) == 0)
				pktNo = 1;
			if(isClient)
				pktNo = CLIENTBIT_CLEAR(pktNo);
			else
				pktNo = SERVERBIT_SET(pktNo);
			closepkt.pktNo = pktNo;
			ReadConnection<isClient>::sendPacket((void*)&closepkt, sizeof(CLOSE));
			isConnected = false;
			connectionClosed = true;
		}
	}
};


class ServerConnection : public ConnectionBase<false>{
friend class Listener;
private:
	uint8_t receivedCONNECTs = 1;
	ServerConnection(std::string iface, mac_t remoteMac, uint32_t connection):
		ConnectedSocket(iface, remoteMac, connection),
		ConnectionBase(iface, remoteMac, connection){
		ACK apkt;
		apkt.type = tACK;
		apkt.connection = connection;
		apkt.pktNo = 0;
		apkt.receivedCount = receivedCONNECTs;
		sendPacket(&apkt, sizeof(ACK));
		isConnected = true;
	}
public:
	void work(bool wait = false){
		if(!connectionClosed){
			auto pkt = recvPkt(wait);
			if(pkt){
				if(pkt->type == tCONNECT){
					ACK apkt;
					apkt.type = tACK;
					apkt.connection = connection;
					apkt.pktNo = 0;
					apkt.receivedCount = ++receivedCONNECTs; //TODO: increase received
					sendPacket(&apkt, sizeof(ACK));
				}else
					handlePacket(pkt.get(), 0); //TODO: add real size
			}
			if(isConnected)
				WriteConnection::work();
		}
	}
};

class Client: public ConnectionBase<true>{
private:
	CONNECT cpkt;
	std::chrono::time_point<std::chrono::system_clock> sent;
	uint32_t rndConnection(){
		//TODO: implement
		return 123;
	}
public:
	Client(std::string iface, mac_t remoteMac, uint16_t service):
		ConnectedSocket(iface, remoteMac, rndConnection()),
		ConnectionBase(iface, remoteMac, connection){
		cpkt.connection = connection;
		cpkt.service = service;
		cpkt.type = tCONNECT;
		cpkt.sentCount = 1;
		sendPacket(&cpkt, sizeof(CONNECT));
		sent = std::chrono::system_clock::now();
	};
	void work(){
		if(!connectionClosed){
			auto pkt = recvPkt();
			if(pkt){
				if(!isConnected){
					if(pkt->type == tACK && pkt->connection == connection && pkt->pktNo == 0){
						isConnected = true;
					}
				}else{
					handlePacket(pkt.get(), 0); //TODO: add real size
				}
			}
			if(!isConnected){
				using namespace std::chrono_literals;
				if(std::chrono::system_clock::now() - sent > 2s){
					cpkt.sentCount++;
					sendPacket(&cpkt, sizeof(CONNECT));
					sent = std::chrono::system_clock::now();
				}
			}else{
				WriteConnection::work();
			}
		}
	}
};

};
