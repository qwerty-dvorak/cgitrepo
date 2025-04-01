#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h> 

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

// New connection handler function
static void *handle_connection(void *arg)
{
    int new_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE] = {0};
    int bytes_read = read(new_socket, buffer, BUFFER_SIZE);
    if (bytes_read < 0)
    {
        // minimal error message
        perror("read failed");
        close(new_socket);
        return NULL;
    }

    // Process POST /receive
    if (strncmp(buffer, "POST /receive HTTP/1.", 20) == 0)
    {
        // minimal logging for production
        // Find the Content-Length header
        char *content_length_str = strstr(buffer, "Content-Length:");
        int content_length = content_length_str ? atoi(content_length_str + 15) : 0;

        // Find the request body (after the header)
        char *body = strstr(buffer, "\r\n\r\n");
        if (body && content_length > 0)
        {
            body += 4;
            RequestResult result = handle_receive_request(body);

            if (result.success)
            {
                if (result.graph_data)
                {
                    char response_header[BUFFER_SIZE];
                    sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: %ld\r\n\r\n", strlen(result.graph_data));
                    send(new_socket, response_header, strlen(response_header), 0);
                    send(new_socket, result.graph_data, strlen(result.graph_data), 0);
                    free(result.graph_data);
                }
                else
                {
                    char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 7\r\n\r\nSuccess";
                    send(new_socket, response, strlen(response), 0);
                }
            }
            else
            {
                char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 21\r\n\r\nFailed to process repo";
                send(new_socket, response, strlen(response), 0);
            }
        }
        else
        {
            char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 14\r\n\r\nMissing payload";
            send(new_socket, response, strlen(response), 0);
        }
    }
    else if (strncmp(buffer, "OPTIONS /receive HTTP/1.", 23) == 0)
    {
        char *response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
        send(new_socket, response, strlen(response), 0);
    }
    else
    {
        char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 9\r\n\r\nNot Found";
        send(new_socket, response, strlen(response), 0);
    }

    close(new_socket);
    return NULL;
}

int main()
{
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Increase backlog to 1000
    if (listen(server_fd, 1000) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // Reduced server startup log message
    printf("Server listening on port %d...\n", PORT);

    while (1)
    {
        int *new_sock = malloc(sizeof(int));
        if (!new_sock)
            continue;
        *new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (*new_sock < 0)
        {
            perror("accept failed");
            free(new_sock);
            continue;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, new_sock) != 0)
        {
            perror("pthread_create failed");
            close(*new_sock);
            free(new_sock);
            continue;
        }
        pthread_detach(tid);
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
    // Reduced logging messages
    // Extract repository name as before...
    char git_command[BUFFER_SIZE] = {0};
    char repo_name[BUFFER_SIZE] = {0};

    const char *slash = strrchr(payload->github_link, '/');
    if (slash)
    {
        const char *dot_git = strstr(slash, ".git");
        if (dot_git)
        {
            strncpy(repo_name, slash + 1, dot_git - (slash + 1));
            repo_name[dot_git - (slash + 1)] = '\0';
        }
        else
        {
            strcpy(repo_name, slash + 1);
        }
    }
    else
    {
        strcpy(repo_name, payload->github_link);
    }

    struct stat st = {0};
    if (stat(repo_name, &st) == 0 && S_ISDIR(st.st_mode))
    {
        // Pulling updates quietly
        char cd_command[BUFFER_SIZE] = {0};
        sprintf(cd_command, "cd %s && git pull -q", repo_name);
        int pull_result = system(cd_command);
        if (pull_result != 0)
        {
            // Only log a warning if pull fails
            fprintf(stderr, "Warning: Failed to pull latest changes. Exit code: %d\n", pull_result);
        }
    }
    else
    {
        // Cloning repository quietly (-q flag added)
        if (payload->personal_access_token && strlen(payload->personal_access_token) > 0)
        {
            char url_with_token[BUFFER_SIZE] = {0};
            const char *https_prefix = "https://";
            char *url_start = payload->github_link;
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
                sprintf(git_command, "git -c core.sharedRepository=group clone -q -b %s %s", payload->branch, url_with_token);
            }
            else
            {
                sprintf(git_command, "git -c core.sharedRepository=group clone -q %s", url_with_token);
            }
        }
        else
        {
            if (payload->branch && strlen(payload->branch) > 0)
            {
                sprintf(git_command, "git -c core.sharedRepository=group clone -q -b %s %s", payload->branch, payload->github_link);
            }
            else
            {
                sprintf(git_command, "git -c core.sharedRepository=group clone -q %s", payload->github_link);
            }
        }

        // Redact token from logs if any (silent log)
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
        // Optionally log only on error:
        // fprintf(stderr, "Executing: %s\n", redacted_command);
        free(redacted_command);

        int result = system(git_command);
        if (result != 0)
        {
            fprintf(stderr, "Failed to clone repository. Exit code: %d\n", result);
            return false;
        }
    }

    // Execute codeflow silently
    char codeflow_command[BUFFER_SIZE] = {0};
    sprintf(codeflow_command, "./goservice/codeflow %s", repo_name);
    int codeflow_result = system(codeflow_command);
    if (codeflow_result == 0)
    {
        FILE *graph_file = fopen("graphdata.json", "r");
        if (graph_file == NULL)
        {
            return true;
        }
        fseek(graph_file, 0, SEEK_END);
        long graph_size = ftell(graph_file);
        fseek(graph_file, 0, SEEK_SET);
        char *graph_data = malloc(graph_size + 1);
        if (graph_data == NULL)
        {
            fclose(graph_file);
            return true;
        }
        size_t read_size = fread(graph_data, 1, graph_size, graph_file);
        fclose(graph_file);
        if (read_size != graph_size)
        {
            free(graph_data);
            return true;
        }
        graph_data[graph_size] = '\0';
        payload->graph_data = strdup(graph_data);
        free(graph_data);
    }
    else
    {
        fprintf(stderr, "Failed to execute codeflow. Exit code: %d\n", codeflow_result);
    }

    return true;
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