#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#define SA struct sockaddr
#define BACKLOG 5
#define MAX_CLIENTS 10
#define PORT "8000"
#define MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

int userid = 1000;
static _Atomic int client_cnt = 0;

typedef struct client_details
{
	int userid, connfd;
	char name [20];
} client_t;

client_t *clients [MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// give IPV4 or IPV6  based on the family set in the sa
void *get_in_addr (struct sockaddr *sa)
{
	if (sa -> sa_family == AF_INET)
		return &(((struct sockaddr_in*) sa) -> sin_addr);	
	
	return &(((struct sockaddr_in6*) sa) -> sin6_addr);
}

void generate_random_mask (uint8_t *mask) 
{
    srand (time (NULL));

    // Generate a random 32-bit mask
    for (size_t i = 0; i < 4; ++i)
        mask [i] = rand () & 0xFF;
}

// Function to mask payload data
void mask_payload (uint8_t *payload, size_t payload_length, uint8_t *mask) 
{
    for (size_t i = 0; i < payload_length; ++i)
        payload [i] ^= mask [i % 4];
}

void send_frame (const uint8_t *frame, size_t length, int connfd) 
{
    ssize_t bytes_sent = send (connfd, frame, length, 0);
    if (bytes_sent == -1)
        perror("Send failed");
    else
        printf("Pong Frame sent to client\n");
}

void send_pong(const char *payload, size_t payload_length, int connfd) 
{
    uint8_t pong_frame [128];
    pong_frame [0] = 0xA;
    pong_frame [1] = (uint8_t) payload_length;
    memcpy (pong_frame + 2, payload, payload_length);
    send_frame (pong_frame, payload_length + 2, connfd);
}

void handle_ping (const uint8_t *data, size_t length, int connfd) 
{
    char ping_payload [126];
    memcpy (ping_payload, data + 2, length - 2);
    ping_payload [length - 2] = '\0';
    send_pong (ping_payload, length - 2, connfd);
}

//Add client to list
void queue_add (client_t *client)
{
	pthread_mutex_lock (&clients_mutex);
	
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!clients [i])
		{
			clients [i] = client;
			break;
		}
	}
	
	pthread_mutex_unlock (&clients_mutex);
}

//Remove client from list
void queue_remove (int connfd)
{
	pthread_mutex_lock (&clients_mutex);
	
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (clients [i] && clients [i] -> connfd == connfd)
		{
			free (clients [i]);
            clients [i] = NULL;
	        client_cnt--;
			break;
		}
	}

	pthread_mutex_unlock (&clients_mutex);
    pthread_detach (pthread_self ());
}

// Function to decode the header of a WebSocket frame
int decode_websocket_frame_header(
    uint8_t *frame_buffer,
    uint8_t *fin,
    uint8_t *opcode,
    uint8_t *mask,
    uint64_t *payload_length
) 
{
    // Extract header bytes and mask
    *fin = (frame_buffer [0] >> 7) & 1;
    *opcode = frame_buffer [0] & 0x0F;
    *mask = (frame_buffer [1] >> 7) & 1;
    int n = 0;
    
    // Calculate payload length based on header type
    *payload_length = frame_buffer [1] & 0x7F;
    if (*payload_length == 126) 
    {
        n = 1;
        *payload_length = *(frame_buffer + 2);
        *payload_length <<= 8;
        *payload_length |= *(frame_buffer + 3);
    } 
    else if (*payload_length == 127) 
    {
        n = 2;
        *payload_length = 0;
        for (int i = 2; i < 10; ++i)
            *payload_length = (*payload_length << 8) | *(frame_buffer + i);
    }

    return  (2 + (n == 1 ? 2 : (n == 2 ? 8 : 0)));
}

int process_websocket_frame (uint8_t *data, size_t length, char **decoded_data, int connfd) 
{
    uint8_t fin, opcode, mask;
    uint64_t payload_length;
    uint8_t* masking_key;

    int header_size = decode_websocket_frame_header (data, &fin, &opcode, &mask, &payload_length);
    if (header_size == -1) 
    {
        printf ("Error decoding WebSocket frame header\n");
        return -1;
    }
    
    if (mask)
    {
    	masking_key = header_size + data;
    	header_size += 2;
    }
    header_size += 2;
    
    size_t payload_offset = header_size; 
    if (opcode == 0x9) 
    {
        handle_ping (data, length, connfd);
        *decoded_data = NULL;
        return 0;
    } 
    else if (opcode == 0x8) 
    {
    	printf ("closes the connection\n");
        return -1;
    }

    *decoded_data = (char *)malloc (payload_length + 1);
    
    if (mask)
    	for (size_t i = 0; i < payload_length; ++i)
	     (*decoded_data) [i] = data [payload_offset + i] ^ masking_key [i % 4];

    (*decoded_data) [payload_length] = '\0';
    return 0;
}

// Function to encode a complete WebSocket frame
int encode_websocket_frame (
    uint8_t fin,
    uint8_t opcode,
    uint8_t mask,
    uint64_t payload_length,
    uint8_t *payload,
    uint8_t *frame_buffer
) 
{
    // Calculate header size based on payload length
    int header_size = 2;
    if (payload_length <= 125) 
    {
        // Short form
    } 
    else if (payload_length <= 65535) 
    {
        // Medium form (2 additional bytes)
        header_size += 2;
    } 
    else 
    {
        // Long form (8 additional bytes)
        header_size += 8;
    }

    // Encode header bytes
    frame_buffer [0] = (fin << 7) | (opcode & 0x0F);
    frame_buffer [1] = mask << 7;
    if (payload_length <= 125) 
        frame_buffer[1] |= payload_length;
    else if (payload_length <= 65535) 
    {
        frame_buffer [1] |= 126;
        frame_buffer [2] = (payload_length >> 8) & 0xFF;
        frame_buffer [3] = payload_length & 0xFF;
    } 
    else 
    {
        frame_buffer [1] |= 127;
        uint64_t n = payload_length;
        for (int i = 8; i >= 1; --i) 
        {
            frame_buffer [i + 1] = n & 0xFF;
            n >>= 8;
        }
    }

    // Mask payload if requested
    if (mask) 
    {
        generate_random_mask (frame_buffer + header_size - 4);
        mask_payload (payload, payload_length, frame_buffer + header_size - 4);
    }

    // Copy payload after header
    memcpy (frame_buffer + header_size, payload, payload_length);
    return header_size + payload_length; // Total frame size
}

// Function to send WebSocket frame to the client
int send_websocket_frame (int client_socket, int userid, uint8_t fin, uint8_t opcode, char *payload) 
{
    // Encode the WebSocket frame
    uint8_t encoded_data [1024];
    int encoded_size = encode_websocket_frame (fin, opcode, 0, strlen (payload), (uint8_t *)payload, encoded_data);

    // Send the encoded message back to the client
    ssize_t bytes_sent = send (client_socket, encoded_data, encoded_size, 0);
    if (bytes_sent == -1) 
    {
        perror ("Send failed");
        return -1;
    }

    printf ("Message sent to client: %d\n", userid);
    return 0;
}

void broadcast_message (char* message, int sender_connfd) 
{
    for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (clients [i] && clients [i] -> connfd != sender_connfd)
		    send_websocket_frame (clients [i] -> connfd, clients [i] -> userid, 1, 1, message);
	}
}

void send_message (char* message, int sender_connfd, int userid) 
{
    for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (clients [i] && clients [i] -> userid == userid)
        {
		    send_websocket_frame (clients [i] -> connfd, clients [i] -> userid, 1, 1, message);
            break;
        }
	}
}

//Get list of active users
char* extractActiveUsersString (int userid) 
{
    int length = 15;

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients [i] && userid != clients [i] -> userid)
        	length += 20;

    if (length == 15)
        return "No active users!!!";

    char *result = (char*)malloc (length + 10);
    if (result == NULL) 
    {
        fprintf (stderr, "Memory allocation failed.\n");
        exit (EXIT_FAILURE);
    }

    char* pos = result;
    pos += snprintf (pos, length, "Active Users: ");

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients [i] && userid != clients [i] -> userid)
        	pos += snprintf (pos, length + 1, "%s, ", clients [i] -> name);

    if (length > 15)
        *(pos - 2) = '\0';
   
    return result;
}

void* handle_client (void* arg) 
{
    int connfd = *((int*) arg), status;
    char name [30], *decoded_name = NULL;

    if (recv (connfd, name, sizeof (name), 0) <= 0)
    {
        perror ("recv");
        close (connfd);

        free (arg);
        pthread_exit (NULL);
        return NULL;
    }

    status = process_websocket_frame (name, sizeof (name), &decoded_name, connfd);
    if (status == -1)
    {
        free (arg);
        pthread_exit (NULL);
        return NULL;
    }
    else if (status != 0) 
    {
        printf("Error processing WebSocket frame\n");
        close(connfd);
        free (arg);
        pthread_exit (NULL);
        return NULL;
    } 

    client_t* new_client = (client_t*) malloc (sizeof (client_t));
    new_client -> connfd = connfd;
    new_client -> userid = userid++;
    strcpy (new_client -> name, decoded_name);
    
    queue_add (new_client);

    // Notify all clients about the new user
    char message [128];
    sprintf (message, "%s has joined the chat.", new_client -> name);
    broadcast_message (message, connfd);

    // Receive and broadcast messages
    while (1) 
    {
        char buffer [1024], reciever_name [20];
        char *decoded_data = NULL;

        ssize_t bytes_received = recv (connfd, buffer, sizeof (buffer), 0);
        if (bytes_received <= 0)
            break;
        buffer [bytes_received] = '\0';

        int status = process_websocket_frame (buffer, bytes_received, &decoded_data, connfd);
        if (status == -1)
            break;
        else if (status != 0) 
        {
            printf("Error processing WebSocket frame\n");
            close(connfd);
            continue;
        } 

        char full_message [1136], id_str [5];
        int id;
        if (strstr (decoded_data, "activeUsers"))
        {
            strcpy (full_message, extractActiveUsersString (new_client -> userid));
            send_websocket_frame (new_client -> connfd, new_client -> userid, 1, 1, full_message);
            send_websocket_frame (new_client -> connfd, new_client -> userid, 1, 1, "To message particular user (<userid>: <message>)");
        }
        else if (strchr (decoded_data, ':'))
        {
            int end = strchr (decoded_data, ':') - decoded_data;
            strncpy (reciever_name, decoded_data, end);
            reciever_name [end] = '\0';
            printf ("Send to: %s\n", reciever_name);
            //id = atoi (id_str);
            
            sprintf (full_message, "%s: %s", new_client -> name, decoded_data + 6);
            send_message (full_message, connfd, id);
        }
        else
        {
            sprintf (full_message, "%s: %s", new_client -> name, decoded_data);
            printf ("Broadcasting: %s\n", full_message);
            // Broadcast the message to all clients
            broadcast_message (full_message, connfd);
        }
    }

    // Notify all clients about the user leaving
    sprintf (message, "User %s has left the chat.", new_client -> name);
    broadcast_message (message, connfd);

    // Remove the disconnected client from the list
    queue_remove (connfd);

    // Close the connection
    close (connfd);

    free (arg);
    pthread_exit (NULL);
}

void calculate_websocket_accept (char *client_key, char *accept_key) 
{
    char combined_key [1024];
    strcpy (combined_key, client_key);
    strcat (combined_key, MAGIC_STRING);

    unsigned char sha1_hash [SHA_DIGEST_LENGTH];
    SHA1 ((unsigned char *) combined_key, strlen (combined_key), sha1_hash);

    // Base64 encode the SHA-1 hash
    BIO *b64 = BIO_new (BIO_f_base64 ());
    BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

    BIO *bio = BIO_new (BIO_s_mem ());
    BIO_push (b64, bio);

    BIO_write (b64, sha1_hash, SHA_DIGEST_LENGTH);
    BIO_flush (b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr (b64, &bptr);

    strcpy (accept_key, bptr -> data);

    // Remove trailing newline character
    size_t len = strlen (accept_key);
    if (len > 0 && accept_key [len - 1] == '\n')
        accept_key [len - 1] = '\0';

    BIO_free_all (b64);
}

void handle_websocket_upgrade (int client_socket, char *request) 
{
    // Check if it's a WebSocket upgrade request
    if (strstr (request, "Upgrade: websocket") == NULL) 
    {
        fprintf (stderr, "Not a WebSocket upgrade request\n");
        return;
    }

    // Extract the value of Sec-WebSocket-Key header
    char *key_start = strstr (request, "Sec-WebSocket-Key: ") + 19;
    char *key_end = strstr (key_start, "\r\n");
    
    if (!key_start || !key_end) 
    {
        fprintf (stderr, "Invalid Sec-WebSocket-Key header\n");
        return;
    }
    *key_end = '\0';

    // Calculate Sec-WebSocket-Accept header
    char accept_key [1024];
    calculate_websocket_accept (key_start, accept_key);

    // Send WebSocket handshake response
     char *upgrade_response_format =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n";

    char response [2048];
    sprintf (response, upgrade_response_format, accept_key);
    send (client_socket, response, strlen (response), 0);

    printf ("WebSocket handshake complete\n");
}

int server_creation ()
{
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int yes = 1;
	int rv;
	memset (&hints, 0, sizeof (hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;// my ip
	
	// set the address of the server with the port info.
	if ((rv = getaddrinfo (NULL, PORT, &hints, &servinfo)) != 0)
    {
		fprintf (stderr, "getaddrinfo: %s\n",gai_strerror (rv));	
		return 1;
	}
	
	// loop through all the results and bind to the socket in the first we can
	for (p = servinfo; p != NULL; p = p -> ai_next)
    {
		sockfd = socket (p -> ai_family, p -> ai_socktype, p -> ai_protocol);
		if (sockfd == -1)
        { 
			perror ("server: socket\n"); 
			continue; 
		} 
		
        //Reuse port
		if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int)) == -1)
        {
			perror ("setsockopt");
			exit (1);	
		}
		    	
		// it will help us to bind to the port.
		if (bind (sockfd, p -> ai_addr, p -> ai_addrlen) == -1) 
        {
			close (sockfd);
			perror ("server: bind");
			continue;
		}
		break;
	}
	
	// server will be listening with maximum simultaneos connections of BACKLOG
	if (listen (sockfd, BACKLOG) == -1)
    { 
		perror ("listen");
		exit (1); 
	} 
	return sockfd;
}

int connection_accepting (int sockfd)
{
	int connfd;
	struct sockaddr_storage their_addr;
	char s [INET6_ADDRSTRLEN];
	socklen_t sin_size;
	
	sin_size = sizeof (their_addr); 
	connfd = accept (sockfd, (SA*)&their_addr, &sin_size); 
	if (connfd == -1)
    { 
		perror ("\naccept error\n");
		return -1;
	} 

	//printing the client name
	inet_ntop (their_addr.ss_family, get_in_addr ((struct sockaddr *)&their_addr), s, sizeof (s));
	printf ("\nserver: got connection from %s\n", s);

	// Handle WebSocket upgrade
    char buffer [2048];
    ssize_t len = recv (connfd, buffer, sizeof (buffer), 0);
    if (len > 0) 
    {
        buffer [len] = '\0';
        handle_websocket_upgrade (connfd, buffer);
    }
	
	return connfd;
}

int main() 
{
    int sockfd, connfd;
    pthread_t thread_id;

    sockfd = server_creation ();

    printf ("Chat server: waiting for connections on port %s...\n", PORT);

    while (1) 
    {
        connfd = connection_accepting (sockfd);
        if (connfd < 0) 
            continue;

        int* new_connfd = (int*)malloc (sizeof (int));
        *new_connfd = connfd;

        if (pthread_create (&thread_id, NULL, handle_client, (void*)new_connfd) != 0) 
        {
            perror ("pthread_create");
            close (connfd);
        }

        if (pthread_detach (thread_id) != 0) 
            perror ("pthread_detach");
    }

    close (sockfd);
    return 0;
}