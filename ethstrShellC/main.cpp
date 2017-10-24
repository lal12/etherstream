/*
 * main.cpp
 *
 *  Created on: 11.07.2017
 *      Author: root
 */

#include "getopt_pp.hpp"

#include <iostream>
#include <string>
#include "../libetherstream/libetherstream.hpp"
#include <thread>
#include <list>
extern "C"{
#include <termios.h>
#include <fcntl.h>
#include <ncurses.h>
}
#define SERVICE_SHELL 1

using namespace GetOpt;
using namespace std;
using namespace ethstream;


static struct termios orig_termios;  /* TERMinal I/O Structure */
static int ttyfd = STDIN_FILENO;     /* STDIN_FILENO is 0 by default */


bool tty_reset(void){
	/* flush and reset */
	if (tcsetattr(ttyfd,TCSAFLUSH,&orig_termios) < 0)
		return false;
	return true;
}

bool tty_raw(void){
	struct termios raw;
	raw = orig_termios;  /* copy original and then modify below */

	/* input modes - clear indicated ones giving: no break, no CR to NL,
	   no parity check, no strip char, no start/stop output (sic) control */
	//raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	/* output modes - clear giving: no post processing such as NL to CR+NL */
	//raw.c_oflag &= ~(OPOST);

	/* control modes - set 8 bit chars */
	//raw.c_cflag |= (CS8);

	/* local modes - clear giving: echoing off, canonical off (no erase with
	   backspace, ^U,...),  no extended functions, no signal chars (^Z,^C) */
	//raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	/* control chars - set return condition: min number of bytes and timer */
	//raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 8; /* after a byte or .8 seconds */

	/* put terminal in raw mode after flushing */
	cfmakeraw(&raw);
	if (tcsetattr(ttyfd,TCSAFLUSH,&raw) < 0)
		return false;
	return true;
}

int main(int argc, char **argv) {
	GetOpt_pp ops(argc, argv);
	if(ops >> OptionPresent('h', "help")){
		cout << "Shell over etherstream" << endl;
		cout << "Usage:" << endl;
		cout << "\t--help    -h                  Show this help" << endl;
		cout << "\t--version -v  --build  -b     Show build info" << endl;
		cout << "\t--dest    -d  mac             Connect to given host (e.g. 12:34:56:78:9A:BC)" << endl;
		cout << "\t--iface   -i  interface       Use specified interface" << endl;
		cout << "\t You have to specify the interface and the destination!" << endl;
		return (0);
	}
	if(ops >> OptionPresent('b', "build") || ops >> OptionPresent('v', "version")){
		cout << "Build on " << __DATE__ << " " << __TIME__ << endl;
		return 0;
	}
	string macstr, iface;
	if(!(ops >> Option('i',"iface", iface))){
		cerr << "Interface was not specified!" << endl;
		return -1;
	}
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
	if(!isatty(ttyfd)){
		cerr << "Shell has to be opened on a tty!" << endl;
		return -1;
	}
	cout << "Connecting to " << mac << " ..."  << endl;
	try{
		Client c(iface, mac, ETHSTR_SERVICE_SHELL);
		while(!c.connected())
			c.work();
		cout << "Connected." << endl;
		if (tcgetattr(ttyfd,&orig_termios) < 0){
			cerr << "Cannot get tty settings!" << endl;
			return -1;
		}
		if (atexit([](){tty_reset();}) != 0){
			cerr << "Cannot get tty settings!" << endl;
			return -1;
		}
		//TODO: add signal handling
		if(!tty_raw()){
			cerr << "Cannot configure tty!" << endl;
			return -1;
		}
		cout << endl;
		int flags = fcntl(ttyfd, F_GETFL, 0);
		fcntl(ttyfd, F_SETFL, flags | O_NONBLOCK);
		while(c.connected()){
			c.work();
			char buf[1000];
			uint32_t len = c.read(buf, 1000);
			if( len > 0 ){
				cout << std::string(buf, len);
				cout.flush();
			}
			int ret;
			string inp;
			while( (ret = read(ttyfd, buf, 1000)) > 0 ){
				inp += string(buf, ret);
			}
			c.write(inp.c_str(), inp.length());
			c.work();
		}
	}catch(std::unique_ptr<std::runtime_error>& e){
		cerr << e->what() << endl;
		return -1;
	}
}
