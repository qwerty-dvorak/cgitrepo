#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>
#include <ctype.h>

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
    char method[16] = {0};
    char endpoint[BUFFER_SIZE] = {0};
    sscanf(buffer, "%15s %s", method, endpoint);

    if (strcmp(method, "POST") == 0 && strncmp(endpoint, "/receive", 8) == 0)
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
    else if (strcmp(method, "OPTIONS") == 0 && strncmp(endpoint, "/receive", 8) == 0)
    {
        char *response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
        send(new_socket, response, strlen(response), 0);
    }
    // Add OPTIONS handling for /analysis routes
    else if (strcmp(method, "OPTIONS") == 0 && strncmp(endpoint, "/analysis", 9) == 0)
    {
        char *response = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n\r\n";
        send(new_socket, response, strlen(response), 0);
    }
    else if (strcmp(method, "POST") == 0 && strncmp(endpoint, "/analysis", 9) == 0)
    {
        // Find the Content-Length header
        char *content_length_str = strstr(buffer, "Content-Length:");
        int content_length = content_length_str ? atoi(content_length_str + 15) : 0;

        // Find the request body (after the header)
        char *body = strstr(buffer, "\r\n\r\n");
        if (body && content_length > 0)
        {
            body += 4;

            // Parse the repository name from the request body
            // Expecting format: repo_name=<repository_name>
            char decoded_repo[BUFFER_SIZE] = {0};
            char *repo_name_pair = strstr(body, "repo_name=");
            if (repo_name_pair)
            {
                char *repo_value = repo_name_pair + 10; // Skip "repo_name="
                // Copy until end of line or end of body
                size_t i = 0;
                while (repo_value[i] && repo_value[i] != '\r' && repo_value[i] != '\n' && i < BUFFER_SIZE - 1)
                {
                    decoded_repo[i] = repo_value[i];
                    i++;
                }
                decoded_repo[i] = '\0';
            }

            char filename[BUFFER_SIZE] = {0};
            if (strlen(decoded_repo) > 0)
            {
                snprintf(filename, sizeof(filename), "analysis_data_%s.json", decoded_repo);
                printf("Looking for analysis file: %s\n", filename);
            }
            else
            {
                strcpy(filename, "analysis_data.json");
            }

            FILE *analysis_file = fopen(filename, "r");
            if (analysis_file == NULL)
            {
                char response[BUFFER_SIZE];
                snprintf(response, sizeof(response),
                         "HTTP/1.1 404 Not Found\r\n"
                         "Content-Type: text/plain\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                         "Access-Control-Allow-Headers: Content-Type\r\n"
                         "Content-Length: 26\r\n\r\n"
                         "Analysis data not available");
                send(new_socket, response, strlen(response), 0);
            }
            else
            {
                fseek(analysis_file, 0, SEEK_END);
                long file_size = ftell(analysis_file);
                fseek(analysis_file, 0, SEEK_SET);
                char *file_content = malloc(file_size + 1);
                if (file_content == NULL)
                {
                    fclose(analysis_file);
                    char *response = "HTTP/1.1 500 Internal Server Error\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Access-Control-Allow-Origin: *\r\n"
                                     "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                                     "Access-Control-Allow-Headers: Content-Type\r\n"
                                     "Content-Length: 28\r\n\r\n"
                                     "Failed to allocate memory";
                    send(new_socket, response, strlen(response), 0);
                }
                else
                {
                    size_t read_size = fread(file_content, 1, file_size, analysis_file);
                    fclose(analysis_file);
                    if (read_size != file_size)
                    {
                        free(file_content);
                        char *response = "HTTP/1.1 500 Internal Server Error\r\n"
                                         "Content-Type: text/plain\r\n"
                                         "Access-Control-Allow-Origin: *\r\n"
                                         "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                                         "Access-Control-Allow-Headers: Content-Type\r\n"
                                         "Content-Length: 23\r\n\r\n"
                                         "Failed to read file";
                        send(new_socket, response, strlen(response), 0);
                    }
                    else
                    {
                        file_content[file_size] = '\0';
                        char response_header[BUFFER_SIZE];
                        sprintf(response_header,
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: application/json\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                                "Access-Control-Allow-Headers: Content-Type\r\n"
                                "Content-Length: %ld\r\n\r\n",
                                file_size);
                        send(new_socket, response_header, strlen(response_header), 0);
                        send(new_socket, file_content, file_size, 0);
                        free(file_content);
                    }
                }
            }
        }
        else
        {
            char *response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 14\r\n\r\nMissing payload";
            send(new_socket, response, strlen(response), 0);
        }
    }
    else if (strcmp(method, "GET") == 0 && strncmp(endpoint, "/analysis", 9) == 0)
    {
        char filename[BUFFER_SIZE] = {0};
        if (strncmp(endpoint, "/analysis/", 10) == 0)
        {
            // Endpoint includes a repo name: /analysis/{reponame}
            char *repo = endpoint + 10;
            // Remove any query parameters or fragments
            char *query = strchr(repo, '?');
            if (query)
                *query = '\0';

            // Simple URL decoding for the repo name
            char decoded_repo[BUFFER_SIZE] = {0};
            size_t i = 0, j = 0;
            while (repo[i] && j < BUFFER_SIZE - 1)
            {
                if (repo[i] == '%' && isxdigit(repo[i + 1]) && isxdigit(repo[i + 2]))
                {
                    char hex[3] = {repo[i + 1], repo[i + 2], '\0'};
                    decoded_repo[j++] = (char)strtol(hex, NULL, 16);
                    i += 3;
                }
                else
                {
                    decoded_repo[j++] = repo[i++];
                }
            }
            decoded_repo[j] = '\0';

            snprintf(filename, sizeof(filename), "analysis_data_%s.json", decoded_repo);
            printf("Looking for analysis file: %s\n", filename);
        }
        else if (strcmp(endpoint, "/analysis") == 0)
        {
            strcpy(filename, "analysis_data.json");
        }
        else
        {
            // Invalid URL format for /analysis endpoint
            char *response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 9\r\n\r\nNot Found";
            send(new_socket, response, strlen(response), 0);
            close(new_socket);
            return NULL;
        }

        FILE *analysis_file = fopen(filename, "r");
        if (analysis_file == NULL)
        {
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response),
                     "HTTP/1.1 404 Not Found\r\n"
                     "Content-Type: text/plain\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Content-Length: 26\r\n\r\n"
                     "Analysis data not available");
            send(new_socket, response, strlen(response), 0);
        }
        else
        {
            fseek(analysis_file, 0, SEEK_END);
            long file_size = ftell(analysis_file);
            fseek(analysis_file, 0, SEEK_SET);
            char *file_content = malloc(file_size + 1);
            if (file_content == NULL)
            {
                fclose(analysis_file);
                char *response = "HTTP/1.1 500 Internal Server Error\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Access-Control-Allow-Origin: *\r\n"
                                 "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                                 "Access-Control-Allow-Headers: Content-Type\r\n"
                                 "Content-Length: 28\r\n\r\n"
                                 "Failed to allocate memory";
                send(new_socket, response, strlen(response), 0);
            }
            else
            {
                size_t read_size = fread(file_content, 1, file_size, analysis_file);
                fclose(analysis_file);
                if (read_size != file_size)
                {
                    free(file_content);
                    char *response = "HTTP/1.1 500 Internal Server Error\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Access-Control-Allow-Origin: *\r\n"
                                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                                     "Access-Control-Allow-Headers: Content-Type\r\n"
                                     "Content-Length: 23\r\n\r\n"
                                     "Failed to read file";
                    send(new_socket, response, strlen(response), 0);
                }
                else
                {
                    file_content[file_size] = '\0';
                    char response_header[BUFFER_SIZE];
                    sprintf(response_header,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Access-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type\r\n"
                            "Content-Length: %ld\r\n\r\n",
                            file_size);
                    send(new_socket, response_header, strlen(response_header), 0);
                    send(new_socket, file_content, file_size, 0);
                    free(file_content);
                }
            }
        }
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
    // Extract repository name from the full URL using the last slash.
    char repo_name[BUFFER_SIZE] = {0};
    char *last_slash = strrchr(payload->github_link, '/');
    if (last_slash && *(last_slash + 1) != '\0')
    {
        snprintf(repo_name, sizeof(repo_name), "%s", last_slash + 1);
        // Remove ".git" suffix if present.
        char *dot = strstr(repo_name, ".git");
        if (dot)
        {
            *dot = '\0';
        }
    }
    else
    {
        strcpy(repo_name, "repo");
    }

    // Check if repository directory exists.
    struct stat st = {0};
    if (stat(repo_name, &st) == 0 && S_ISDIR(st.st_mode))
    {
        char cd_command[BUFFER_SIZE] = {0};
        sprintf(cd_command, "cd %s && git pull -q", repo_name);
        int pull_result = system(cd_command);
        if (pull_result != 0)
        {
            // Log a warning but continue
            fprintf(stderr, "Warning: Failed to pull latest changes. Exit code: %d\n", pull_result);
        }
    }
    else
    {
        // Use the provided full URL directly.
        char git_command[BUFFER_SIZE] = {0};
        if (payload->branch && strlen(payload->branch) > 0)
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone -q -b %s %s", payload->branch, payload->github_link);
        }
        else
        {
            sprintf(git_command, "git -c core.sharedRepository=group clone -q %s", payload->github_link);
        }

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
        // Open graph file with the repo name
        char graph_filename[BUFFER_SIZE] = {0};
        sprintf(graph_filename, "graphdata_%s.json", repo_name);
        FILE *graph_file = fopen(graph_filename, "r");
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