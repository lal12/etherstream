/*
 * main.cpp
 *
 *  Created on: 11.07.2017
 *      Author: root
 */

#include "getopt_pp.hpp"

#include <ncurses.h>
#include <iostream>
#include <string>
#include "../libetherstream/libetherstream.hpp"
#include <thread>
#include <list>
#include <atomic>

extern "C"{
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
}

#include "json.hpp"

using namespace GetOpt;
using namespace std;
using namespace ethstream;

std::atomic_bool run(true);

void handle_signal(int signal) {
    // Find out which signal we're handling
    switch (signal) {
        case SIGINT:
        case SIGKILL:
        case SIGTERM:
            run.store(false);
        break;
    }
}

bool readData(ServerConnection* c, char* buf, int len, int tmout){
	int sz = 0;
	while(run && sz<len){//TODO: check for tmout
		c->work();
		sz += c->read(buf+sz, len-sz);
	}
	return true;
}

string readString(ServerConnection* c, int tmout, char end = '\n'){
	char ch = 0;
	string str = "";
	do{//TODO: check for tmout
		c->work();
		if(c->read(&ch, 1) && ch != end)
			str += ch;
	}while(ch != end );
	return str;
}

struct FTResult{
	uint8_t res;
	uint32_t size;
};

void sendError(ServerConnection* c, string err = ""){
	struct FTResult res;
	res.res = 0;
	res.size = err.length();
	c->write((char*)&res, sizeof(res));
	c->write(err.data(), err.length());
	c->work();
}
void sendSuccess(ServerConnection* c, uint32_t sz = 0){
	struct FTResult res;
	res.res = 1;
	res.size = sz;
	c->write((char*)&res, sizeof(res));
	c->work();
}

void connectionHandler(ServerConnection* c){
	string cmd = "";
	while(run  && c->connected()){
		c->work();
		std::string cmd = readString(c, 0);
		std::string param = readString(c, 0);
		if(cmd == "ls"){
			DIR *dir = opendir(param.c_str());
			if(dir == nullptr){
				sendError(c, "Dir not found!");
			}else{
				struct dirent *dp;
				vector<struct dirent> list;
				while ((dp=readdir(dir)) != NULL) {
					list.push_back(*dp);
				}
				closedir(dir);
				sendSuccess(c, list.size()*sizeof(struct dirent));
				c->write((char*)list.data(), list.size()*sizeof(struct dirent));
			}
		}else if(cmd == "mkdir"){
			int res = mkdir(param.c_str(), 0) == 0;
			if(res)
				sendSuccess(c);
			else
				sendError(c, strerror(errno));
		}else if(cmd == "stat"){
			struct stat statdata;
			if(stat(param.c_str(), &statdata) == 0){
				sendSuccess(c, sizeof(struct stat));
				c->write((char*)&statdata, sizeof(statdata));
			}else{
				sendError(c, strerror(errno));
			}
		}else if(cmd == "del"){
			int res = unlink(param.c_str()) == 0;
			if(res)
				sendSuccess(c);
			else
				sendError(c, strerror(errno));
		}else if(cmd == "get"){
			ifstream ifile(param);
			if(!ifile.is_open()){ // Error
				sendError(c, strerror(errno));
			}else{
				ifile.seekg(0, ifile.end);
				int sz = ifile.tellg();
				ifile.seekg(0, ifile.beg);
				sendSuccess(c, sz);
				char buf[1000];
				while(ifile.tellg() < sz){
					int r = ifile.readsome(buf, 1000);
					c->write(buf, r);
				}
			}
		}else if(cmd == "put"){
			ofstream ofile(param);
			if(!ofile.is_open()){ // Error
				sendError(c, "Could not open file!");
			}else{
				sendSuccess(c);
				uint32_t sz;
				if(readData(c, (char*)&sz, 4, 0)){ // TODO: Check for tmout
					unsigned int got = 0;
					while(got < sz){ // TODO: Check for tmout
						char buf[1000];
						int r = c->read(buf, 1000);
						ofile.write(buf, r);
						got += r;
					}
					//TODO: error handling
					sendSuccess(c);
				}else{
					//TODO: Error handling
				}
			}
		}else{
			sendError(c, "Unknown command!");
			cerr << "Illegal op " << cmd << " " << param << endl;
		}
		c->work();
	}
	c->close();
}

int main(int argc, char **argv) {
	std::string dir;
	GetOpt_pp ops(argc, argv);
	if(ops >> OptionPresent('h', "help")){
		cout << "Server to manage files over etherstream" << endl;
		cout << "Usage: esftd -s shell -i iface | -h | -v | -b" << endl;
		cout << "\t--help    -h                  Show this help" << endl;
		cout << "\t--version -v  --build  -b     Show build info" << endl;
		cout << "\t--dir     -d  path	         File root, default '/'" << endl;
		cout << "\t--iface   -i  interface       Use specified interface" << endl;
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
	if(!(ops >> Option('d',"dir", dir))){
		dir = "/";
		cout << "dir was not set, default: /" << endl;
	}

	struct sigaction sa;
	sa.sa_handler = &handle_signal;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		cerr << "Error: cannot handle SIGINT" << endl;// Should not happen
		exit(-1);
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1) {
		cerr << "Error: cannot handle SIGTERM" << endl;// Should not happen
		exit(-1);
	}


	try{
		Listener l(iface, ETHSTR_SERVICE_FILE_TRANSFER);
		while(run){
			ServerConnection* conn = l.listen();
			if(conn){
				std::cout << "New connection from " << conn->getRMac() << "\n";
				pid_t pid = fork();
				if(pid == 0){ // Child
					if(chroot(dir.c_str()) == 0){
						connectionHandler(conn);
					}else{
						cerr << "Could not chroot: " << strerror(errno) << endl;
						conn->close();
					}
					exit(0);
				}else if(pid == -1){ // Error
					cerr << "Error in fork!" << endl;
				}else{ // Parent

				}
			}
		}
		while(wait(0) == 0); // Waits for all childs
	}catch(std::unique_ptr<std::runtime_error>& e){
		cerr << "Error occured: " << e->what() << endl;
		return -1;
	}catch(const char* msg){
		cerr << "Error occured: " << msg << endl;
		return -1;
	}catch(...){
		cerr << "Unknown error occured!" << endl;
		return -1;
	}
}
