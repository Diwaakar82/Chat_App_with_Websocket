#include <bits/stdc++.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <errno.h>
#include <signal.h>
#include <thread>
#include <mutex>
#include <jsoncpp/json/json.h>

#define SA struct sockaddr
#define BACKLOG 20
#define MAX_CLIENTS 10
#define PORT "8000"
#define MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

using namespace std;
using namespace Json;

class TCPServer 
{
    int sockfd;

    // give IPV4 or IPV6  based on the family set in the sa
    void *get_in_addr (struct sockaddr *sa)
    {
        if (sa -> sa_family == AF_INET)
            return &(((struct sockaddr_in*) sa) -> sin_addr);	
        
        return &(((struct sockaddr_in6*) sa) -> sin6_addr);
    }

    int server_creation ()
    {
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

    public:

    TCPServer ()
    {
        server_creation ();
    }

    int getServerSocket ()
    {
        return sockfd;
    }

    int connection_accepting ()
    {
        int connfd;
        struct sockaddr_storage their_addr;
        char s [INET6_ADDRSTRLEN];
        socklen_t sin_size;
        
        sin_size = sizeof (their_addr); 
        connfd = accept (sockfd, (SA*)&their_addr, &sin_size); 
        if (connfd == -1)
        {
            // perror ("\naccept error\n");
            return -1;
        } 

        //printing the client name
        inet_ntop (their_addr.ss_family, get_in_addr ((struct sockaddr *)&their_addr), s, sizeof (s));
        printf ("\nserver: got connection from %s\n", s);
        
        return connfd;
    }

    // send request in the uint8_t* form to the client connected to the client_socket with the length len
    int sendRequest (int client_socket, uint8_t *buff, int len)
    {
        return send (client_socket, buff, len, 0);
    }

	// receive request in the uint8_t* form from the client connected to the client_socket with the maximum length len
    int getResponse (int client_socket, uint8_t *buff, int len)
    {
        return recv (client_socket, buff, len, 0);
    }

	// send response in the char* form to the client connected to the client_socket with the length len
    int sendRequest (int client_socket, char *buff,int len)
    {
        return send (client_socket, buff, len, 0);
    }

	// receive request in the char* form from the client connected to the client_socket with the maximum length len
    int getResponse (int client_socket, char *buff,int len)
    {
        return recv (client_socket, buff, len, 0);
    }
};

class WebSocketServer
{
    TCPServer tcp;
    // char upgrade_response_format [200] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n";
    int userid = 1000;

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
            //handle_ping (data, length, connfd);
            *decoded_data = NULL;
            return 0;
        } 
        else if (opcode == 0x8) 
            return -1;

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

    void calculate_websocket_accept(const char *clientKey, char *acceptKey) 
    {
        char combinedKey [1024] = "";
        strcpy(combinedKey, clientKey);
        //cout<<"clientkey:"<<clientKey<<endl;
        strcat (combinedKey, MAGIC_STRING);
        //cout<<"combinedkey:"<<combinedKey<<endl;
        memset (acceptKey, '\0', 50);
        unsigned char sha1Hash [SHA_DIGEST_LENGTH];
        SHA1 (reinterpret_cast <const unsigned char*>(combinedKey), strlen (combinedKey), sha1Hash);

        BIO* b64 = BIO_new (BIO_f_base64 ());
        BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

        BIO* bio = BIO_new (BIO_s_mem ());
        BIO_push (b64, bio);

        BIO_write (b64, sha1Hash, SHA_DIGEST_LENGTH);
        BIO_flush (b64);

        BUF_MEM* bptr;
        BIO_get_mem_ptr (b64, &bptr);

        strcpy (acceptKey, bptr -> data);

        size_t len = strlen (acceptKey);
        
        if (len > 0 && acceptKey [len - 1] == '\n') 
        {
            acceptKey [len - 1] = '\0';
        }
        acceptKey [28] = '\0';
        //cout<<"acceptKey:"<<acceptKey<<endl;

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
        char *upgrade_response_format = strdup (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n");

        char response [2048];
        sprintf (response, upgrade_response_format, accept_key);
        send (client_socket, response, strlen (response), 0);

        printf ("WebSocket handshake complete\n");
    }

    public:
    int webSocketCreate ()
    {
        char buffer [2048];
        int client_socket = tcp.connection_accepting ();
        

        if (client_socket == -1)
        {
            return -1;
        }

        // cout << "&&&\n";
        int len = tcp.getResponse (client_socket, buffer, 2048);

        buffer [len] = '\0';
        // printf ("Header: %s\n", buffer);

        handle_websocket_upgrade (client_socket, buffer);
        userid++;
        return client_socket;
    }

    // Function to send WebSocket frame to the client
    int send_websocket_frame (int client_socket, uint8_t fin, uint8_t opcode, char *payload) 
    {
        // Encode the WebSocket frame
        uint8_t encoded_data [1024];

        int encoded_size = encode_websocket_frame (fin, opcode, 0, strlen (payload), (uint8_t *)payload, encoded_data);

        // Send the encoded message back to the client
        ssize_t bytes_sent = tcp.sendRequest (client_socket, encoded_data, encoded_size);
        //printf ("$$$%s\n", payload);

        if (bytes_sent == -1) 
        {
            perror ("Send failed");
            return -1;
        }
        return 0;
    }

    void sendCloseFrame (int client_socket)
    {
        uint8_t close_frame [] = {0x88, 0x00};
        tcp.sendRequest (client_socket, close_frame, sizeof (close_frame));

        cout << "Close frame sent to the client" << endl;
    }

    int recv_websocket_frame (char **decodedData, int client_socket)
    {
        uint8_t data [2048]; 
        size_t length = 2048;
        int len = 0;
    
        if ((len = tcp.getResponse (client_socket, data, length)) == -1)
        {
            return -1; 
        }

        return process_websocket_frame (data, length, decodedData, client_socket);
    }

    int getUserID ()
    {
        return userid;
    }
};

class client
{
    int userid, connfd;
    char name [20];

    public:
    void setConnfd (int connfd)
    {
        this -> connfd = connfd;
    }

    void setUserID (int userid)
    {
        this -> userid = userid;
    }
    
    void setName (char name [20])
    {
        strcpy (this -> name, name);
    }

    int getConnfd ()
    {
        return connfd;
    }

    int getUserID ()
    {
        return userid;
    }

    char *getName ()
    {
        return name;
    }
};

class ChatServer
{
    unordered_map <int, client> clients;
    WebSocketServer websocket = WebSocketServer ();
    mutex clients_mutex;

    public:
    void startServer ()
    {
        cout << "Chat server: waiting for connections on port " << PORT << "....\n";

        while (1)
        {
            int connfd = addClient (websocket);
            if (connfd < 0) 
                continue;

            clients_mutex.lock ();
            thread clientThread (&ChatServer::handle_client, this, connfd);

            clientThread.detach ();
        }
    }

    int addClient (WebSocketServer &websocket)
    {
        client new_client;
        new_client.setConnfd (websocket.webSocketCreate ());

        if (new_client.getConnfd () < 0)
        {
            return -1;
        }

        new_client.setUserID (-1);

        clients_mutex.lock ();
        clients [new_client.getConnfd ()] = new_client;
        clients_mutex.unlock ();

        return new_client.getConnfd ();
    }

    void handleClose (int connfd)
    {
        clients_mutex.lock ();

        close (clients [connfd].getConnfd ());
        clients.erase (connfd);
    
        clients_mutex.unlock ();
        pthread_exit (NULL);
    }

    void broadcast_message (Value &response, int sender_connfd) 
    {
        struct json_object *message;
        char temp [100];
        FastWriter fastwriter;

        for (auto i: clients)
        {
            if (i.first != sender_connfd)
            {
                if (strstr (response ["Message"].asString ().c_str (), "Message sent."))
                    response ["Message"] = temp;

                websocket.send_websocket_frame (i.first, 1, 1, strdup (fastwriter.write (response).c_str ()));
            }
            if (i.second.getConnfd () == sender_connfd)
            {
                strcpy (temp, response ["Message"].asString ().c_str ());
                response ["Message"] = "Message sent.";
                websocket.send_websocket_frame (i.first, 1, 1, strdup (fastwriter.write (response).c_str ()));
            }
        }
    }

    void send_message (Value &response, int sender_connfd, const char* username) 
    {
        int connfd;
        FastWriter fastwriter;

        for (auto i: clients)
        {
            if (strstr (username, i.second.getName ()))
            {
                response ["Status"] = 104;

                connfd = i.first;
                break;
            }
        }

        if (response ["Status"].asInt () == 104)
        {
            websocket.send_websocket_frame (connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
            response ["Message"] = "Message sent";
            websocket.send_websocket_frame (sender_connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
        }
        else
        {
            response ["Message"] = "User doesn't exist";
            websocket.send_websocket_frame (sender_connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
        }
    }

    //Get list of active users
    vector <string> extractActiveUsersString (int userid, char *status_code, char *msg) 
    {
        vector <string> users;

        for (auto i: clients)
            if (i.second.getUserID () != userid && i.second.getUserID () != -1)
                users.push_back (i.second.getName ());
        
        if (users.size ())
        {
            strcpy (status_code, "108");
            strcpy (msg, "Fetched users!!!");
        }
        else
        {
            strcpy (msg, "No active users!!!");
            strcpy (status_code, "105");
        }

        return users;
    }

    int check_name_exists (char *name)
    {
        for (auto i: clients)
            if (strcmp (i.second.getName (), name) == 0)
                return 1;
        return 0;
    }

    void* handle_client (int connfd) 
    {
        // int connfd = *((int*) arg), status;
        char name [30], *decoded_name = NULL;

        client *new_client = &clients [connfd];
        clients_mutex.unlock ();

        // Receive and broadcast messages
        while (1) 
        {
            char buffer [1024];
            char *decoded_data = NULL;
            char msg [100];
            int flag;
            Value response;

            if ((flag = websocket.recv_websocket_frame (&decoded_data, connfd)) == -1)
            {
                handleClose (connfd);
                break;
            }

            //Ping frame
            if (flag == 1)
                continue;
            
            //Close frame
            if (flag == 2)
            {
                websocket.sendCloseFrame (connfd);
                handleClose (connfd);
                break;
            }

            printf ("###%s\n", decoded_data);

            vector <string> users;
            Reader reader;
            Value parsed_data, usersJson;
            FastWriter fastwriter;
            
            reader.parse (decoded_data, parsed_data);

            switch (parsed_data ["Type"].asInt ())
            {
                case 1:
                    
                    strcpy (name, parsed_data ["Message"].asString ().c_str ());

                    response ["Type"] = 1;

                    if (!check_name_exists (name))
                    {
                        char message [50];
                        if (new_client -> getUserID () == -1)
                            new_client -> setUserID (connfd);
                        new_client -> setName (name);

                        response ["Status"] = 101;
                        response ["Message"] = "Name updated";
                        clients [connfd].setName (name);

                        websocket.send_websocket_frame (connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
                    }
                    else
                    {
                        response ["Status"] = 102;
                        response ["Message"] = "Name already exists";
                        
                        websocket.send_websocket_frame (connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
                    }
                    break;

                case 2:
                    bzero (msg, sizeof (msg));

                    response ["Type"] = 2;
                    response ["Status"] = 104;

                    sprintf (msg, "%s: ", new_client -> getName ());
                    strcat (msg, parsed_data ["Message"].asString ().c_str ());

                    response ["Message"] = msg;

                    broadcast_message (response, connfd);
                    break;

                case 3:
                    bzero (msg, sizeof (msg));

                    response ["Type"] = 3;
                    response ["Status"] = 107;

                    sprintf (msg, "%s: %s", new_client -> getName (), parsed_data ["Message"].asString ().c_str ());
                    response ["Message"] = msg;

                    //send_to end
                    send_message (response, connfd, parsed_data ["User"].asString ().c_str ());
                    break;

                case 4:
                    char status_code [4], msg [30];
                    users = extractActiveUsersString (new_client -> getUserID (), status_code, msg);

                    for (auto user: users)
                        usersJson.append (user);

                    response ["Type"] = 4;
                    response ["Status"] = status_code;
                    response ["Message"] = msg;

                    if (users.size ())
                        response ["Users"] = usersJson;

                    websocket.send_websocket_frame (connfd, 1, 1, strdup (fastwriter.write (response).c_str ()));
                    break;

                default:
                    websocket.send_websocket_frame (connfd, 1, 1, strdup ("Unkown message!!!"));
                    break;
            }
        }

        cout << "{{}}\n";
        // Notify all clients about the user leaving
        char message [1000];
        Value response;

        sprintf (message, "%s has left the chat.", new_client -> getName ());
        response ["Type"] = 5;
        response ["Status"] = 109;
        response ["Message"] = message;

        printf ("%s\n", message);
        broadcast_message (response, connfd);
        bzero (message, sizeof (message));

        // Remove the disconnected client from the list
        handleClose (connfd);
        pthread_exit (NULL);
    }
};

int main ()
{
    ChatServer chat;
    chat.startServer ();

    return 0;
}