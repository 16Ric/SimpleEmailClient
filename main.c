#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define TAG_SIZE 6
#define MSG_NUM_STR_SIZE 10
#define FOLDER_SIZE 512
#define DEFAULT_FOLDER "INBOX"
#define RETRIEVE_COMMAND "retrieve"
#define CONNECT_RESPONSE "* OK "
#define LOGIN_RESPONSE "%s OK "
#define SELECT_RESPONSE "%s OK "

// Struct for client
typedef struct {
    char *username;
    char *password;
    char *folder;
    int message_num;
    int use_tls;
    char *command;
    char *server_name;
    int connfd;
    int tag_counter;
} client_t;

// Initializing a client
client_t* init_client();

// Parsing the command line argument
void parse_command_line(int argc, char* argv[], client_t* client);

// Connecting with the server either IPv6 or IPv4
void connect_server(client_t* client);

// Checking the established connection
void check_connection(client_t* client);

// Logging in the IMAP
void login_imap(client_t* client);

// Select the specified folder
void select_folder(client_t* client);

// To escape special char for folder
void escape_special_char(char* input, char* output, int output_size);

// Fetching the whole raw email
void fetch_email(client_t* client);

// Printing the response
void print_response(int connfd, int print_index, int print_size);

// Receiving the remaining response from server
void receive_remaining_response(int connfd);

// Parsing the header fields
void parse_header_fields(client_t* client);

// Parsing "from" field
void parse_from(client_t* client);

// Parsing "to" field
void parse_to(client_t* client);

// Parsing "date" field
void parse_date(client_t* client);

// Parsing "subject" field
void parse_subject(client_t* client);

// Removing \r\n for unfolding
void remove_cr_newline(char* input);

// Printing the parsed header fields
void print_parsed_fields(int connfd, int print_index, int output_size);

// Reading the mime body
void read_mime(client_t* client);

// Returning the full body given body size
char* get_full_body(int connfd, int body_size);

// Printing the mime parts of the email
void print_mime(char* body_buffer);

// Case insensitive strstr for the mime parameters
char* insensitive_strstr(char* search, char* target);

// Getting the boundary parameter
char* get_boundary(char* content);

// Checking the starting boundary of the mime
char* check_starting_boundary(char* content, char* boundary);

// Checking the content type and charset
char* check_content_type_charset(char* content);

// Checking the content-type-encoding
char* check_encoding_parameter(char* content);

// Checking the end boundary of the mime
char* check_end_boundary(char* content, char* boundary);

// Listing all of the email 
void list_email(client_t* client);

// Parsing the list and print them
int parse_list_response(int connfd, char* response);


int main(int argc, char* argv[]) {
    client_t* client = init_client();
    parse_command_line(argc, argv, client);
    connect_server(client);
    check_connection(client);
    login_imap(client);
    select_folder(client);

    if (strcmp(client->command, "retrieve") == 0) {
        fetch_email(client);
    } else if (strcmp(client->command, "parse") == 0) {
        parse_header_fields(client);
    } else if (strcmp(client->command, "mime") == 0) {
        read_mime(client);
    } else if (strcmp(client->command, "list") == 0) {
        list_email(client);
    } else {
        fprintf(stderr, "Command is not given\n");
        exit(EXIT_FAILURE);
    }
    free(client);

    return 0;
}

void parse_command_line(int argc, char* argv[], client_t* client) {
    int opt;

    while ((opt = getopt(argc, argv, "u:p:f:n:t")) != -1) {
        switch (opt) {
            case 'u':
                client->username = optarg;
                break;
            case 'p':
                client->password = optarg;
                break;
            case 'f':
                client->folder = optarg;
                break;
            case 'n':
                client->message_num = atoi(optarg);
                break;
            case 't':
                client->use_tls = 1;
                break;
            default:
                fprintf(stderr, "Invalid command line input\n");
                exit(EXIT_FAILURE);
        }
    }

    if (client->username == NULL || client->password == NULL) {
        fprintf(stderr, "Username or Password not found\n");
        exit(EXIT_FAILURE);
    }

    if (argc - optind != 2) {
        fprintf(stderr, "Invalid command line input\n");
        exit(EXIT_FAILURE); 
    }

    client->command = argv[optind];
    client->server_name = argv[optind + 1];

}

client_t* init_client() {
    client_t* client = (client_t*)malloc(sizeof(client_t));
    if (client == NULL) {
        fprintf(stderr, "Malloc failure\n");
        exit(EXIT_FAILURE);
    }
    client->username = NULL;
    client->password = NULL;
    client->folder = DEFAULT_FOLDER;
    client->message_num = 1;
    client->use_tls = 0;
    client->command = NULL;
    client->server_name = NULL;
    client->connfd = -1;
    client->tag_counter = 1;
    return client;
}

void connect_server(client_t* client) {
    int connfd, s;
    struct addrinfo hints, *res, *rp;
    const char* port = client->use_tls ? "993" : "143";

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;         // Request IPv6
    hints.ai_socktype = SOCK_STREAM;

    s = getaddrinfo(client->server_name, port, &hints, &res);
    if (s != 0) {

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;      // Request IPv4

        s = getaddrinfo(client->server_name, port, &hints, &res);
        if (s != 0) {
            fprintf(stderr, "Error in getaddrinfo\n");
            exit(2);
        }
    }

    if (res == NULL) {
        fprintf(stderr, "Server address not found\n");
        exit(EXIT_FAILURE);
    }

    for(rp = res; rp != NULL; rp = rp->ai_next) {
        connfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        
        if (connfd == -1) continue;
        if (connect(connfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            client->connfd = connfd;
            freeaddrinfo(res);
            return;                     // Connection established
        }
        close(connfd);
    }

    freeaddrinfo(res);
    fprintf(stderr, "Failed to connect using both IPv6 and IPv4\n");
    exit(2);

}

void check_connection(client_t* client) {
    char receive_buffer[BUFFER_SIZE];
    int bytes_received;
    
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive connect response");
        exit(EXIT_FAILURE);
    }

    receive_buffer[bytes_received] = '\0';
    if (strstr(receive_buffer, CONNECT_RESPONSE) == NULL) {
        fprintf(stderr, "Connect failure\n");
        exit(EXIT_FAILURE);
    }
}

void login_imap(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received;
    char tag[TAG_SIZE];
    char check_buffer[BUFFER_SIZE];

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate login command
    snprintf(send_buffer, sizeof(send_buffer), "%s LOGIN %s %s\r\n", tag, client->username, client->password);

    // Send login command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send login command\n");
        exit(EXIT_FAILURE);
    }

    // Receive login response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive login response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    // Generate the response checker
    snprintf(check_buffer, sizeof(check_buffer), LOGIN_RESPONSE, tag);

    // Check if login was successful
    if (strstr(receive_buffer, check_buffer) == NULL) {
        printf("Login failure\n");
        exit(3);
    }
}

void select_folder(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received;
    char tag[TAG_SIZE];
    char check_buffer[BUFFER_SIZE];
    char escaped_folder[FOLDER_SIZE];

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate select command
    escape_special_char(client->folder, escaped_folder, FOLDER_SIZE);
    if (strchr(client->folder, ' ') != NULL || strchr(client->folder, '"') != NULL) {
        snprintf(send_buffer, sizeof(send_buffer), "%s SELECT \"%s\"\r\n", tag, escaped_folder);
    } else {
        snprintf(send_buffer, sizeof(send_buffer), "%s SELECT %s\r\n", tag, escaped_folder);
    }

    // Send select command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send select command\n");
        exit(EXIT_FAILURE);
    }

    // Receive select response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive select response\n");
        exit(EXIT_FAILURE);
    }

    receive_buffer[bytes_received] = '\0';
    
    // Generate response checker
    snprintf(check_buffer, sizeof(check_buffer), SELECT_RESPONSE, tag);
    
    // Check if select was successful
    if (strstr(receive_buffer, check_buffer) == NULL) {
        printf("Folder not found\n");
        exit(3);
    }
}

void escape_special_char(char *input, char *output, int output_size) {
    int j = 0;

    for (int i = 0; i < strlen(input) && j < output_size - 1; i++) {
        if (input[i] == '"' || input[i] == '\\') {
            if (j < output_size - 2) {
                output[j++] = '\\';
            } else {
                break;
            }
        }
        output[j++] = input[i];
    }
    output[j] = '\0'; // Null-terminate
}

void fetch_email(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, body_size;
    char tag[TAG_SIZE];
    char* body_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate fetch command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[]\r\n", tag, client->message_num);

    // Send fetch command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send fetch command");
        exit(EXIT_FAILURE);
    }

    // Receive fetch response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive select response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';
    
    body_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[] {%d}", &body_size) == 1 && body_start != NULL) {
        body_start += 2;                        // Move past the \r\n to the start of the body
        int body_index = body_start - receive_buffer;
        print_response(client->connfd, body_index, body_size - 1);
        printf("\n");
        exit(0);
    } else {
        printf("Message not found\n");
        exit(3);
    }
}

void print_response(int connfd, int print_index, int print_size) {
    int total_received = 0;
    int bytes_received;
    
    char* print_buffer = (char*)malloc(sizeof(char) * (print_size + 1));
    if (print_buffer == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        exit(EXIT_FAILURE);
    }

    // Read the initial response line
    if (print_index > 0) {
        char response_buffer[print_index];
        int response_bytes_received = recv(connfd, response_buffer, print_index, 0);
        if (response_bytes_received < 0) {
            fprintf(stderr, "Failed to receive header\n");
            free(print_buffer);
            exit(EXIT_FAILURE);
        }
    }

    // Read the entire parsed content
    while (total_received < print_size) {
        bytes_received = recv(connfd, print_buffer + total_received, print_size - total_received, 0);
        if (bytes_received < 0) {
            fprintf(stderr, "Failed to receive body content\n");
            free(print_buffer);
            exit(EXIT_FAILURE);
        }
        total_received += bytes_received;
    }

    print_buffer[total_received] = '\0'; // Null-terminate the buffer

    // Print the parsed content
    printf("%s", print_buffer);
    free(print_buffer); // Free allocated memory
    receive_remaining_response(connfd);
}

void receive_remaining_response(int connfd) {
    char receive_buffer[BUFFER_SIZE];
    int bytes_received;
    
    bytes_received = recv(connfd, receive_buffer, sizeof(receive_buffer) - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive remaining response");
        exit(EXIT_FAILURE);
    }

}

void parse_header_fields(client_t* client) {
    parse_from(client);
    parse_to(client);
    parse_date(client);
    parse_subject(client);
}

void parse_from(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, from_size;
    char tag[TAG_SIZE];
    char* from_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate parse from command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[HEADER.FIELDS (FROM)]\r\n", tag, client->message_num);

    // Send parse from command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send parse from command");
        exit(EXIT_FAILURE);
    }

    // Receive parse from response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive parse from response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';
    
    from_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[HEADER.FIELDS (FROM)] {%d}", &from_size) == 1 && from_start != NULL) {
        from_start += 2;        // Move past the \r\n to the start of the from
        from_start += 6;        // Move past the "From: ":
        int from_index = from_start - receive_buffer;
        printf("From: ");
        print_parsed_fields(client->connfd, from_index, from_size - 8);
        printf("\n");
    } else {
        fprintf(stderr, "From response not found\n");
        exit(EXIT_FAILURE);
    }
}

void parse_to(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, to_size;
    char tag[TAG_SIZE];
    char* to_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate parse to command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[HEADER.FIELDS (TO)]\r\n", tag, client->message_num);

    // Send parse to command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send parse to command");
        exit(EXIT_FAILURE);
    }

    // Receive parse to response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive parse to response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    to_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[HEADER.FIELDS (TO)] {%d}", &to_size) == 1 && to_start != NULL) {
        to_start += 2;          // Move past the \r\n to the start of the to
        to_start += 4;          // Move past the "To: ":
        int to_index = to_start - receive_buffer;
        
        if (to_size > 2) {
            printf("To: ");
            print_parsed_fields(client->connfd, to_index, to_size - 6);
            printf("\n");
        } else {
            printf("To:\n");
            receive_remaining_response(client->connfd);
        }   
    }
}

void parse_date(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, date_size;
    char tag[TAG_SIZE];
    char* date_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate parse from command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[HEADER.FIELDS (DATE)]\r\n", tag, client->message_num);

    // Send parse from command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send parse date command");
        exit(EXIT_FAILURE);
    }

    // Receive parse date response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive parse date response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    date_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[HEADER.FIELDS (DATE)] {%d}", &date_size) == 1 && date_start != NULL) {
        date_start += 2;            // Move past the \r\n to the start of the date
        date_start += 6;            // Move past the "Date: ":
        int date_index = date_start - receive_buffer;
        printf("Date: ");
        print_parsed_fields(client->connfd, date_index, date_size - 8);
        printf("\n");
    } else {
        fprintf(stderr, "Date response not found\n");
        exit(EXIT_FAILURE);
    }
}

void parse_subject(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, subject_size;
    char tag[TAG_SIZE];
    char* subject_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate parse from command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[HEADER.FIELDS (SUBJECT)]\r\n", tag, client->message_num);

    // Send parse from command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send parse subject command");
        exit(EXIT_FAILURE);
    }

    // Receive parse from response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive parse subject response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    subject_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[HEADER.FIELDS (SUBJECT)] {%d}\r\n", &subject_size) == 1 && subject_start != NULL) {
        subject_start += 2;             // Move past the \r\n to the start of the subject
        subject_start += 9;             // Move past the "Subject: ":
        int subject_index = subject_start - receive_buffer;
        
        printf("Subject: ");
        if (subject_size > 2) {
            print_parsed_fields(client->connfd, subject_index, subject_size - 11);
            printf("\n");
        } else {
           printf("<No subject>\n");
           receive_remaining_response(client->connfd);
        }
        
    } else {
        fprintf(stderr, "Subject response not found\n");
        exit(EXIT_FAILURE);
    }
}

void print_parsed_fields(int connfd, int print_index, int print_size) {
    int total_received = 0;
    int bytes_received;
    
    char* print_buffer = (char*)malloc(sizeof(char) * (print_size + 1));
    if (print_buffer == NULL) {
        printf("%d", print_size);
        fprintf(stderr, "Memory allocation failure\n");
        exit(EXIT_FAILURE);
    }

    // Read the initial response line
    if (print_index > 0) {
        char response_buffer[print_index];
        int response_bytes_received = recv(connfd, response_buffer, print_index, 0);
        if (response_bytes_received < 0) {
            fprintf(stderr, "Failed to receive header\n");
            free(print_buffer);
            exit(EXIT_FAILURE);
        }
    }

    // Read the entire parsed content
    while (total_received < print_size) {
        bytes_received = recv(connfd, print_buffer + total_received, print_size - total_received, 0);
        if (bytes_received < 0) {
            fprintf(stderr, "Failed to receive body content\n");
            free(print_buffer);
            exit(EXIT_FAILURE);
        }
        total_received += bytes_received;
    }

    print_buffer[total_received] = '\0'; // Null-terminate the buffer
    remove_cr_newline(print_buffer);    // Unfold the fields

    // Print the parsed content
    printf("%s", print_buffer);
    free(print_buffer); // Free allocated memory
    receive_remaining_response(connfd);
}

void remove_cr_newline(char *input) {
    char *src = input, *dst = input;
    while (*src) {
       
        if (*src == '\r' && *(src + 1) == '\n') {
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void read_mime(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received, body_size;
    char tag[TAG_SIZE];
    char* body_start;

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate parse from command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH %d BODY.PEEK[]\r\n", tag, client->message_num);

    // Send parse from command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send parse subject command");
        exit(EXIT_FAILURE);
    }

    // Receive parse from response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, MSG_PEEK);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive parse subject response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    body_start = strstr(receive_buffer, "\r\n");
    if (sscanf(receive_buffer, "* %*d FETCH (BODY[] {%d}", &body_size) == 1 && body_start != NULL) {
        body_start += 2;                    // Move past the \r\n to the start of the body           
        int body_index = body_start - receive_buffer;
        char* body_buffer = get_full_body(client->connfd, body_size + body_index);
        print_mime(body_buffer);
        free(body_buffer);
    }
}

char* get_full_body(int connfd, int body_size) {
    int bytes_received, total_received = 0;

    char* body_buffer = (char*)malloc(sizeof(char) * (body_size + 1));
    if (body_buffer == NULL) {
        fprintf(stderr, "Memory allocation failure\n");
        exit(EXIT_FAILURE);
    }

    // Read the entire body content
    while (total_received < body_size) {
        bytes_received = recv(connfd, body_buffer + total_received, body_size - total_received, 0);
        if (bytes_received < 0) {
            fprintf(stderr, "Failed to receive body content\n");
            free(body_buffer);
            exit(EXIT_FAILURE);
        }
        total_received += bytes_received;
    }
    body_buffer[total_received] = '\0'; // Null-terminate the buffer 
    return body_buffer;
}

void print_mime(char* body_buffer) {

    char* content = NULL;
    char* boundary = NULL;

    if (!insensitive_strstr(body_buffer, "MIME-Version: 1.0")) {
        fprintf(stderr, "MIME-Version not found\n");
        exit(4);
    }

    content = insensitive_strstr(body_buffer, "Content-Type: multipart/alternative;");
    if (content) {
        boundary = get_boundary(content);
        if (!boundary) {
            fprintf(stderr, "Boundary not found\n");
            exit(4);
        } 
    } else {
        fprintf(stderr, "Content-Type: multipart/alternative\n");
        exit(4);
    }
    
    content = check_starting_boundary(content, boundary);
    
    char* content_type = check_content_type_charset(content);
    char* encoding = check_encoding_parameter(content);
    
    if (content_type < encoding) {
        content = check_content_type_charset(content);
        content = check_encoding_parameter(content);
    } else {
        content = check_encoding_parameter(content); 
        content = check_content_type_charset(content);
    }

    content += 4;                               // Skip the \r\n\r\n
    content = check_end_boundary(content, boundary);
    printf("%s", content);

}

char* insensitive_strstr(char* search, char* target) {
    if (!target[0]) {
        return search;
    }

    size_t target_len = strlen(target);
    
    while (*search) {
        if (strncasecmp(search, target, target_len) == 0) {
            return search;
        }
        search++;
    }

    return NULL;
}

char* get_boundary(char* content) {
    
    char* start = insensitive_strstr(content, "boundary=");
    if (start) {
        start += strlen("boundary=");
        
        // Check if boundary is in quotes
        if (*start == '\"') {
            start++;            // Move past the \"
            char* end = strchr(start, '\"');
            if (end) {
                int boundary_len = end - start;
                char* boundary = (char*)malloc(boundary_len + 1);
                if (boundary == NULL) {
                    fprintf(stderr, "Malloc failure\n");
                    exit(EXIT_FAILURE);
                }
                strncpy(boundary, start, boundary_len);
                boundary[boundary_len] = '\0';
                return boundary;
            }
        } else {
            // Boundary without quotes
            char* end = start;
            while (*end && *end != ' ' && *end != '\r' && *end != '\n') {
                end++;
            }
            int boundary_len = end - start;
            char* boundary = (char*)malloc(boundary_len + 1);
            if (boundary == NULL) {
                fprintf(stderr, "Malloc failure\n");
                exit(EXIT_FAILURE);
            }
            strncpy(boundary, start, boundary_len);
            boundary[boundary_len] = '\0';
            return boundary;
        }
    }    
    return NULL;
}

char* check_starting_boundary(char* content, char* boundary) {
    char boundary_start[BUFFER_SIZE];

    snprintf(boundary_start, sizeof(boundary_start), "\r\n--%s\r\n", boundary);
    if (insensitive_strstr(content, boundary_start)) {
        char* new_content = insensitive_strstr(content, boundary_start);
        new_content += strlen(boundary_start);
        return new_content;
    } else {
        fprintf(stderr, "Starting boundary not found\n");
        exit(4);
    }
}

char* check_content_type_charset(char* content) {
    char text_plain[BUFFER_SIZE];

    snprintf(text_plain, sizeof(text_plain), "Content-Type: text/plain");
    if (insensitive_strstr(content, text_plain)) {
        
        char charset[BUFFER_SIZE];
        snprintf(charset, sizeof(charset), "charset=UTF-8");

        if (insensitive_strstr(content, charset)) {
            char* new_content = insensitive_strstr(content, charset);
            new_content += strlen(charset);
            return new_content;

        } else {
           fprintf(stderr, "charset not found\n");
            exit(4); 
        }
        
    } else {
        fprintf(stderr, "Content-Type text/plain not found\n");
        exit(4);
    } 
}

char* check_encoding_parameter(char* content) {
    
    char encoding_quoted[BUFFER_SIZE];
    snprintf(encoding_quoted, sizeof(encoding_quoted), "Content-Transfer-Encoding: quoted-printable");
    if (insensitive_strstr(content, "Content-Transfer-Encoding: quoted-printable")) {
        char* new_content = insensitive_strstr(content, "Content-Transfer-Encoding: quoted-printable");
        new_content += strlen(encoding_quoted);
        return new_content;
    }

    char encoding_7bit[BUFFER_SIZE];
    snprintf(encoding_7bit, sizeof(encoding_7bit), "Content-Transfer-Encoding: 7bit");
    if (insensitive_strstr(content, "Content-Transfer-Encoding: 7bit")) {
        char* new_content = insensitive_strstr(content, "Content-Transfer-Encoding: 7bit");
        new_content += strlen(encoding_7bit);
        return new_content;
    } 
    
    char encoding_8bit[BUFFER_SIZE];
    snprintf(encoding_8bit, sizeof(encoding_8bit), "Content-Transfer-Encoding: 8bit");
    if (insensitive_strstr(content, "Content-Transfer-Encoding: 8bit")) {
        char* new_content = insensitive_strstr(content, "Content-Transfer-Encoding: 8bit");
        new_content += strlen(encoding_8bit);
        return new_content;
    }
    
    fprintf(stderr, "Content-Transfer-Encoding not found\n");
    exit(4);    
}

char* check_end_boundary(char* content, char* boundary) {
    char boundary_end[BUFFER_SIZE];

    snprintf(boundary_end, sizeof(boundary_end), "\r\n--%s", boundary);
    if (insensitive_strstr(content, boundary_end)) {
        char* end_content = insensitive_strstr(content, boundary_end);
        
        int new_len = end_content - content;
        char* new_content = (char*)malloc(new_len + 1);
        if (new_content == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(1);
        }
        strncpy(new_content, content, new_len);
        new_content[new_len] = '\0';
        return new_content;
    } else {
        fprintf(stderr, "Ending boundary not found\n");
        exit(4);
    }
}

void list_email(client_t* client) {
    char send_buffer[BUFFER_SIZE];
    char receive_buffer[BUFFER_SIZE];
    int bytes_received;
    char tag[TAG_SIZE];

    // Generate tag
    snprintf(tag, sizeof(tag), "A%04d", client->tag_counter++);

    // Generate list command
    snprintf(send_buffer, sizeof(send_buffer), "%s FETCH 1:* (BODY[HEADER.FIELDS (SUBJECT)])\r\n", tag);

    // Send list command
    if (send(client->connfd, send_buffer, strlen(send_buffer), 0) < 0) {
        fprintf(stderr, "Failed to send list command");
        exit(EXIT_FAILURE);
    }

    // Receive list response
    bytes_received = recv(client->connfd, receive_buffer, sizeof(receive_buffer) - 1, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Failed to receive select response\n");
        exit(EXIT_FAILURE);
    }
    receive_buffer[bytes_received] = '\0';

    if (!parse_list_response(client->connfd, receive_buffer)) {
        fprintf(stderr, "Mailbox is empty\n");
        exit(0);
    }
}

int parse_list_response(int connfd, char* response) {
    char* line_start = response;
    char* subject_start;
    char* subject_end;
    int is_not_empty = 0;

    // Loop to get all of the email header lines
    while ((line_start = strstr(line_start, "* ")) != NULL) {
        int email_num, subject_size;
        if (sscanf(line_start, "* %d FETCH (BODY[HEADER.FIELDS (SUBJECT)] {%d}\r\n", &email_num, &subject_size) == 2) {
            subject_start = strstr(line_start, "\r\nSubject:");
            if (subject_start) {
                subject_start += 10; // Move past "\r\nSubject:"
        
                // Find the end of the subject line
                subject_end = strstr(subject_start, "\r\n\r\n)\r\n");
                if (subject_end) {
                    *subject_end = '\0'; // Temporarily null-terminate the subject

                    // Remove starting whitespace
                    while (*subject_start == ' ' || *subject_start == '\t') {
                        subject_start++;
                    }

                    // Unfold and print
                    remove_cr_newline(subject_start);
                    printf("%d: %s\n", email_num, subject_start);
                    is_not_empty = 1;
                    
                    // Move to the next line
                    line_start = subject_end + 7;
                } else {
                    fprintf(stderr, "Subject end not found\n");
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("%d: <No subject>\n", email_num);
                is_not_empty = 1;

                // Move to the next line
                line_start += 5;
            }
        } else {
            fprintf(stderr, "Header not found\n");
            exit(EXIT_FAILURE);
        }
    }
    
    return is_not_empty;
}