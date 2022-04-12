#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAXCMDSIZE 128
#define MAXPENDING 5
#define MAXOUTSIZE 4000

typedef struct node
{
    char *nodename;
    char *ip;
    short port;
} node;

void ignore_signal(int sig)
{

    //do nothing
}
void trim_string(char *str)
{
    int first_non_space = 0;
    while (str[first_non_space] == ' ')
    {
        first_non_space++;
    }
    for (int i = 0; i < strlen(str); i++)
    {
        str[i] = str[first_non_space++];
    }
    int last_non_space = strlen(str) - 1;
    while (str[last_non_space] == ' ')
    {
        str[last_non_space] = '\0';
        last_non_space--;
    }
}
char **tokenise_string(char **cmd_line_inp_, int *num_tokens, char delim) //return tokens of strings ,trimmed and delimited
{
    char *cmd_line_inp = *cmd_line_inp_;
    char *temp = malloc(strlen(cmd_line_inp) + 1);
    strcpy(temp, cmd_line_inp);
    *num_tokens = 1;
    for (int i = 0; cmd_line_inp[i] != '\0'; i++)
    {
        if (cmd_line_inp[i] == delim)
        {
            (*num_tokens) += 1;
        }
    }
    char **tokens = (char **)malloc(sizeof(char *) * (*num_tokens));

    char *token = strtok(cmd_line_inp, &delim);
    trim_string(token);
    tokens[0] = (char *)malloc(strlen(token) + 1);
    memcpy(tokens[0], token, strlen(token) + 1);

    int i = 0;
    while (token != NULL)
    {
        token = strtok(NULL, &delim);
        if (token != NULL)
        {
            trim_string(token);
            tokens[++i] = (char *)malloc(strlen(token) + 1);
            memcpy(tokens[i], token, strlen(token) + 1);
        }
    }
    free(*cmd_line_inp_);
    *cmd_line_inp_ = temp;
    return tokens;
}

int start_server(long portnum)
{ //starts the server and returns the listenfd of server
    int servsockfd;
    if ((servsockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket init:");
        return -1;
    }
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(portnum);

    if (bind(servsockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        perror("server bind:");
        close(servsockfd);
        return -1;
    }

    if (listen(servsockfd, MAXPENDING) == -1)
    {
        perror("listen:");
        close(servsockfd);
        return -1;
    }

    return servsockfd;
}
int execute_command(char **cmd, int p[2], int p_in[2])
{
    //parse command line arguments
    int num_tokens = 0;
    char **tokens = tokenise_string(cmd, &num_tokens, ' ');
    char *program_name = tokens[0];
    char *argv[num_tokens + 1];
    for (int i = 0; i < num_tokens; i++)
    {
        argv[i] = tokens[i];
    }
    argv[num_tokens] = NULL;

    if (strcmp(argv[num_tokens - 1], "clustertop") == 0)
    {
        char process_id[8];
        sprintf(process_id, "%d", getpid());
        argv[2] = process_id;
        argv[num_tokens - 1] = NULL;
    }

    int ret_val = 1;
    if (strcmp("cd", argv[0]) == 0)
    {
        if (chdir(argv[1]) != 0)
        {
            ret_val = -1;
        }
    }
    else
    {
        int ret = fork();
        if (ret == -1)
        {
            ret_val = -1;
        }
        else if (ret == 0)
        {
            close(1);
            dup(p[1]);
            close(0);
            dup(p_in[0]);
            close(2);

            execvp(program_name, argv);
            exit(EXIT_FAILURE);
        }
        else
        {

            int status;
            waitpid(ret, &status, 0);
            if (status == EXIT_FAILURE || status == EXIT_FAILURE * 256)
            {
                ret_val = -1;
            }
        }
    }
    close(p[1]);
    close(p_in[0]);

    for (int i = 0; i < num_tokens; i++)
    {
        free((void *)tokens[i]);
    }
    free((void *)tokens);
    return ret_val;
}
void *handle_connection(void *p_clientsocket)
{
    pthread_detach(pthread_self()); //detach the thread so the status doesnt has to be reclaimed
    int clientconnfd = *(int *)p_clientsocket;
    free(p_clientsocket);
    char *cmd, *inbuf, *outbuf;
    cmd = (char *)malloc(MAXCMDSIZE);
    inbuf = (char *)malloc(MAXOUTSIZE);
    outbuf = (char *)malloc(MAXOUTSIZE);
    memset(inbuf, '\0', MAXOUTSIZE);
    memset(outbuf, '\0', MAXOUTSIZE);
    int p_out[2];
    pipe(p_out);

    int p_in[2];
    pipe(p_in);
    if (read(clientconnfd, cmd, MAXCMDSIZE) <= 0)
    {
        free(cmd);
        free(inbuf);
        free(outbuf);
        close(p_in[1]);
        close(p_out[0]);
        close(clientconnfd);
        return NULL;
    }

    if ((read(clientconnfd, inbuf, MAXOUTSIZE)) <= 0)
    {
        free(cmd);
        free(inbuf);
        free(outbuf);
        close(p_in[1]);
        close(p_out[0]);
        close(clientconnfd);
        return NULL;
    }

    if (write(p_in[1], inbuf, strlen(inbuf)) < 0)
    {
        free(cmd);
        free(inbuf);
        free(outbuf);
        close(p_in[1]);
        close(p_out[0]);
        close(clientconnfd);
        return NULL;
    }
    free(inbuf);
    close(p_in[1]);
    if (execute_command(&cmd, p_out, p_in) == -1)
    {
        free(cmd);
        free(outbuf);
        close(p_out[0]);
        close(clientconnfd);
        return NULL;
    }
    int char_read = read(p_out[0], outbuf, MAXOUTSIZE);
    close(p_out[0]);
    if (char_read == -1)
    {
        free(cmd);
        free(inbuf);
        free(outbuf);
        close(clientconnfd);
        return NULL;
    }
    write(clientconnfd, outbuf, MAXOUTSIZE);
    close(clientconnfd);
    // printf("\nConnection Ended);
    free(outbuf);
    return NULL;
}

void accept_connections(int servsockfd)
{
    struct sockaddr_in clientaddr;
    int clientaddr_len = sizeof(clientaddr);

    for (;;)
    {
        int clientconnfd;
        if ((clientconnfd = accept(servsockfd, (struct sockaddr *)&clientaddr, &clientaddr_len)) <= 0)
        {
            if (errno == EINTR)
                continue;
        }
        // printf("connection accepted");
        // fflush(stdout);
        printf("\nConnection Established with %s", inet_ntoa(clientaddr.sin_addr));
        pthread_t t;
        int *pclient = malloc(sizeof(int));
        *pclient = clientconnfd;
        // handle_connection((void *)pclient);
        pthread_create(&t, NULL, handle_connection, (void *)pclient);
    }
}

int main()
{
    // close(0);
    signal(SIGPIPE, ignore_signal);
    setbuf(stdout, NULL);
    long port = 60000;
    int listenfd = start_server(port);
    if (listenfd == -1)
    {
        exit(1);
    }
    else
    {
        printf("\nListenfd:%d", listenfd);
    }
    // printf("going to accept connections now");
    accept_connections(listenfd);
}