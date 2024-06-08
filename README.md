# Simple Proxy Server
Author: Saeed Asle

Files:
* proxyServer.c: Implementation of a multithreaded HTTP proxy server in C.
* 2.threadpool.c:  file that implements the thread pool features.

# Description
This project implements a simple proxy server in C.

Filtering: Filters requests based on a specified filter file.

The server listens on a specified port and forwards incoming HTTP requests to the destination server.

Remarks:
- The proxy server supports only the GET method.
- The proxy server supports HTTP/1.0 and HTTP/1.1 protocols.
- Implemented filtering based on a filter file containing host names and sub-networks.
- The code manages the creation of responses, parsing of HTTP requests, and socket actions.
- Error handling includes sending appropriate HTTP error responses.
- Uses a thread pool for handling multiple client connections.
- The thread pool implementation provides a basic thread pool functionality for managing a pool of threads to perform tasks concurrently.
- The program supports using command-line options to set proxyServer <port> <pool-size> <max-number-of-request> <filter>.
- Structure-allocated memory is correctly freed.

# Features
* HTTP Request Handling: Handles incoming HTTP requests from clients.
* Filtering: Filters requests based on a specified filter file.
* Multi-threaded: Uses a thread pool to handle multiple client requests concurrently.
Usage
Compile:

      gcc -o proxyServer proxyServer.c threadpool.c -lpthread
  
Run:

    ./proxyServer <port> <pool-size> <max-number-of-request> <filter>
        

Example:

    ./proxyServer 8080 5 10 filter.txt

# Parameters
* port: Port number to listen on.
* pool-size: Size of the thread pool for handling client requests.
* max-number-of-request: Maximum number of requests to process.
* filter: Path to the filter file for request filtering.

# Filter File Format
The filter file contains a list of hostnames and IP addresses to allow/deny. Each entry should be on a new line.

Example:

    Copy code
    www.example.com
    192.168.1.1
    
# Dependencies
pthread: For multi-threading support.
