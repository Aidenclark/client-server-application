#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <climits>

using namespace std;

const int NUMBER_OF_ARGS = 3;
const int PACKET_SIZE = 1024;

struct Arguments
{
    int port;
    string host;
    string filename;
};

void printUsage()
{
    cerr<< "USAGE: ./client <HOSTNAME-OR-IP> <PORT> <FILENAME>\n";
}

void printError(string message)
{
    cerr<<"ERROR: ";
    cerr<< message <<endl;
}

void exitOnError(int sockfd)
{
    close(sockfd);
    exit(1);
}

sockaddr_in createServerAddr(const int port, const string IP)
{
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);     // short, network byte order
    serverAddr.sin_addr.s_addr = inet_addr(IP.c_str());
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));
    return serverAddr;
}



/***
serverConnect(), is used to connect to a server using a TCP socket. It takes two arguments: sockfd, which is the file descriptor of the socket, and serverAddr, which is the address of the server. Here's what the function does:

Sets a timeout value of 15 seconds using a struct timeval.
Tries to connect to the server using the connect() function. If the connection attempt fails and the error code is EINPROGRESS, it means that the connection is still being established asynchronously. In this case, the function waits for the connection to complete by using select() to check if the socket is ready for writing.
If select() returns an error, the function prints an error message using perror() and exits the program.
If select() times out, the function prints an error message and exits the program.
If the connection attempt fails for any other reason, the function prints an error message using perror() and exits the program.
***/

void serverConnect(const int sockfd, const struct sockaddr_in &serverAddr)
{
    // Set timeout for connection
    struct timeval timeout;
    timeout.tv_sec = 15;
    timeout.tv_usec = 0;

    // Connect to server
    int result = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (result == -1) {
        if (errno == EINPROGRESS) {
            // Wait for connection to be established
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(sockfd, &writefds);

            int selectResult = select(sockfd + 1, NULL, &writefds, NULL, &timeout);
            if (selectResult == -1) {
                perror("select() failed");
                exit(1);
            } else if (selectResult == 0) {
                perror("Timeout! Client has not been able to connect to the server in more than 15 seconds.");
                exit(1);
            }
        } else {
            perror("connect() failed");
            exit(1);
        }
    }
}

/***
createClientAddr(), is used to retrieve the local address of a TCP socket. It takes one argument: sockfd, which is the file descriptor of the socket. 
***/

struct sockaddr_in createClientAddr(const int sockfd)
{
    struct sockaddr_in clientAddr;
    memset(&clientAddr, 0, sizeof(clientAddr));
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getsockname(sockfd, (struct sockaddr *)&clientAddr, &clientAddrLen) == -1)
    {
        perror("getsockname() failed");
        exit(1);
    }
    return clientAddr;
}


void connectionSetup(const struct sockaddr_in clientAddr)
{
    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
}


void communicate(const int sockfd, const std::string filename)
{
    // send/receive data to/from connection
    std::fstream fin;
    fin.open(filename, std::ios::in);
    char buf[PACKET_SIZE];
    
    fd_set writefds;
    
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    
    do {
        fin.read(buf, PACKET_SIZE);
        if (fin.gcount() == 0) {
            break;
        }
        
        FD_ZERO(&writefds);
        FD_SET(sockfd, &writefds);
        
        int sel_res = select(sockfd+1,NULL,&writefds,NULL,&timeout);
        
        if(sel_res == -1)
        {
            printError("select() failed.");
            exitOnError(sockfd);
        }
        else if(sel_res==0)
        {
            printError("Timeout! Client has not been able to send data to the server in more than 10 seconds.");
            exitOnError(sockfd);
        }
        else
        {
           if (send(sockfd, buf, fin.gcount(), MSG_NOSIGNAL) == -1)
            {
                printError("Unable to send data to server");
                exitOnError(sockfd);
            }
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
        }

    } while (fin);
    fin.close();
}






long parsePort(char **argv)
{
    char *endptr;
    long temp_port = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || temp_port < 1024 || temp_port > 65535)
    {
        printError("Port number needs to be a valid integer greater than 1023.");
        exit(1);
    }
    return temp_port;
}



string parseHost(char **argv)
{
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result;
    int ret = getaddrinfo(argv[1], nullptr, &hints, &result);
    if (ret != 0)
    {
        printError("Host name is invalid.");
        printUsage();
        exit(1);
    }

    char addrbuf[INET_ADDRSTRLEN];
    inet_ntop(result->ai_family, &((struct sockaddr_in *)(result->ai_addr))->sin_addr, addrbuf, sizeof(addrbuf));
    freeaddrinfo(result);

    return addrbuf;



Arguments parseArguments(int argc, char** argv)
{
    if (argc != (NUMBER_OF_ARGS + 1))
    {
        printError("Incorrect number of arguments");
        printUsage();
        exit(1);
    }
    Arguments args;

    // host
    struct addrinfo hints, *info;
    hints.ai_family = AF_INET;

    if (getaddrinfo(argv[1], NULL, &hints, &info))
    {
        printError("Host name is invalid.");
        printUsage();
        exit(1);
    }
    char addrbuf[INET_ADDRSTRLEN + 1];
    const char *addr = inet_ntop(info->ai_family, &(((struct sockaddr_in *)info->ai_addr)->sin_addr), addrbuf, sizeof(addrbuf));
    args.host = addr;

    // port
    long temp_port = strtol(argv[2], nullptr, 10);
    if (temp_port == 0 || temp_port == LONG_MAX || temp_port == LONG_MIN || (temp_port < 1024) || temp_port > 65535)
    {
        printError("Port number needs to be a valid integer greater than 1023.");
        exit(1);
    }
    args.port = temp_port;

    // filename
    args.filename = (string)argv[3];

    return args;
}




void setupEnvironment(const int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        printError("fcntl() failed 1.");
        exitOnError(sockfd);
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        printError("fcntl() failed.");
        exitOnError(sockfd);
    }
}



int
main(int argc, char **argv)
{
  Arguments args = parseArguments(argc, argv);
    
  // create a socket using TCP IP
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
  setupEnvironment(sockfd);

  struct sockaddr_in serverAddr = createServerAddr(args.port, args.host);

  serverConnect(sockfd, serverAddr);

  struct sockaddr_in clientAddr = createClientAddr(sockfd);
  
  connectionSetup(clientAddr);
    
  communicate(sockfd, args.filename);

  close(sockfd);

  return 0;
}
