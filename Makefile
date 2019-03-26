SHELL = /bin/sh
CC = g++

CFLAG = -W -Wall -g -Wno-reorder -Wno-unused-parameter -Wno-format
 
INCLUDE_PATH = -I.
HTTPD_TARGET = tinyhttpd
HTTPD_SRC = httpd.cpp tinyhttpd.cpp

all:
	$(CC) $(CFLAG) $(INCLUDE_PATH) $(HTTPD_SRC) -o $(HTTPD_TARGET) -lpthread
 
clean:
	rm -f *.o $(HTTPD_TARGET)