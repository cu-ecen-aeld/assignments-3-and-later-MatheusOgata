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

//Defines
#define PRINT_LOG
#define BUFFER_SIZE 100

//Macros
#ifdef	PRINT_LOG
#define DEBUG_LOG(str, ...)	printf(str, ##__VA_ARGS__)
#else
#define DEBUG_LOG(str, ...)
#endif

//static variables
static char ipstr[INET_ADDRSTRLEN];

//funtons prototype
static void signal_handler_function(int signal_number);

int main(int argc, char* argv[])
{

	int opt;
	int status;
	int server_fd;
	int client_fd;
	struct addrinfo hints;
	struct addrinfo* servinfo = NULL;
	struct sockaddr client_sock_addr;
	struct sigaction s_action;
	socklen_t client_sock_length;

        memset(&hints, 0x00, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

	memset(&s_action, 0x00, sizeof(struct sigaction));
	s_action.sa_handler = signal_handler_function;

        FILE* file_dir = fopen("/var/tmp/aesdsocketdata", "w+r");
        openlog(NULL, 0, LOG_USER);


	if(sigaction(SIGTERM, &s_action, NULL) != 0)
	{
		perror("signal SIGTERM perror");
	}

	if(sigaction(SIGINT, &s_action, NULL) != 0)
	{
		perror("signal SIGINT  perror");
	}


	if(file_dir == NULL)
        {
            perror("perror on opening or creating a file");
            return -1;
        }


	if((status = getaddrinfo( NULL, "9000", &hints, &servinfo)) != 0)
	{
		perror("perror returned when getting address info");
		return -1;
	}


	if((server_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{

		perror("perror returned on attempting create a socket");
		return -1;
	}

	if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
	{
                perror("perror returned when setting socket options");
                return -1;
	}
	
	if(bind(server_fd, servinfo->ai_addr, sizeof(struct sockaddr)) != 0)
	{
                perror("perror returned when attempting bind the server");
                return -1;
	}

	freeaddrinfo(servinfo);

	if(listen(server_fd, 10) == -1)
	{
		perror("perror ocurrend while linstening");
		return -1;
	}

	for(;;)
	{
		char rec_buff[BUFFER_SIZE + 1] = {'\0'};
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
				DEBUG_LOG("fgets error\n");
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
	}	

	close(server_fd);
	fclose(file_dir);

	return status;

}

static void signal_handler_function(int signal_number)
{

	syslog(LOG_DEBUG, "Caught signal, exiting");

}
