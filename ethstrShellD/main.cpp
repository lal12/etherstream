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
}

#define SERVICE_SHELL 1

using namespace GetOpt;
using namespace std;
using namespace ethstream;

union pipefd{
	struct{
		int rfd;
		int wfd;
	} obj;
	int arr[2];
};

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

pid_t popen_rw(std::string command, pipefd& com){
	pipefd p_stdin, p_stdout;
	pid_t pid;

	if (pipe(p_stdin.arr) != 0 || pipe(p_stdout.arr) != 0)
		return -1;
	pid = fork();
	if (pid < 0){ // Error
		close(p_stdin.obj.rfd);
		close(p_stdout.obj.wfd);
		return -1;
	}else if (pid == 0){ // Child
		cout << "Starte (/usr/bin/script -qfc): " << command << endl;
		close(p_stdin.obj.wfd);
		dup2(p_stdin.obj.rfd, STDIN_FILENO);
		close(p_stdout.obj.rfd);
		dup2(p_stdout.obj.wfd, STDOUT_FILENO);
		dup2(p_stdout.obj.wfd, STDERR_FILENO);
		execl("/usr/bin/script", "script", "-qfc", command.c_str(), NULL);
		//execl("/bin/sh", "sh", NULL);
		exit(-1);
	}else{ // Parent
		close(p_stdin.obj.rfd);
		close(p_stdout.obj.wfd);
		com.obj.rfd = p_stdout.obj.rfd;
		com.obj.wfd = p_stdin.obj.wfd;
	}
	return pid;
}


int main(int argc, char **argv) {
	GetOpt_pp ops(argc, argv);
	if(ops >> OptionPresent('h', "help")){
		cout << "Shell over etherstream" << endl;
		cout << "Usage: ethstrshelld -s shell -i iface | -h | -v | -b" << endl;
		cout << "\t--help    -h                  Show this help" << endl;
		cout << "\t--version -v  --build  -b     Show build info" << endl;
		cout << "\t--shell   -s  shellpath       Start daemon with given shell, " << endl;
		cout << "\t                              default 'bash -i'" << endl;
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
	if(!(ops >> Option('s',"shell", shell))){
		shell = "bash -i";
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

	list<thread*> threads;
	//TODO: add Signal handler
	try{
		Listener l(iface, SERVICE_SHELL);
		while(run){
			ServerConnection* conn = l.listen();
			if(conn){
				std::cout << "New connection from " << conn->getRMac() << "\n";
				thread* t = new thread([conn, shell, t, run](){
					while(!conn->connected())
						conn->work();
					conn->work();
					//TODO: maybe replace by fork to have finer control
					conn->write("Welcome to EtherStreamShell!\n");
					conn->work();
					pipefd f;
					pid_t pid = popen_rw(shell, f);
					if(pid <= 0){
						cerr << "Error: " << strerror(errno) << endl;
						return -1;
					}
					cout << "Opened: " << shell << " pid: " << pid << endl;
					int flags = fcntl(f.obj.rfd, F_GETFL, 0);
					fcntl(f.obj.rfd, F_SETFL, flags | O_NONBLOCK);
					//TODO: rewrite to support select

					// While running and shell running and connected
					while(run && waitpid(pid, NULL, WNOHANG) == 0 && conn->connected()){
						conn->work();
						char buf[1000];
						int r = read(f.obj.rfd, buf, 1000);
						if(r > 0){
							conn->write(buf, r);
						}
						r = conn->read(buf, 1000);
						if(r > 0){
							write(f.obj.wfd, buf, r);
						}
					}
					conn->close();
					std::cout << "Connection to " << conn->getRMac() << " closed!\n";
					return 0;
				});
				threads.push_back(t);
			}
		}
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
