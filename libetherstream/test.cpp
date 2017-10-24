/*
 * test.cpp
 *
 *  Created on: 28.06.2017
 *      Author: luca
 */

#include <iostream>
#include <thread>

#define ETHSTREAM_TEST
#include "libetherstream.connection.hpp"
#include "libetherstream.listener.hpp"

const char* iface = "wlp5s0";

using namespace ethstream;
	using namespace std;

int main(int argc, char **argv) {
	//ethstream::Socket sock(iface);
	//		ethstream::Connection* conn = sock.connect(sock.getMac(),33);
	//		return 0;
	/*while(true){
		ethstream::Socket sock(iface);
		ethstream::Connection* conn = sock.connect(sock.getMac(),33);
		sleep(5);
	}
	try{
		ethstream::Socket csock(iface);
		ethstream::Socket ssock(iface);
		ssock.startListen(33, [](ethstream::Connection* conn){
			std::cout << "Connection to service 33 started!" << std::endl;
		});
		sleep(5);
		ethstream::Connection* conn = csock.connect(sock.getMac(),33);
	}catch(std::unique_ptr<std::runtime_error>& e){
		std::cerr << e->what() << std::endl;
	}*/
	/*ethstream::CONNECT conn;
	conn.connection = 123;
	conn.flags = 0;
	conn.service = 1234;
	ethstream::mac_t dest = sock.localMAC;
	dest.bytes[0] = 2;
	sock.sendPacket(dest, (int8_t*)&conn, sizeof(conn));*/

	/*thread t([](){

	});*/

	if(argc > 1){ // Listen
		ethstream::Listener l(iface, 123);
		ServerConnection* conn = nullptr;
		while(!conn){
			conn = l.listen();
		}
		while(true){
			conn->work();
			char buf[1000];
			uint32_t len = conn->read(buf, 1000);
			if(len){
				std::cout << std::string(buf, len) << std::endl;
				conn->write(buf, len);
			}
		}
	}else{
		mac_t dest;
		{ // get local mac
			ethstream::Listener l(iface, 123);
			dest = l.getLMac();
		}
		ethstream::Client client(iface,dest,123);
		int i = 0;
		while(true){
			client.work();
			char buf[1000];
			uint32_t len = client.read(buf, 1000);
			if(len){
				std::cout << "<<<< " << std::string(buf, len) << "\n";
			}
			std::string send = "Hallo "+to_string(i++)+"\n";
			client.write(send);
			std::cout << ">>>> " << send << "\n";
		}
	}
}
