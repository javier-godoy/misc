/**
Copyright (C) 2024 Roberto Javier Godoy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>
*/	
#include <iostream>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <random>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <tuple>
#include <vector>
#include <getopt.h>
using namespace std;

#define MAX_NAME_LEN 96
#define RECORD_LEN (32+MAX_NAME_LEN)

string ntoa(int in_addr) {
	unsigned char *b = (unsigned char *) &in_addr;
	ostringstream ss;
	ss<<(unsigned int)b[3]<<"."<<(unsigned int)b[2]<<"."<<(unsigned int)b[1]<<"."<<(unsigned int)b[0];
	return ss.str();
}

///(C) 2021 Martin Arvidssson CC-BY-SA
///https://stackoverflow.com/a/68194997/1297272
uint32_t aton( const string& ipv4Str )
{
	istringstream iss( ipv4Str );
	
	uint32_t ipv4 = 0;
	
	for( uint32_t i = 0; i < 4; ++i ) {
		uint32_t part;
		iss >> part;
		if ( iss.fail() || part > 255 ) {
			throw runtime_error( "Invalid IP address - Expected [0, 255]" );
		}
		
		// LSHIFT and OR all parts together with the first part as the MSB
		ipv4 |= part << ( 8 * ( 3 - i ) );
		
		// Check for delimiter except on last iteration
		if ( i != 3 ) {
			char delimiter;
			iss >> delimiter;
			if ( iss.fail() || delimiter != '.' ) {
				throw runtime_error( "Invalid IP address - Expected '.' delimiter" );
			}
		}
	}
	
	return ipv4;
}
///end of third party code

void pool_open(string filename, fstream &file) {
	file.open(filename,ios::binary|ios::in|ios::out);
	if (!file.is_open()) {
		file.open(filename,ios::binary|ios::out);
		file.close();	
		file.open(filename,ios::binary|ios::in|ios::out);
	}
	
	if (!file.is_open()) {
		throw runtime_error( "Error opening " + filename);		
	}	
}

size_t pool_size(fstream &file) {
	file.seekg(0, ios_base::end);
	size_t size = file.tellg()/RECORD_LEN;
	file.seekg(0);
	return size;	
}

void pool_print(string filename) {
	fstream file;
	pool_open(filename,file);	
	char buf[RECORD_LEN+1];
	buf[RECORD_LEN]=0;
	
	vector<pair<uint32_t,string>> v;
	
	size_t n = pool_size(file);
	for (size_t i=0;i<n;i++) {
		file.read(buf,RECORD_LEN);
		uint32_t addr = *(uint32_t*)buf;
		if (addr) {
			string name = buf+sizeof(uint32_t);
			v.push_back(make_pair(addr, name));
		}
	}
	sort(v.begin(),v.end());
	for (auto t:v) {
		cout << ntoa(t.first) << " " << t.second << endl;
	}
	
}

vector<uint32_t> pool_load(fstream &file) {
	char buf[RECORD_LEN];	
	size_t n = pool_size(file);
	vector<uint32_t> pool;
	pool.reserve(n);
	for (size_t i=0;i<n;i++) {
		file.read(buf,RECORD_LEN);
		uint32_t addr = *(uint32_t*)buf;
		if (addr) pool.push_back(addr);
	}
	return pool;
}

string pool_find(fstream &file, uint32_t addr) {
	char buf[RECORD_LEN+1];
	buf[RECORD_LEN]=0;
	size_t n = pool_size(file);
	for (size_t i=0;i<n;i++) {
		file.read(buf,RECORD_LEN);
		uint32_t _addr = *(uint32_t*)buf;
		if (addr == _addr) {
			file.seekp(file.tellg());
			file.seekp(-RECORD_LEN, ios_base::cur);
			return buf+4;
		}
	}
	return "";
}

uint32_t pool_find(fstream &file, const char* name) {
	char buf[RECORD_LEN];
	size_t n = pool_size(file);
	for (size_t i=0;i<n;i++) {
		file.read(buf,RECORD_LEN);
		uint32_t addr = *(uint32_t*)buf;
		if (addr && strncmp(name, buf+sizeof(uint32_t), MAX_NAME_LEN)==0) {
			file.seekp(file.tellg());
			file.seekp(-RECORD_LEN, ios_base::cur);
			return addr;
		}
	}
	return 0;
}

void pool_write(fstream &file, uint32_t addr, const char* name) {
	char buf[RECORD_LEN];
	memset(buf,0,RECORD_LEN);
	memcpy(buf,&addr,sizeof(uint32_t));
	memcpy(buf+sizeof(uint32_t), name,min(strlen(name),(size_t)MAX_NAME_LEN));
	file.write(buf,RECORD_LEN);
}

string pool_find(string filename, uint32_t addr) {
	fstream file;
	pool_open(filename,file);
	return pool_find(file, addr);
}

string pool_find(string filename, string name) {
	fstream file;
	pool_open(filename,file);
	return ntoa(pool_find(file, name.c_str()));	
}

string pool_request(string filename, string name, uint32_t addr) {
	fstream file;
	pool_open(filename,file);

	uint32_t found_addr = pool_find(file,name.c_str());
	if (found_addr) return ntoa(found_addr);
	
	if (pool_find(file, addr).empty()) {
		pool_write(file, addr, name.c_str());
		return ntoa(addr);
	} else {
		return ntoa(0);
	}
}

string pool_request(string filename, uint32_t begin, uint32_t end, string name) {
	fstream file;
	pool_open(filename,file);
	
	if (end<begin) {
		swap(begin,end);
	}
	
	uint32_t addr = pool_find(file,name.c_str());
	if (addr) return ntoa(addr);
	
	mt19937 rng(chrono::steady_clock::now().time_since_epoch().count());	
	
	uint32_t size = end-begin+1;
	
	vector<uint32_t> pool = pool_load(file);
	sort(pool.begin(), pool.end());
	uint32_t* a = new uint32_t[size];
	for (size_t i=0;i<size;i++) a[i]=i;
	shuffle(&a[0],&a[size], rng);
	for (size_t i=0;i<size;i++) {
		addr = begin+a[i];
		if (find(pool.begin(), pool.end(), addr)==pool.end()) {
			pool_write(file, addr, name.c_str());
			return ntoa(addr);
		}
	}
	throw runtime_error( "Pool exhausted" );
}

void pool_release(string filename, string name) {
	fstream file;
	pool_open(filename,file);
	uint32_t addr = pool_find(file,name.c_str());
	if (addr) pool_write(file, 0, "\0");
}

void pool_release(string filename, uint32_t addr) {
	fstream file;
	pool_open(filename,file);
	if (!pool_find(file, addr).empty()) {
		pool_write(file, 0, "\0");
	}
}

struct main_options {
	const char* command;
	const char* file;
	const char* name;
	uint32_t begin;
	uint32_t end;  
	uint32_t addr;
};

int main(int argc, char **argv) {
	
	main_options opts = {0};
        opts.command = "";
	opts.file = "pool";
		
	#define LONG_OPT_FILE  1001
	#define LONG_OPT_NAME  1002
	#define LONG_OPT_BEGIN 1003
	#define LONG_OPT_END   1004
	#define LONG_OPT_ADDR  1005

	while (argc>1) {
		opts.command = argv[1];
		static struct option long_options[] = {
			{"file",  required_argument, 0, LONG_OPT_FILE},
			{"name",  required_argument, 0, LONG_OPT_NAME},
			{"begin", required_argument, 0, LONG_OPT_BEGIN},
			{"end",   required_argument, 0, LONG_OPT_END},
			{"addr",  required_argument, 0, LONG_OPT_ADDR},
			{0, 0, 0, 0}
		};
	
		int option_index = 0;
		int c = getopt_long(argc-1, argv+1, "", long_options, &option_index);
		if (c == -1) break;
		
		switch (c) {
		case LONG_OPT_FILE:
			opts.file=strdup(optarg);
			break;

		case LONG_OPT_NAME:
			if (strlen(optarg)>MAX_NAME_LEN) {
				fprintf(stderr,"Name too long: %s\n", optarg);
				return 1;
			}
			opts.name=strdup(optarg);
			break;

		case LONG_OPT_BEGIN:
			opts.begin = aton(optarg);
			break;
		case LONG_OPT_END:   
			opts.end = aton(optarg);
			break;
		case LONG_OPT_ADDR:  
			opts.addr = aton(optarg);
			break;
		}
	}
	

	if (strcmp(opts.command, "request")==0) {
		if (opts.name && !opts.addr && opts.begin && opts.end) {
			cout << pool_request(opts.file,opts.begin,opts.end,opts.name) << endl;
			return 0;
		} else {
			cout << pool_request(opts.file,opts.name,opts.addr) << endl;
			return 0;
		}
	}

	if (strcmp(opts.command, "request")==0) {
		if (opts.addr!=0 && opts.name!=nullptr) {
			fprintf(stderr,"Use either --name or --addr\n");
			return 1;
		}
		if (opts.addr) {
			pool_release(opts.file,opts.addr);
			return 0;
		}
		if (opts.name) {
			pool_release(opts.file,opts.name);
			return 0;
		}
	}
	
	if (strcmp(opts.command, "get")==0) {
		if (opts.addr!=0 && opts.name!=nullptr) {
			fprintf(stderr,"Use either --name or --addr\n");
			return 1;
		}
		if (opts.addr) {
			cout << pool_find(opts.file, opts.addr) << endl;
			return 0;
		}
		if (opts.name) {
			cout << pool_find(opts.file, opts.name) << endl;
			return 0;
		}
	}

	if (strcmp(opts.command, "print")==0) {
		pool_print(opts.file);
		return 0;
	}

	cout << "Usage:"<< endl
             << argv[0]<<" request --begin <address> --end <address> --name <name> --file <file>"<<endl
             << argv[0]<<" request --address <address> --file <file>"<<endl
             << argv[0]<<" release --name <name> --file <file>"<<endl
             << argv[0]<<" release --address <address> --file <file>"<<endl
             << argv[0]<<" get --name <name> --file <file>"<<endl
             << argv[0]<<" get --address <address> --file <file>"<<endl
             << argv[0]<<" print --file <file>"<<endl;

	return 1;
}


