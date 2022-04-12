#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <pwd.h>

#define MAXCMDSIZE 128
#define MAXPENDING 5
#define MAXOUTSIZE 4000

typedef enum SERVER_COMM
{
    SUCCESS,
    CONNECTION_FAILED,
    PREMATURE_EXIT,
    FAILED
} SERVER_COMM;
typedef struct node
{
    char name[30];
    char IP[16];
} node;
struct msg
{
    long msgtype;
    char output[MAXOUTSIZE];
};
typedef struct test_conn_msg
{
    long msgtype;
    int isActive;
} test_conn_msg;
node **node_array;
int num_nodes = 0;
char **localIPs;
int self_node;

void getlocalIPs()
{
    localIPs = (char **)malloc(sizeof(char *) * 10);
    for (int i = 0; i < 10; i++)
        localIPs[i] = NULL;
    struct ifaddrs *i1;
    getifaddrs(&i1);
    struct ifaddrs *ptr = i1;
    int j = 0;
    while (ptr)
    {
        if (ptr->ifa_addr->sa_family == AF_INET)
        {
            // char IP[INET_ADDRSTRLEN];
            char *IP;
            struct sockaddr_in *addr = (struct sockaddr_in *)(ptr->ifa_addr);
            struct in_addr ipAddr = addr->sin_addr;
            // inet_ntop(AF_INET,&ipAddr,IP,INET_ADDRSTRLEN);
            IP = inet_ntoa(addr->sin_addr);
            localIPs[j] = malloc(strlen(IP) + 1);
            strcpy(localIPs[j++], IP);
            // printf("%s\n", localIPs[j - 1]);
        }
        ptr = ptr->ifa_next;
    }
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
void read_config_file(FILE *fp)
{
    node_array = malloc(sizeof(node *) * 100);

    while (!feof(fp))
    {
        char *line = (char *)malloc(60);
        memset(line, '\0', 60);
        int char_read = fscanf(fp, "%s", line);
        if (char_read <= 0)
        {
            break;
        }
        int num_tokens;
        char **tokens = tokenise_string(&line, &num_tokens, ',');
        if(num_tokens!=2){
            printf("Invalid config file");
            exit(0);
        }
        struct sockaddr_in test_ip;
        if(inet_pton(AF_INET,tokens[1],&(test_ip.sin_addr))<=0){
            printf("Invalid IP address in config file");
            exit(0);
        }
        node_array[num_nodes] = (node *)malloc(sizeof(node));
        strcpy(node_array[num_nodes]->IP, tokens[1]);
        int j = 0;
        while (localIPs[j])
        {
            if (strcmp(tokens[1], localIPs[j]) == 0)
            {
                self_node = num_nodes + 1;
            }
            j++;
        }
        strcpy(node_array[num_nodes]->name, tokens[0]);
        num_nodes++;
        free(tokens[0]);
        free(tokens[1]);
        free(tokens);
        free(line);
    }
    // node_array=realloc((void*)node_array,num_nodes+1);
    node_array[num_nodes] = NULL;
}

char *get_actual_cmd(char **cmd, int *node) //returns the command separated from node information, modifies the command the include the missing node information(if applicable)
{
    char *temp = malloc(strlen(*cmd) + 1);
    char *result = malloc(strlen(*cmd) + 1);
    memset(result, '\0', strlen(*cmd) + 1);
    strcpy(temp, *cmd);
    *node = self_node;
    char *token = strtok(temp, ".");
    int offset = 0;
    if (strcmp(temp, "n*") == 0)
    {
        *node = 0;
        offset = strlen(temp) + 1;
    }
    else
    {
        for (int j = 0; j < num_nodes; j++)
        {
            if (strcmp(node_array[j]->name, temp) == 0)
            {
                *node = j + 1;
                offset = strlen(temp) + 1;
                break;
            }
        }
    }
    strcpy(result, (*cmd) + offset);
    free(temp);
    return result;
}
void input_prompt(char **cmd)
{
    printf("\n\033[0;36mClusterShell>>>\033[0m");
    if (fgets(*cmd, MAXCMDSIZE, stdin) == NULL)
    {
        perror("couldnt read input");
        exit(EXIT_FAILURE);
    };
    (*cmd)[strlen(*cmd) - 1] = '\0';
    if (strcmp(*cmd, "") == 0)
    {
        input_prompt(cmd);
    }
}

SERVER_COMM send_to_server(char *ip, unsigned int port, char *cmd_buf, char *inbuf, char *outbuf) //writes to given IP and port the command and input, outbuf contains the output
{
    struct sockaddr_in servadr;
    servadr.sin_family = AF_INET;

    inet_pton(AF_INET, ip, &servadr.sin_addr);
    servadr.sin_port = htons(port);
    int clisoc = socket(AF_INET, SOCK_STREAM, 0);
    int flags = fcntl(clisoc, F_GETFL);
    fcntl(clisoc, F_SETFL, O_NONBLOCK);
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(clisoc, &fdset);
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    int ret, so_error, len = sizeof(so_error);
    ret = connect(clisoc, (struct sockaddr *)&servadr, sizeof(servadr));
    if (ret < 0 && errno != EINPROGRESS)
    {
        close(clisoc);
        return CONNECTION_FAILED;
    }
    if (ret == 0)
    {
        fcntl(clisoc, F_SETFL, flags);
    }
    else if (select(clisoc + 1, NULL, &fdset, NULL, &tv) == 1)
    {
        getsockopt(clisoc, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error == 0)
        {
            fcntl(clisoc, F_SETFL, flags);
        }
        else
        {
            close(clisoc);
            return CONNECTION_FAILED;
        }
    }
    else
    {
        close(clisoc);
        return CONNECTION_FAILED;
    }
    // printf("writing to server now");
    if (write(clisoc, cmd_buf, MAXCMDSIZE) < 0)
    {
        return PREMATURE_EXIT;
    }
    if (write(clisoc, inbuf, MAXOUTSIZE) < 0)
    {
        return PREMATURE_EXIT;
    }
    int char_read = read(clisoc, outbuf, MAXOUTSIZE);
    if (char_read <= 0)
    {
        return PREMATURE_EXIT;
    }
    memset(outbuf + char_read, '\0', MAXOUTSIZE - char_read);
    return SUCCESS;
    // printf("%s", outbuf);
}
SERVER_COMM run_on_client(char **cmd, int use_stdin, char **inbuf, char **outbuf)
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
    int p_in[2];
    int p_out[2];
    pipe(p_in);
    pipe(p_out);
    write(p_in[1], *inbuf, strlen(*inbuf));
    close(p_in[1]);
    if (strcmp(argv[num_tokens - 1], "clustertop") == 0)
    {
        char process_id[8];
        sprintf(process_id, "%d", getppid());
        argv[2] = process_id;
        argv[num_tokens - 1] = NULL;
    }
    if (strcmp("cd", argv[0]) == 0)
    {
        chdir(argv[1]);
    }

    else
    {
        int ret = fork();

        if (ret == 0)
        {
            close(p_out[0]);
            if (use_stdin == 0)
            {
                close(0);
                dup(p_in[0]);
            }
            close(1);
            dup(p_out[1]);
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
                close(p_in[0]);
                close(p_out[1]);
                close(p_out[0]);
                return FAILED;
            }
        }
    }
    close(p_in[0]);
    close(p_out[1]);
    int char_read = read(p_out[0], *outbuf, MAXOUTSIZE);
    memset((*outbuf) + char_read, '\0', MAXOUTSIZE - char_read);
    close(p_out[0]);
    return SUCCESS;
}
void broadcast(char **cmd, int printoutput)
{
    int msgqid = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    struct msg message;
    for (int i = 1; i <= num_nodes; i++)
    {
        if (i == self_node)
        {
            int num_tokens = 0;
            char **tokens = tokenise_string(cmd, &num_tokens, ' ');
            char *program_name = tokens[0];
            char *argv[num_tokens + 1];
            for (int i = 0; i < num_tokens; i++)
            {
                argv[i] = tokens[i];
            }
            argv[num_tokens] = NULL;
            if (strcmp("cd", argv[0]) == 0)
            {
                chdir(argv[1]);
            }
            for (int i = 0; i < num_tokens; i++)
            {
                free(tokens[i]);
            }
            free(tokens);
        }
        int ret = fork();

        if (ret == 0)
        {
            char *inbuf = malloc(MAXOUTSIZE);
            char *outbuf = malloc(MAXOUTSIZE);
            memset((void *)inbuf, '\0', MAXOUTSIZE);
            memset((void *)outbuf, '\0', MAXOUTSIZE);
            if (i == self_node)
            {
                SERVER_COMM status = run_on_client(cmd, 0, &inbuf, &outbuf);
                if (status == FAILED)
                {
                    strcpy(outbuf, "Execution failed");
                }
            }
            else
            {
                SERVER_COMM status = send_to_server(node_array[i - 1]->IP, 60000, *cmd, inbuf, outbuf);
                if (status == PREMATURE_EXIT)
                {
                    strcpy(outbuf, "Connection closed prematurely");
                }
                else if (status == CONNECTION_FAILED)
                {
                    strcpy(outbuf, "Connection Failed");
                }
            }

            memcpy(message.output, (void *)outbuf, MAXOUTSIZE);
            message.msgtype = i;
            msgsnd(msgqid, (void *)&message, sizeof(message), 0);
            exit(0);
        }
    }
    int remaining = num_nodes;

    while (remaining--)
    {
        int re;
        if ((re = msgrcv(msgqid, &message, sizeof(message), -1 * num_nodes, 0)) < 0)
        {
            perror("Message Recieve:broadcast|");
            return;
        }
        if (printoutput)
            printf("\n\033[0;33mNode %s:\033[0m : %s", node_array[message.msgtype - 1]->name, message.output);
    }
    msgctl(msgqid, IPC_RMID, NULL);
    int pid, status;
    while ((pid = (waitpid(-1, &status, WNOHANG)) > 0))
    {
        //Reclaimed child status
    }
}
void test_connections()
{
    int msgqid = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL | 0666);
    test_conn_msg message;

    struct sockaddr_in servadr;
    servadr.sin_family = AF_INET;
    printf("%s %s(client)", node_array[self_node - 1]->name, node_array[self_node - 1]->IP);
    for (int i = 1; i <= num_nodes; i++)
    {
        int fork_ret = fork();
        if (fork_ret == 0)
        {
            inet_pton(AF_INET, node_array[i - 1]->IP, &servadr.sin_addr);
            servadr.sin_port = htons(60000);
            int clisoc = socket(AF_INET, SOCK_STREAM, 0);
            fcntl(clisoc, F_SETFL, O_NONBLOCK);
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(clisoc, &fdset);
            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            int ret, so_error, len = sizeof(so_error);
            ret = connect(clisoc, (struct sockaddr *)&servadr, sizeof(servadr));
            if (ret < 0 && errno != EINPROGRESS)
            {
                close(clisoc);
                message.msgtype = i;
                message.isActive = 0;
                msgsnd(msgqid, (void *)&message, sizeof(message), 0);
                exit(0);
            }
            if (ret == 0)
            {
                // printf("%s %s", node_array[i - 1]->name, node_array[i - 1]->IP);
                message.msgtype = i;
                message.isActive = 1;
                msgsnd(msgqid, (void *)&message, sizeof(message), 0);
                close(clisoc);
                exit(0);
            }
            else if (select(clisoc + 1, NULL, &fdset, NULL, &tv) == 1)
            {
                getsockopt(clisoc, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0)
                {
                    // printf("%s %s", node_array[i - 1]->name, node_array[i - 1]->IP);
                    message.msgtype = i;
                    message.isActive = 1;
                    msgsnd(msgqid, (void *)&message, sizeof(message), 0);
                    exit(0);
                }
                message.msgtype = i;
                message.isActive = 0;
                msgsnd(msgqid, (void *)&message, sizeof(message), 0);
                close(clisoc);
                exit(0);
            }
            else
            {
                message.msgtype = i;
                message.isActive = 0;
                msgsnd(msgqid, (void *)&message, sizeof(message), 0);
                close(clisoc);
                exit(0);
            }
            exit(0);
        }
    }
    int remaining = num_nodes;
    while (remaining--)
    {
        msgrcv(msgqid, &message, sizeof(message), -1 * num_nodes, 0);
        if (message.isActive == 1)
        {
            printf("\n%s %s", node_array[message.msgtype - 1]->name, node_array[message.msgtype - 1]->IP);
        }
    }
    msgctl(msgqid, IPC_RMID, NULL);
    int pid, status;
    while ((pid = (waitpid(-1, &status, WNOHANG)) > 0))
    {
        //Reclaimed Child Status
    }
}

void execute_command(char **cmd_buf)
{
    int printoutput = 1;
    char *inbuf = (char *)malloc(MAXOUTSIZE);
    memset((void *)inbuf, EOF, MAXOUTSIZE);
    char *outbuf = (char *)malloc(MAXOUTSIZE);
    memset((void *)outbuf, EOF, MAXOUTSIZE);
    int num_tokens;
    char pipe_char = '|';
    char **tokens = tokenise_string(cmd_buf, &num_tokens, pipe_char);
    int p[2]; //pipe for redriecting inputs and outputs;
    pipe(p);
    for (int i = 0; i < num_tokens; i++)
    {
        int server_node;
        char *cmd_to_send_to_server = get_actual_cmd(&tokens[i], &server_node);
        if (strcmp("nodes", cmd_to_send_to_server) == 0)
        {
            test_connections();
            printoutput = 0;
        }
        else if (strcmp("clustertop", cmd_to_send_to_server) == 0)
        {
            char *new_cmd = malloc(40);
            memset((void *)new_cmd, '\0', 40);
            strcpy(new_cmd, "free");
            printf("\n\033[0;32m------------Memory Usage------------\033[0m\n");
            broadcast(&new_cmd, 1);
            // memset((void *)new_cmd, '\0', 40);
            sleep(3);
            strcpy(new_cmd,"iostat -c");
            printf("\n\033[0;32m------------CPU Usage------------\033[0m\n");
            broadcast(&new_cmd, 1);
            free(new_cmd);
            printoutput = 0;
        }
        else if (server_node == 0)
        {
            printoutput = 0;
            broadcast(&cmd_to_send_to_server, 1);
        }
        else if (server_node == self_node)
        {
            int use_stdin;
            if (i == 0)
            {
                use_stdin = 1;
            }
            else
            {
                use_stdin = 0;
            }
            SERVER_COMM status = run_on_client(&cmd_to_send_to_server, use_stdin, &inbuf, &outbuf);
            if (status == FAILED)
            {
                printf("Execution failed on node %s", node_array[server_node - 1]->name);
                printoutput = 0;
                break;
            }
        }
        else
        {
            char *ip = node_array[server_node - 1]->IP;
            // char*ip="127.0.0.1";
            SERVER_COMM status = send_to_server(ip, 60000, cmd_to_send_to_server, inbuf, outbuf);
            if (status == CONNECTION_FAILED)
            {
                printf("Could not connect to node %s", node_array[server_node - 1]->name);
                printoutput = 0;
                break;
            }
            else if (status == PREMATURE_EXIT)
            {
                printf("Connection with %s terminated prematurely [CAUSE MIGHT BE INVALID COMMAND]", node_array[server_node - 1]->name);
                printoutput = 0;
                break;
            }
        }

        memcpy((void *)inbuf, (void *)outbuf, MAXOUTSIZE);
        free(cmd_to_send_to_server);
    }
    // char* out=get_actual_cmd(&cmd_buf,&self_node);

    if (printoutput)
        printf("%s", outbuf);
    free(inbuf);
    free(outbuf);
    for (int i = 0; i < num_tokens; i++)
    {
        free(tokens[i]);
    }
    free(tokens);
}

int main(int argc,char* argv[])
{
    if(argc!=2){
        printf("Path for config file is required"); 
        exit(1);
    }
    setbuf(stdout, NULL);
    getlocalIPs();
    FILE *fp = fopen(argv[1], "r");
    if(fp==NULL){
        printf("Config file could not be opened");
        exit(1);
    }
    read_config_file(fp);
    fclose(fp);
    // self_node = 4;
    const char *homedir;
    homedir = getpwuid(getuid())->pw_dir;
    char *cd_cmd = malloc(50);
    strcpy(cd_cmd, "cd ");
    strcpy(cd_cmd + 3, homedir);
    broadcast(&cd_cmd, 0); //All nodes must start their execution in home directory of user running the client
    free(cd_cmd);

    while (1)
    {
        char *cmd_buf = malloc(MAXCMDSIZE);
        input_prompt(&cmd_buf);
        execute_command(&cmd_buf);
        free(cmd_buf);
    }
}
