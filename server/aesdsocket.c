#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>

//Defines
//#define PRINT_LOG
#define BUFFER_SIZE 100

//Macros
#ifdef	PRINT_LOG
#define DEBUG_LOG(str, ...)	printf(str, ##__VA_ARGS__)
#else
#define DEBUG_LOG(str, ...)
#endif

//static variables
static char ipstr[INET_ADDRSTRLEN];
static const char* file_location = "/var/tmp/aesdsocketdata";

//funtons prototype
static void signal_handler_function(int signal_number);
static void* thread_function(void* param);

//struct
struct app_data
{
	int server_fd;
        FILE* file_fd;
	pthread_mutex_t* mutex;
};

int main(int argc, char* argv[])
{

	bool is_daemon = (argc == 2 && strncmp("-d", argv[1], 2) == 0);
	int opt = 1;
	int status = 0;
	int server_fd;
	struct addrinfo hints;
	struct addrinfo* servinfo = NULL;
	struct sigaction s_action;
	struct app_data thread_data;
	pthread_mutex_t mutex;
	pthread_t server_thread;
	FILE* file_dir = NULL;

        memset(&hints, 0x00, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

	memset(&s_action, 0x00, sizeof(struct sigaction));
	s_action.sa_handler = signal_handler_function;

	openlog(NULL, 0, LOG_USER);

      	do
      	{		
		if((status = getaddrinfo( NULL, "9000", &hints, &servinfo)) != 0)
		{
			perror("perror returned when getting address info");
			status = -1;
			break;
		}

		if((server_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
		{
			perror("perror returned on attempting create a socket");
			status = -1;
                	break;
		}

		if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) != 0)
		{
                	perror("perror returned when setting socket options");
                	status = -1;
                	break;		
		}
	
		if(bind(server_fd, servinfo->ai_addr, sizeof(struct sockaddr)) != 0)
		{
                	perror("perror returned when attempting bind the server");
               	  	status = -1;
                	break;			
		}

        }
	while(0);

	freeaddrinfo(servinfo);

	if(status == 0 && is_daemon)
	{
		DEBUG_LOG("starting a daemon...\n");
		pid_t pid = fork();

		if(pid == 0) //child  process
		{

			chdir("/");
			pid_t sid = setsid();			

			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
		}
		else if(pid > 0) //parent process
		{
		        close(server_fd);		
			exit(EXIT_SUCCESS);
		}
		else
		{
			DEBUG_LOG("ERROR: it's not possible to create the child\n");
			exit(EXIT_FAILURE);
		}
	}
      
	do
      	{
                if(status != 0)
                        break;

		if(listen(server_fd, 10) == -1)
		{	
			perror("perror ocurrend while linstening");
                	status = -1;
                	break;
		}

		file_dir = fopen(file_location, "w+r");

		if(file_dir == NULL)
        	{
            		perror("perror on opening or creating a file");
                        status = -1;
                        break;
        	}

        	if(pthread_mutex_init(&mutex, NULL) != 0)
        	{
                	DEBUG_LOG("Failed to initialize mutex");
                        status = -1;
                        break;			
        	}

        	if(sigaction(SIGTERM, &s_action, NULL) != 0)
        	{
                	perror("signal SIGTERM perror");
			status = -1;
                        break;
        	}

        	if(sigaction(SIGINT, &s_action, NULL) != 0)
        	{
                	perror("signal SIGINT  perror");
                        status = -1;
                        break;			
        	}
	}
      	while(0);

	thread_data.file_fd = file_dir;
	thread_data.server_fd = server_fd;
	thread_data.mutex = &mutex;	
	
	do
	{
		if(status != 0)
			break;

		if(pthread_create(&server_thread, NULL, &thread_function, (void *) &thread_data) != 0)
		{
			DEBUG_LOG("It was not possible to create the thread");
                        status = -1;
                        break;
		}

		if(pthread_detach(server_thread) != 0)
		{
                        DEBUG_LOG("it was not possivel to deattach the thread\n");
                        status = -1;
                        break;
		}

		pause();

        	if(pthread_mutex_lock(&mutex) != 0)
        	{
        		DEBUG_LOG("it was not possivel to lock mutex");
                        status = -1;
                        break;
        	}
		
		if(pthread_cancel(server_thread) != 0)
		{
			DEBUG_LOG("Erro to cancel the thread");
                        status = -1;
                        break;			
		}

        	if(pthread_mutex_unlock(&mutex) != 0)
        	{
                	DEBUG_LOG("it was not possivel to unlock mutex");
                        status = -1;
                        break;			
        	}
	}
	while(0);
	
	DEBUG_LOG("\nClosing server..\n");
	shutdown(server_fd, SHUT_RDWR); 
	close(server_fd);
	fclose(file_dir);

	DEBUG_LOG("removing aesdsocketdata\n");
	remove(file_location);	

	return status;

}

static void signal_handler_function(int signal_number)
{
	syslog(LOG_DEBUG, "Caught signal, exiting");
}

static void* thread_function(void* param)
{
	struct app_data* t_data = (struct app_data*) param;
	FILE* file_dir = t_data->file_fd;
	int server_fd = t_data->server_fd;
	struct sockaddr client_sock_addr;
	struct pollfd poll_socket;
        socklen_t client_sock_length;
        pthread_mutex_t* mutex = t_data->mutex;

        char rec_buff[BUFFER_SIZE + 1] = {'\0'};
        int client_fd;

	poll_socket.fd = server_fd;
	poll_socket.events = POLLIN;

        for(;;)
        {
		DEBUG_LOG("Polling socket conection\n");
		if(poll(&poll_socket, 1, -1) == -1) 
		{
		     DEBUG_LOG("Polling has failed.\n");
		     continue;
		}

		DEBUG_LOG("Socket conection request arrived.\n");

                if(pthread_mutex_lock(mutex) != 0)
                {
                        DEBUG_LOG("it was not possivel to lock mutex");
                        close(client_fd);
                        continue;
                }

		client_sock_length = sizeof(struct sockaddr);
                if((client_fd = accept(server_fd, &client_sock_addr, &client_sock_length)) == -1)
                {
                        perror("accept error");
                        continue;
                }

                memset(ipstr, 0x00, sizeof(ipstr));
                inet_ntop(AF_INET, &(((struct sockaddr_in*) &client_sock_addr)->sin_addr), ipstr, sizeof(ipstr));
                syslog(LOG_DEBUG, "Accepted connection from %s \n", ipstr);
                DEBUG_LOG("Accepted connection from %s \n", ipstr);


                do
                {
                        int rx_size = 0;

                        DEBUG_LOG("receiving data\n");
                        rx_size = recv(client_fd, rec_buff, BUFFER_SIZE, 0);

                        if(rx_size == -1) //an error has occurred
                        {
                                perror("recv error");
                                break;
                        }
                        else if (rx_size == 0) // The remote side has closed
                        {
                                DEBUG_LOG("the remote side has closed");
                                break;
                        }
                        else if(rx_size < BUFFER_SIZE)
                        {
                                if(fwrite(rec_buff, rx_size, 1, file_dir) == -1)
                                {
                                        DEBUG_LOG("error fwrite\n");
                                }
                                if(strchr(rec_buff, '\n') != NULL)
                                {
                                        break;
                                }
                        }
                        else
                        {
                                if(fwrite(rec_buff, rx_size, 1, file_dir) == -1)
                                {
                                        DEBUG_LOG("error fwrite\n");
                                }
                                if(strchr(rec_buff, '\n') != NULL)
                                {
                                        break;
                                }
                        }

                }
                while(1);

                DEBUG_LOG("Sending data back..\n");
                fseek(file_dir, 0, SEEK_SET);

      
      		do //read the content of the file and send over the socket
                {

                        memset(rec_buff, '\0', sizeof(rec_buff));
                        if(fgets(rec_buff, BUFFER_SIZE, file_dir) == NULL)
                        {
                                break;
                        }

                        DEBUG_LOG("fgets result: %s", rec_buff);

                        if(send(client_fd, rec_buff, strlen(rec_buff), 0) == -1)
                        {
                                perror("perror while sending");
                                break;
                        }
                }
                while(1);

                fseek(file_dir, 0, SEEK_END);
                close(client_fd);
                syslog(LOG_DEBUG, "Closed connection from %s \n", ipstr);

                if(pthread_mutex_unlock(mutex) != 0)
                {
                        DEBUG_LOG("it was not possivel to unlock mutex");
                }
        }		
}

