/*
 * proxy.c - CS:APP Web proxy
 *
 * Dillon Gaughan
 *  
 *
 * 
 * I found a working layout created by Andrew Carnegie and Harry Q. Bovik. There was an error in the original logging
 * function that was missing a log for a 404 request so I implemented raw URI. I also found the process request function
 * difficult to read and proceeded to break it up into a more modular design, which I tend to favor. I also decided to 
 * dynamically manage the size of the buffer for the http request. I believe now that debugging this code as well as adding
 * further improvements will be much easier. 
 *
 * This proxy server acts as an intermediary between clients and servers. It accepts HTTP requests from clients, processes
 * these requests, forwards them to the appropriate web servers, and then relays the responses back to the clients. This
 * operation is multi-threaded, enabling it to handle multiple client requests concurrently. The server also includes
 * logging functionality for monitoring and debugging purposes.
 */ 

#include "csapp.h"
#define PROXY_LOG "proxy.log"
#define DEBUG
typedef struct {
    int myid;    
    int connfd;                    
    struct sockaddr_in clientaddr;
} arglist_t;

/*********************
 * Global Variables  *
 *        &          *
 *  Function Headers *
 ********************/

static unsigned long global_thread_id = 0;
static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *log_file; 
sem_t mutex;    
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp); 
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen); 
void Rio_writen_w(int fd, void *usrbuf, size_t n);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);



/*************
*    MAIN    *
*************/
  
int main(int argc, char **argv)
{
    int listenfd;             
    pthread_t tid;            
    unsigned int clientlen;           
    arglist_t *argp = NULL;  
    int request_count = 0;

    if (argc != 2) {
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }
    signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    log_file = Fopen(PROXY_LOG, "a");
    Sem_init(&mutex, 0, 1); 
  
   
    while (1) { 
	argp = (arglist_t *)Malloc(sizeof(arglist_t));
	clientlen = sizeof(argp->clientaddr);
	argp->connfd = 
	  Accept(listenfd, (SA *)&argp->clientaddr, (socklen_t *) &clientlen); 
	argp->myid = request_count++;
	pthread_create(&tid, NULL, process_request, argp);
    }
    exit(0);
}

/**************************
* Process request helpers *
**************************/


 /*read an entire HTTP request from a client and *
 *store it in a dynamically allocated string    */

char* read_http_request(int connfd, rio_t *rp){
    int realloc_factor = 2;
    int request_len = 0;  
    char *request = (char *)Malloc(MAXLINE);
    char buf[MAXLINE];
    int n;

    if(request == NULL){
        return NULL;
    }

    request[0] = '\0';
    Rio_readinitb(rp, connfd);

    while (1) {
        n = Rio_readlineb_w(rp, buf, MAXLINE);
        if (n <= 0) {
            printf("process_request: client issued a bad request (1).\n");
            free(request);
            return NULL;
        }
        if (request_len + n + 1 > MAXLINE * realloc_factor) {
            char *temp = Realloc(request, MAXLINE * realloc_factor);
            if(temp == NULL) {
                free(request);
                return NULL;
            }
            request = temp;
            realloc_factor++;
        }

        strcat(request, buf);
        request_len += n;

        if (strcmp(buf, "\r\n") == 0) break; 
    }
    return request;
}


/*process an HTTP request string to validate that it *
 * is a GET request and to extract the URI.           */

char* validate_and_extract_uri(char *request, char **request_uri_end) {
    if (strncmp(request, "GET ", strlen("GET "))) {
        printf("Received non-GET request\n");
        return NULL;
    }
    char *request_uri = request + 4; 
    *request_uri_end = NULL;
    for (int i = 0; request_uri[i] != '\0'; i++) {
        if (request_uri[i] == ' ') {
            request_uri[i] = '\0';
            *request_uri_end = &request_uri[i];
            break;
        }
    }
    if (*request_uri_end == NULL) {
        return NULL;
    }

    return request_uri;
}

/*Create thread ids one by one for each connection for easy debugging*/

unsigned long get_next_thread_id() {
    pthread_mutex_lock(&id_mutex);
    unsigned long thread_id = global_thread_id++;
    pthread_mutex_unlock(&id_mutex);
    return thread_id;
}


/* Debgging code ripped out of old process request function.             *
 * Prints details of the HTTP request being handled by a specific thread.*/

void debug_print_request(int thread_id, struct sockaddr_in clientaddr, char *request) {
    struct hostent *hp;
    char *haddrp;

    P(&mutex);
    hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                       sizeof(clientaddr.sin_addr.s_addr), 
                       AF_INET);
    haddrp = inet_ntoa(clientaddr.sin_addr);
    printf("Thread %d: Received request from %s (%s):\n", thread_id,
           hp ? hp->h_name : "Unknown", haddrp);
    printf("%s", request);
    printf("*** End of Request ***\n\n");
    fflush(stdout);
    V(&mutex);
}

/* responsible for forwarding an HTTP request to the destination    *
 * server and relaying the response back to the client in the proxy.*/

int forward_request_to_server(int serverfd, int connfd, const char* pathname, const char* rest_of_request, rio_t *rio, unsigned long thread_id) {
    Rio_writen_w(serverfd, "GET /", strlen("GET /"));
    Rio_writen_w(serverfd, (void*)pathname, strlen(pathname));
    Rio_writen_w(serverfd, " HTTP/1.0\r\n", strlen(" HTTP/1.0\r\n"));
    Rio_writen_w(serverfd, (void*)rest_of_request, strlen(rest_of_request));
    int response_len = 0;
    ssize_t n;
    char buf[MAXLINE];
    Rio_readinitb(rio, serverfd);
    while((n = Rio_readn_w(serverfd, buf, MAXLINE)) > 0) {
        response_len += n;
        Rio_writen_w(connfd, buf, n);
        #if defined(DEBUG)
        printf("Thread %lu: Forwarded %zd bytes from end server to client\n", thread_id, n); 
        fflush(stdout);
        #endif
        bzero(buf, MAXLINE);
    }
    return response_len;
}

/****************
* HEART OF PROXY*
****************/
/*handles the core functionality of a proxy server, *
* managing the communication between the client and *
* the server by calling the helpers. Also contains  *
* the logic for thread management and cleanup.      */

void *process_request(void *vargp) 
{
    arglist_t arglist;              
    struct sockaddr_in clientaddr;       
    int connfd;                     
    int serverfd; 
    char *request;                            
    char *request_uri;              
    char *request_uri_end;          
    char *rest_of_request;                         
    int response_len;                                                   
    char hostname[MAXLINE];         
    char pathname[MAXLINE];         
    int port;                       
    char log_entry[MAXLINE];
    unsigned long thread_id = get_next_thread_id();        

    rio_t rio;             


    arglist = *((arglist_t *)vargp); 
    connfd = arglist.connfd;           
    clientaddr = arglist.clientaddr;
    Pthread_detach(pthread_self());  
    Free(vargp);
    request = read_http_request(connfd, &rio);
    if (request == NULL) {
        close(connfd);
        return NULL;
    } 
    char raw_uri[MAXLINE];
    if (sscanf(request, "GET %s", raw_uri) < 1) {
    strcpy(raw_uri, "Invalid or Incomplete URI"); }


#if defined(DEBUG) 	
    debug_print_request(thread_id, clientaddr, request);
#endif

    request_uri = validate_and_extract_uri(request, &request_uri_end);
    if (request_uri == NULL) {
        printf("process_request: Couldn't find the end of the URI\n");
        close(connfd);
        free(request);
        return NULL;
    }
    if (strncmp(request_uri_end + 1, "HTTP/1.0\r\n", strlen("HTTP/1.0\r\n")) &&
	strncmp(request_uri_end + 1, "HTTP/1.1\r\n", strlen("HTTP/1.1\r\n"))) {
	printf("process_request: client issued a bad request (4).\n");
	close(connfd);
	free(request);
	return NULL;
    }
    rest_of_request = request_uri_end + strlen("HTTP/1.0\r\n") + 1;


    if (parse_uri(request_uri, hostname, pathname, &port) < 0) {
	printf("process_request: cannot parse uri\n");
	close(connfd);
	free(request);
	return NULL;
    }    


    serverfd = open_clientfd_ts(hostname, port, &mutex);
        if (serverfd < 0) {
            printf("process_request: Unable to connect to end server.\n");
            free(request);
            return NULL;
        }
    response_len = forward_request_to_server(serverfd, connfd, pathname, rest_of_request, &rio, thread_id);
    format_log_entry(log_entry, &clientaddr, raw_uri, response_len);  
    P(&mutex);
    fprintf(log_file, "%s %d\n", log_entry, response_len);
    fflush(log_file);
    V(&mutex);
    close(connfd);
    close(serverfd);
    free(request);
    return NULL;
}


/*
 * prints a warning message when a read fails instead of terminating
 * the process.
 */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0) {
	printf("Warning: rio_readn failed\n");
	return 0;
    }    
    return n;
}


 /* prints a warning when a read fails instead of terminating 
 * the process.*/
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
	printf("Warning: rio_readlineb failed\n");
	return 0;
    }
    return rc;
} 


 /* prints a warning when a write fails, instead of terminating the
 * process.*/

void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n) {
	printf("Warning: rio_writen failed.\n");
    }	   
}


 /* A thread safe version of the open_clientfd
 * function (csapp.c) that uses the lock-and-copy technique to deal
 * with the Class 3 thread unsafe gethostbyname function.*/

int open_clientfd_ts(char *hostname, int port, sem_t *mutexp) 
{
    int clientfd;
    struct hostent hostent, *hp = &hostent;
    struct hostent *temp_hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return -1; 
    P(mutexp);
    temp_hp = gethostbyname(hostname);
    if (temp_hp != NULL)
	hostent = *temp_hp; 
    V(mutexp);
    if (temp_hp == NULL)
	return -2; 
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);


    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
	return -1;
    return clientfd;
}


 /* Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.*/

int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    *port = 80; 
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    

    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }

    return 0;
}


 /*Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).*/

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}