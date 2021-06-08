#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#define MAX_LEN 4096

struct Qmessage *new_Qmessage(int sd, char *mess, int to_fd);
struct Queue *createQueue();
void enQueue(struct Queue *q, int sd, char *mess, int to_fd);
void deQueue(struct Queue *q);
void delete_Qmessage(struct Queue *q, char *mess, int fd, int to_fd);
void delete_mess_from_fd(int fd, struct Queue *q);
void handle_sigint(int sig);
void alloc_handle();

struct Qmessage
{
    char message[MAX_LEN];
    int from;
    int to;
    struct Qmessage *next;
};

struct Queue
{
    struct Qmessage *front, *rear;
};

/*********Global Variables*********/

int main_socket = -1;
struct Queue *q = NULL;
int *new_fds = NULL;

/**********************************/

int main(int argc, char *argv[])
{

    /********************************usage validtaion*************************************/

    if (argc != 2)
    {
        printf("worng usage\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    if (port < 0)
    {
        fprintf(stderr, "undefined port\n");
        exit(EXIT_FAILURE);
    }

    /********************************initialization*************************************/

    signal(SIGINT, handle_sigint);

    int rc;
    int fds_length = 15; /* Initial size */

    new_fds = (int *)calloc(fds_length, sizeof(int));

    q = createQueue();

    fd_set fd;
    fd_set cpy_rfd, cpy_wfd; /* dirty copy */

    char buffer[MAX_LEN]; /* read the messages to this buffer */

    int maxfds = 0;

    /********************************create the socket*************************************/

    if ((main_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket failed\n");
        exit(EXIT_FAILURE);
    }

    /********************************bind the socket to a port*************************************/

    struct sockaddr_in srv;                  /* used by bind() */
    srv.sin_family = PF_INET;                /* use the Internet addr family */
    srv.sin_port = htons(port);              /* bind socket ‘fd’ to port */
    srv.sin_addr.s_addr = htonl(INADDR_ANY); /* bind: a client may connect to any of my addresses */

    if (bind(main_socket, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
        perror("bind filed\n");
        close(main_socket);
        exit(EXIT_FAILURE);
    }

    /********************************listen on the socket*************************************/

    if (listen(main_socket, 5) < 0) /* listen to max number of request client */
    {
        perror("listen filed\n");
        close(main_socket);
        exit(EXIT_FAILURE);
    }

    /********************************main loop*************************************/

    FD_ZERO(&fd); 
    FD_SET(main_socket, &fd);

    while (1)
    {
        cpy_rfd = fd;

        if (q->front == NULL) /* Queue is empty */
            FD_ZERO(&cpy_wfd);

        else //Queue is not empty
            cpy_wfd = fd;

        /**************select**************/
        rc = select(getdtablesize(), &cpy_rfd, &cpy_wfd, NULL, NULL);

        if (rc < 0)
        {
            perror("error: select filed\n");
            alloc_handle();
            exit(EXIT_FAILURE);
        }

        /**************accept**************/
        if (FD_ISSET(main_socket, &cpy_rfd))
        {
            if (maxfds > fds_length - 1)
            {
                int *temp_new_fds = (int *)realloc(new_fds, sizeof(int) * fds_length * 2);

                if( temp_new_fds == NULL)
                {
                    perror("error:realloc filed\n");
                    alloc_handle();
                    exit(EXIT_FAILURE);
                }

                new_fds = temp_new_fds;

                int k = fds_length;
                for (; k < fds_length * 2; k++)
                    new_fds[k] = 0;

                fds_length = fds_length * 2;
            }

            new_fds[maxfds] = accept(main_socket, NULL, NULL);

            if (new_fds[maxfds] < 0)
            {
                perror("accept filed\n");
                alloc_handle();
                exit(EXIT_FAILURE);
            }

            else
            {
                FD_SET(new_fds[maxfds], &fd);
                maxfds++;
            }
        }
        /**************read**************/
        int index;
        for (index = 0; index < maxfds; index++)
        {
            if (new_fds[index] != 0)
            {
                if (FD_ISSET(new_fds[index], &cpy_rfd))
                {
                    rc = read(new_fds[index], buffer, MAX_LEN);

                    if (rc == 0)
                    {
                        printf("server is ready to read from socket %d\n", new_fds[index]);
                        FD_CLR(new_fds[index], &fd);
                        close(new_fds[index]);
                        new_fds[index] = 0;
                    }

                    else if (rc > 0)
                    {
                        printf("server is ready to read from socket %d\n", new_fds[index]);
                        buffer[rc] = '\0';

                        /* update the queue */
                        int j;
                        for (j = 0; j < maxfds; j++)
                        {
                            if (j != index)
                                if (new_fds[index] != 0)
                                    enQueue(q, new_fds[index], buffer, new_fds[j]);
                        }
                    }

                    else
                    {
                        perror("error:read failed\n");
                        alloc_handle();
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        /**************write**************/
        struct Qmessage *temp = q->front;
        struct Qmessage *next;
        while (temp != NULL)
        {
            next = temp->next;

            int temp_fd = temp->to;
            if (FD_ISSET(temp_fd, &cpy_wfd))
            {
                printf("server is ready to write to socket %d\n", temp_fd);

                char new_buffer[MAX_LEN];
                strcpy(new_buffer, "guest");
                char from[3];
                sprintf(from, "%d", temp->from);
                strcat(new_buffer, from);
                strcat(new_buffer, ": ");
                strcat(new_buffer, temp->message);

                int size = strlen(new_buffer);
                rc = write(temp_fd, new_buffer, size);

                delete_Qmessage(q, temp->message, temp->from, temp->to);
            }

            temp = next;
        }
    }
}

/*********************************************************************/
struct Qmessage *new_Qmessage(int sd, char *mess, int to_fd)
{
    struct Qmessage *temp = (struct Qmessage *)malloc(sizeof(struct Qmessage));
    temp->from = sd;
    temp->to = to_fd;
    temp->next = NULL;

    if (mess != NULL)
        strcpy(temp->message, mess);

    else
    {
        printf("NULL MESSAGE\n");
    }

    return temp;
}

/*********************************************************************/
struct Queue *createQueue()
{
    struct Queue *q = (struct Queue *)malloc(sizeof(struct Queue));
    q->front = NULL;
    q->rear = NULL;
    return q;
}

/*********************************************************************/
void enQueue(struct Queue *q, int sd, char *mess, int to_fd)
{
    struct Qmessage *temp = new_Qmessage(sd, mess, to_fd);

    // If queue is empty, then new node is front and rear both
    if (q->rear == NULL)
    {
        q->front = q->rear = temp;
        return;
    }

    q->rear->next = temp;
    q->rear = temp;
}

/*********************************************************************/
void deQueue(struct Queue *q)
{
    // If queue is empty, return NULL.
    if (q->front == NULL)
        return;

    struct Qmessage *temp = q->front;
    q->front = q->front->next;

    if (q->front == NULL)
        q->rear = NULL;

    free(temp);
}
/*********************************************************************/
void delete_Qmessage(struct Queue *q, char *mess, int fd, int to_fd)
{
    struct Qmessage *temp = q->front;

    /* if the requested Qmessage is the front */
    if (temp != NULL && temp->from == fd && temp->to == to_fd && strcmp(mess, temp->message) == 0)
    {
        deQueue(q);
        return;
    }

    struct Qmessage *prev = q->front;
    while (temp != NULL)
    {
        if (temp->from == fd && temp->to == to_fd && strcmp(mess, temp->message) == 0)
            break;

        prev = temp;
        temp = temp->next;
    }

    if (temp == NULL)
        return;

    prev->next = temp->next;

    free(temp); // Free memory
}

/*********************************************************************/

void delete_mess_from_fd(int fd, struct Queue *q)
{
    struct Qmessage *temp = q->front;
    struct Qmessage *prev = q->front;

    while (temp != NULL)
    {
        if (temp->from == fd)
        {
            /* temp is the front */
            if (temp->from == fd)
            {
                if (prev == temp)
                    deQueue(q);
            }

            else
            {
                struct Qmessage *curr = temp->next;
                free(temp);
                temp = curr;
            }
        }

        else
        {
            prev = temp;
            temp = temp->next;
        }

        prev = temp;
        temp = temp->next;
    }
}

/*********************************************************************/

void alloc_handle()
{
    if (q != NULL)
    {
        struct Qmessage *temp = q->front;
        struct Qmessage *next;

        while (temp != NULL)
        {
            next = temp->next;
            free(temp);
            temp = next;
        }

        free(q);
    }

    if (main_socket != -1)
        close(main_socket);

    if (new_fds != NULL)
        free(new_fds);

}
/*********************************************************************/

void handle_sigint(int sig)
{
    alloc_handle();
    exit(0);
}
