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
#define PORT "8000"
#define MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

struct client_details 
{
    int connfd;
    struct client_details* next;
};

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
struct client_details* client_list = NULL;

// give IPV4 or IPV6  based on the family set in the sa
void *get_in_addr(struct sockaddr *sa){
	if(sa->sa_family == AF_INET){
		return &(((struct sockaddr_in*)sa)->sin_addr);	
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void generate_random_mask(uint8_t *mask) {
    srand(time(NULL));

    // Generate a random 32-bit mask
    for (size_t i = 0; i < 4; ++i) {
        mask[i] = rand() & 0xFF;
    }
}

// Function to mask payload data
void mask_payload(uint8_t *payload, size_t payload_length, uint8_t *mask) {
    for (size_t i = 0; i < payload_length; ++i) {
        payload[i] ^= mask[i % 4];
    }
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

// Function to send WebSocket frame to the client
int send_websocket_frame (int client_socket, uint8_t fin, uint8_t opcode, char *payload) 
{
    // Encode the WebSocket frame
    uint8_t encoded_data [1024];
    int encoded_size = encode_websocket_frame (fin, opcode, 0, strlen (payload), (uint8_t *)payload, encoded_data);

    // Send the encoded message back to the client
    ssize_t bytes_sent = send (client_socket, encoded_data, encoded_size, 0);
    if (bytes_sent == -1) 
    {
        perror("Send failed");
        return -1;
    }

    printf ("Message sent to client\n");

    return 0;
}

void broadcast_message (char* message, int sender_connfd) 
{
    struct client_details* current = client_list;

    while (current != NULL) 
    {
        if (current -> connfd != sender_connfd)
            send_websocket_frame (current -> connfd, 1, 1, message);
        current = current -> next;
    }
}

void remove_client (int connfd) 
{
    pthread_mutex_lock (&mutex1);

    struct client_details* current = client_list;
    struct client_details* prev = NULL;

    while (current != NULL) 
    {
        if (current->connfd == connfd) 
        {
            if (prev == NULL)
                client_list = current -> next;
            else
                prev -> next = current -> next;
    
            free(current);
            break;
        }

        prev = current;
        current = current -> next;
    }

    pthread_mutex_unlock (&mutex1);
}

void* handle_client (void* arg) 
{
    int connfd = *((int*)arg);
    struct client_details* new_client = (struct client_details*) malloc (sizeof (struct client_details));
    new_client -> connfd = connfd;
    new_client -> next = client_list;

    pthread_mutex_lock (&mutex1);
    client_list = new_client;
    pthread_mutex_unlock (&mutex1);

    // Notify all clients about the new user
    char message [128];
    sprintf (message, "User %d has joined the chat.", connfd);
    broadcast_message (message, connfd);

    // Receive and broadcast messages
    char buffer[1024];
    while (1) 
    {
        ssize_t bytes_received = recv (connfd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
            break;

        buffer [bytes_received] = '\0';
        char full_message [1136];
        sprintf (full_message, "User %d: %s", connfd, buffer);

        // Broadcast the message to all clients
        broadcast_message (full_message, connfd);
    }

    // Remove the disconnected client from the list
    remove_client (connfd);

    // Notify all clients about the user leaving
    sprintf (message, "User %d has left the chat.", connfd);
    broadcast_message (message, connfd);

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
    char *key_end = strchr (key_start, "\r\n");
    
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

    printf("WebSocket handshake complete\n");
}

int server_creation()
{
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int yes = 1;
	int rv;
	memset (&hints,0,sizeof (hints));
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
	if(listen(sockfd,BACKLOG) == -1){ 
		perror("listen");
		exit(1); 
	} 
	return sockfd;
}

int connection_accepting (int sockfd)
{
	int connfd;
	struct sockaddr_storage their_addr;
	char s[INET6_ADDRSTRLEN];
	socklen_t sin_size;
	
	sin_size = sizeof (their_addr); 
	connfd = accept (sockfd, (SA*)&their_addr, &sin_size); 
	if(connfd == -1){ 
		perror("\naccept error\n");
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
        connfd = connection_accepting ();
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
