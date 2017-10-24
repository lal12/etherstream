/*
 * main.cpp
 *
 *  Created on: 25.09.2017
 *      Author: root
 */

#include <iostream>
#include <string>
#include "../libetherstream/libetherstream.hpp"
#include "getopt_pp.hpp"
#include <sys/inotify.h>

using namespace std;
using namespace GetOpt;
using namespace ethstream;

bool readData(Client* c, char* buf, int len, int tmout){
	int sz = 0;
	while(sz<len){//TODO: check for tmout
		c->work();
		sz += c->read(buf+sz, len-sz);
	}
	return true;
}

struct FTResult{
	uint8_t res;
	uint32_t size;
};

bool put(Client& c, string local, string remote){
	ifstream ifile(local);
	if(!ifile.is_open()){
		cerr << "Could not open file: " << strerror(errno) << endl;
		return false;
	}else{
		c.write("put\n", 4);
		c.write(remote.c_str(), remote.length());
		c.write("\n", 1);
		c.work();
		FTResult res;
		readData(&c, (char*)&res, sizeof(res), 0);
		if(!res.res){
			if(res.size){
				char* buf = new char[res.size];
				readData(&c, buf, res.size, 2000);
				cerr << "Error occured: " << std::string(buf, res.size) << endl;
				delete[] buf;
			}else{
				cerr << "Unknown error occured!" << endl;
			}
			return false;
		}
		ifile.seekg(0, ifstream::end);
		uint32_t sz = ifile.tellg();
		c.write((char*)&sz, 4);
		ifile.seekg(0, ifstream::beg);
		for(unsigned offs=0;offs<sz;offs+=1000){
			char buf[1000];
			int r = ifile.readsome(buf, 1000);
			c.write((char*)buf, r);
			c.work();
		};
	}
	return true;
}

bool get(Client& c, string remote, string local){
	ofstream ofile(local);
	if(!ofile.is_open()){
		cerr << "Cannot open local-file: " << local << endl;
		return false;
	}
	c.write("get\n", 4);
	c.write(remote.data(), remote.length());
	c.write("\n", 1);
	c.work();
	FTResult res;
	readData(&c, (char*)&res, sizeof(res), 0);
	if(!res.res){
		if(res.size){
			char* buf = new char[res.size];
			readData(&c, buf, res.size, 2000);
			cerr << "Error occured: " << std::string(buf, res.size) << endl;
			delete[] buf;
		}else{
			cerr << "Unknown error occured!" << endl;
		}
		return false;
	}
	unsigned int got = 0;
	while(got < res.size){ // Check for timeout
		char buf[1000];
		int r = c.read(buf, 1000);
		ofile.write(buf, r);
		got += r;
	}
	return true;
}

bool del(Client& c, string remote){
	c.write("del\n", 4);
	c.write(remote.data(), remote.length());
	c.write("\n", 1);
	c.work();
	FTResult res;
	readData(&c, (char*)&res, sizeof(res), 0);
	if(!res.res){
		if(res.size){
			char* buf = new char[res.size];
			readData(&c, buf, res.size, 2000);
			cerr << "Error occured: " << std::string(buf, res.size) << endl;
			delete[] buf;
		}else{
			cerr << "Unknown error occured!" << endl;
		}
		return false;
	}
	return true;
}

void watch_put(Client& c, string local, string remote){

	int fd = inotify_init(); //(IN_NONBLOCK);
	if(fd <= 0){
		cerr << "Error on inotify_init(): " << strerror(errno) << endl;
		return;
	}
	cout << "a " << local << endl;
	if(inotify_add_watch(fd, local.c_str(), IN_ALL_EVENTS) < 0){
		cerr << "Error on inotify_add_watch(): " << strerror(errno) << endl;
		return;
	}
	while(true){
		cout << "b" << endl;
		const unsigned bufsz = (sizeof(struct inotify_event))*1024;
		char buf[bufsz];
		int length = read(fd, buf, bufsz);
		if(length < 0){
			cerr << "Could not read inotify fd!" << endl;
			close(fd);
			return;
		}
		cout << "c " << length << endl;
		unsigned i=0;
		inotify_event* evt = (inotify_event*)&buf[i];
		while(i+sizeof(inotify_event) <= length){
			cout << "d" << endl;
			evt = (inotify_event*)&buf[i];
			cout << "len " << evt->len << endl;
			if ( evt->wd == -1 ) {
				cerr << "Overflow wd" << endl;
			}
			if ( evt->mask & IN_Q_OVERFLOW ) {
				cerr << "Overflow mask" << endl;
			}
			if(evt->len || 1){
				cout << "e" << endl;
				if(evt->mask & (IN_CREATE | IN_ISDIR) ){
					cerr << "Created dir, but can only watch files!" << endl;
					return;
				}else if(evt->mask & (IN_CREATE | IN_MODIFY)){
					cout << "f" << endl;
					if(put(c, local, remote)){
						cout << "Successfully put file " << local << endl;
					}else{
						cerr << "Failed to put file " << local << endl;
					}
				}else if(evt->mask & (IN_DELETE)){
					cout << "g" << endl;
					if(del(c, remote)){
						cout << "Successfully del file " << remote << endl;
					}else{
						cerr << "Failed to del file " << remote << endl;
					}
				}else if(evt->mask & (IN_IGNORED)){
					close(fd);
					cout << "Reinitialiazing..." << endl;
					fd = inotify_init(); //(IN_NONBLOCK);
					if(fd <= 0){
						cerr << "Error on inotify_init(): " << strerror(errno) << endl;
						return;
					}
					if(inotify_add_watch(fd, local.c_str(), IN_ALL_EVENTS) < 0){
						cerr << "Error on inotify_add_watch(): " << strerror(errno) << endl;
						return;
					}
				}else printf("%x\n", evt->mask);
			}
			i += sizeof(inotify_event) + evt->len;
		}
	}
}

int main(int argc, char **argv) {
	GetOpt_pp ops(argc, argv);
	if(ops >> OptionPresent('h', "help")){
		cout << "Manage files over etherstream" << endl;
		cout << "Usage: esftc OPTION COMMAND" << endl;
		cout << "OPTIONS:" << endl;
		cout << "\t--help    -h                  		Show this help" << endl;
		cout << "\t--version -v  --build  -b     		Show build info" << endl;
		cout << "\t--dest    -d  mac             		Connect to given host (e.g. 12:34:56:78:9A:BC)" << endl;
		cout << "\t--iface   -i  interface       		Use specified interface" << endl << endl;
		cout << "COMMANDS:" << endl;
		cout << "\tput local-path remote-path    		Copy local-path onto remote as remote-path" << endl;
		cout << "\tget remote-path local-path    		Get remote-path onto local as local-path" << endl;
		cout << "\twatch-put local-path remote-path		Watch local file and put/del it on remote accordingly" << endl;
		cout << "\tdel remote-path				   		Delete remote-path" << endl;
		return (0);
	}
	if(ops >> OptionPresent('b', "build") || ops >> OptionPresent('v', "version")){
		cout << "Build on " << __DATE__ << " " << __TIME__ << endl;
		return 0;
	}
	string shell, iface;
	if(!(ops >> Option('i',"iface", iface))){
		cerr << "Interface was not specified!" << endl;
		return -1;
	}
	string macstr;
	if(!(ops >> Option('d',"dest", macstr))){
		cerr << "Destination mac was not specified!" << endl;
		return -1;
	}
	if(macstr.length() != 17){
		cerr << "Invalid MAC specified (invalid length)!" << endl;
		return -1;
	}
	mac_t mac;
	for(int i = 0; i<6; i++){
		if(i && macstr[i*2+i-1] != ':'){
			cerr << "Invalid MAC specified, expected : got " << macstr[i*2+i-1] <<  " !" << endl;
			return -1;
		}
		try{
			mac.bytes[i] = stoul(macstr.substr(i*2+i, 2), nullptr, 16) & 0xFF;
		}catch(...){
			cerr << "Invalid MAC specified, invalid hex: " << macstr.substr(i*2+i, 2) << " !" << endl;
			return -1;
		}
	}
	vector<string> args;
	ops >> GlobalOption(args);
	if(args.size() == 0){
		cerr << "No command was given!" << endl;
		return -1;
	}
	//TODO: add more commands
	if(args[0] == "put"){
		if(args.size() != 3){
			cerr << "Invalid number of parameters!" << endl;
			return -1;
		}
		Client c(iface, mac, ETHSTR_SERVICE_FILE_TRANSFER);
		while(!c.connected()) // TODO: Add tmout
			c.work();
		if(put(c, args[1], args[2])){
			cout << "Successfully put file " << args[1] << endl;
		}else{
			cerr << "Failed to put file " << args[1] << endl;
		}
		c.close();
	}else if(args[0] == "get"){
		if(args.size() != 3){
			cerr << "Invalid number of parameters!" << endl;
			return -1;
		}
		Client c(iface, mac, ETHSTR_SERVICE_FILE_TRANSFER);
		while(!c.connected()) // TODO: Add tmout
			c.work();
		if(get(c, args[1], args[2])){
			cout << "Successfully get file " << args[1] << endl;
		}else{
			cerr << "Failed to get file " << args[1] << endl;
		}
		c.close();
	}else if(args[0] == "watch-put"){
		if(args.size() != 3){
			cerr << "Invalid number of parameters!" << endl;
			return -1;
		}
		Client c(iface, mac, ETHSTR_SERVICE_FILE_TRANSFER);
		while(!c.connected()) // TODO: Add tmout
			c.work();
		watch_put(c, args[1], args[2]);
		c.close();
	}else if(args[0] == "del"){
		if(args.size() != 2){
			cerr << "Invalid number of parameters!" << endl;
			return -1;
		}
		Client c(iface, mac, ETHSTR_SERVICE_FILE_TRANSFER);
		while(!c.connected()) // TODO: Add tmout
			c.work();
		if(del(c, args[1])){
			cout << "Successfully del file " << args[1] << endl;
		}else{
			cerr << "Failed to del file " << args[1] << endl;
		}
	}else{
		cerr << "Invalid command" << args[0] << endl;
		return -1;
	}
}
