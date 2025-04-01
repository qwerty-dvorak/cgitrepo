#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>

#define PORT 3000
#define BUFFER_SIZE 8192

typedef struct
{
    char *github_link;
    char *branch;
    char *personal_access_token;
    char *graph_data;
} ReceivePayload;

// Add a new struct to return both success status and graph data
typedef struct
{
    bool success;
    char *graph_data;
} RequestResult;

RequestResult handle_receive_request(char *request_body);
ReceivePayload parse_payload(const char *body);
bool clone_repository(ReceivePayload *payload);
void free_payload(ReceivePayload payload);

int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("HTTP server listening on port %d...\n", PORT);

    while (1)
    {
        // Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("accept failed");
            continue;
        }

        // Read the incoming request
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = read(new_socket, buffer, BUFFER_SIZE);
        if (bytes_read < 0)
        {
            perror("read failed");
            close(new_socket);
            continue;
        }

        // Check if this is a POST request to /receive
        if (strncmp(buffer, "POST /receive HTTP/1.", 20) == 0)
        {
            printf("Received POST request to /receive\n");

            // Find the Content-Length header
            char *content_length_str = strstr(buffer, "Content-Length:");
            int content_length = 0;

            if (content_length_str)
            {
                content_length = atoi(content_length_str + 15); // Skip "Content-Length: "
            }

            // Find the request body (after the double newline)
            char *body = strstr(buffer, "\r\n\r\n");
            if (body && content_length > 0)
            {
                body += 4; // Move past the \r\n\r\n

                RequestResult result = handle_receive_request(body);

                if (result.success)
                {
                    // If we have graph data, send it in the response
                    if (result.graph_data)
                    {
                        char response_header[BUFFER_SIZE];
                        sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n",
                                strlen(result.graph_data));
                        send(new_socket, response_header, strlen(response_header), 0);
                        send(new_socket, result.graph_data, strlen(result.graph_data), 0);
                        free(result.graph_data); // Free the graph data
                    }
                    else
                    {
                        // Send a success response without graph data
                        char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 7\r\n\r\nSuccess";
                        send(new_socket, response, strlen(response), 0);
                    }
                }
                else
                {
                    // Send an error response
                    char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: 21\r\n\r\nFailed to process repo";
                    send(new_socket, response, strlen(response), 0);
                }
            }
            else
            {
                // No body found or no content length
                char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 14\r\n\r\nMissing payload";
                send(new_socket, response, strlen(response), 0);
            }
        }
        else
        {
            // Not a POST request to /receive
            char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
            send(new_socket, response, strlen(response), 0);
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}

RequestResult handle_receive_request(char *request_body)
{
    printf("Processing request...\n");
    RequestResult result = {false, NULL};

    ReceivePayload payload = parse_payload(request_body);

    if (payload.github_link)
    {
        result.success = clone_repository(&payload);
        // Save graph data if available
        if (payload.graph_data)
        {
            result.graph_data = payload.graph_data;
            payload.graph_data = NULL; // Prevent it from being freed
        }
    }
    else
    {
        printf("Error: No GitHub link provided\n");
    }

    free_payload(payload);
    return result;
}

// Parse simple key-value pairs format (key=value, one per line)
ReceivePayload parse_payload(const char *body)
{
    ReceivePayload payload = {NULL, NULL, NULL, NULL};
    char *body_copy = strdup(body);
    char *line, *saveptr1, *saveptr2;

    // Split into lines and parse each key=value pair
    line = strtok_r(body_copy, "\n", &saveptr1);
    while (line != NULL)
    {
        char *key = strtok_r(line, "=", &saveptr2);
        char *value = NULL;

        if (key != NULL)
        {
            value = strtok_r(NULL, "", &saveptr2);

            if (value != NULL)
            {
                // Remove any trailing carriage return
                int len = strlen(value);
                if (len > 0 && value[len - 1] == '\r')
                {
                    value[len - 1] = '\0';
                }

                if (strcmp(key, "github_link") == 0)
                {
                    payload.github_link = strdup(value);
                    printf("GitHub Link: %s\n", payload.github_link);
                }
                else if (strcmp(key, "branch") == 0)
                {
                    payload.branch = strdup(value);
                    printf("Branch: %s\n", payload.branch);
                }
                else if (strcmp(key, "personal_access_token") == 0)
                {
                    payload.personal_access_token = strdup(value);
                    printf("Personal Access Token: [REDACTED]\n");
                }
            }
        }

        line = strtok_r(NULL, "\n", &saveptr1);
    }

    free(body_copy);
    return payload;
}

bool clone_repository(ReceivePayload *payload)
{
    printf("Cloning repository: %s\n", payload->github_link);

    // Construct the git command
    char git_command[BUFFER_SIZE] = {0};
    char repo_name[BUFFER_SIZE] = {0};

    // Extract repository name from URL for possible future use
    const char *slash = strrchr(payload->github_link, '/');
    if (slash)
    {
        const char *dot_git = strstr(slash, ".git");
        if (dot_git)
        {
            // Copy from slash+1 to dot_git
            strncpy(repo_name, slash + 1, dot_git - (slash + 1));
            repo_name[dot_git - (slash + 1)] = '\0'; // Ensure null termination
        }
        else
        {
            // Just use everything after the last slash
            strcpy(repo_name, slash + 1);
        }
    }
    else
    {
        // Fallback - use the full URL as name (not ideal)
        strcpy(repo_name, payload->github_link);
    }

    if (payload->personal_access_token && strlen(payload->personal_access_token) > 0)
    {
        // If we have a token, use HTTPS with token in URL
        char url_with_token[BUFFER_SIZE] = {0};

        const char *https_prefix = "https://";
        char *url_start = payload->github_link;

        // If URL starts with https://, skip that part for insertion
        if (strncmp(payload->github_link, https_prefix, strlen(https_prefix)) == 0)
        {
            url_start = payload->github_link + strlen(https_prefix);
            sprintf(url_with_token, "https://%s@%s", payload->personal_access_token, url_start);
        }
        else
        {
            sprintf(url_with_token, "https://%s@%s", payload->personal_access_token, payload->github_link);
        }

        if (payload->branch && strlen(payload->branch) > 0)
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone -b %s %s",
                    payload->branch, url_with_token);
        }
        else
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone %s", url_with_token);
        }
    }
    else
    {
        // No token, use SSH or plain HTTPS
        if (payload->branch && strlen(payload->branch) > 0)
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone -b %s %s",
                    payload->branch, payload->github_link);
        }
        else
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone %s", payload->github_link);
        }
    }

    // Redact token from logs
    char *redacted_command = strdup(git_command);
    if (payload->personal_access_token)
    {
        char *token_pos = strstr(redacted_command, payload->personal_access_token);
        if (token_pos)
        {
            for (int i = 0; i < strlen(payload->personal_access_token); i++)
            {
                token_pos[i] = '*';
            }
        }
    }

    printf("Executing: %s\n", redacted_command);
    free(redacted_command);

    int result = system(git_command);

    if (result == 0)
    {
        printf("Repository cloned successfully\n");

        // Execute the codeflow executable after successful clone
        char codeflow_command[BUFFER_SIZE] = {0};
        sprintf(codeflow_command, "./goservice/codeflow %s", repo_name);

        printf("Executing codeflow with repo: %s\n", repo_name);
        int codeflow_result = system(codeflow_command);

        if (codeflow_result == 0)
        {
            printf("Codeflow executed successfully\n");

            // Read the graphdata.json file
            FILE *graph_file = fopen("graphdata.json", "r");
            if (graph_file == NULL)
            {
                printf("Failed to open graphdata.json\n");
                // Clean up repository folder even if we can't read the graph data
                char rm_command[BUFFER_SIZE];
                sprintf(rm_command, "rm -rf %s", repo_name);
                system(rm_command);
                return true; // Still return success since clone and codeflow worked
            }

            // Get file size
            fseek(graph_file, 0, SEEK_END);
            long graph_size = ftell(graph_file);
            fseek(graph_file, 0, SEEK_SET);

            // Read file content
            char *graph_data = (char *)malloc(graph_size + 1);
            if (graph_data == NULL)
            {
                printf("Failed to allocate memory for graph data\n");
                fclose(graph_file);
                // Clean up repository folder
                char rm_command[BUFFER_SIZE];
                sprintf(rm_command, "rm -rf %s", repo_name);
                system(rm_command);
                return true;
            }

            size_t read_size = fread(graph_data, 1, graph_size, graph_file);
            fclose(graph_file);

            if (read_size != graph_size)
            {
                printf("Failed to read complete graph data\n");
                free(graph_data);
                // Clean up repository folder
                char rm_command[BUFFER_SIZE];
                sprintf(rm_command, "rm -rf %s", repo_name);
                system(rm_command);
                return true;
            }

            graph_data[graph_size] = '\0'; // Ensure null termination

            // Store graph data to be returned in the HTTP response
            payload->graph_data = strdup(graph_data);
            free(graph_data);

            // Clean up repository folder
            char rm_command[BUFFER_SIZE];
            sprintf(rm_command, "rm -rf %s", repo_name);
            printf("Cleaning up repository: %s\n", repo_name);
            system(rm_command);
        }
        else
        {
            printf("Failed to execute codeflow. Exit code: %d\n", codeflow_result);
            // Note: We still return true since the clone was successful
        }

        return true;
    }
    else
    {
        printf("Failed to clone repository. Exit code: %d\n", result);
        return false;
    }
}

void free_payload(ReceivePayload payload)
{
    if (payload.github_link)
        free(payload.github_link);
    if (payload.branch)
        free(payload.branch);
    if (payload.personal_access_token)
        free(payload.personal_access_token);
    if (payload.graph_data)
        free(payload.graph_data);
}