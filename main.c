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
#include <time.h>

#define PORT 3001
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

/**
 * Trims trailing carriage returns and whitespace from a string
 * @param str The string to trim
 * @return Pointer to the same string for convenience
 */
static char *trim_string(char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    if (len == 0)
        return str;

    // Remove trailing carriage return and whitespace
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t'))
    {
        str[len - 1] = '\0';
        len--;
    }

    return str;
}

/**
 * Processes a single key-value pair and updates the payload accordingly
 * @param payload Pointer to the payload structure to update
 * @param key The key string
 * @param value The value string
 * @return true if the key was recognized and processed, false otherwise
 */
static bool process_key_value_pair(ReceivePayload *payload, const char *key, const char *value)
{
    if (!payload || !key || !value)
        return false;

    bool recognized = true;

    if (strcmp(key, "github_link") == 0)
    {
        free(payload->github_link); // Free any previous value
        payload->github_link = strdup(value);
        printf("GitHub Link: %s\n", payload->github_link ? payload->github_link : "ERROR: Failed to allocate memory");
    }
    else if (strcmp(key, "branch") == 0)
    {
        free(payload->branch); // Free any previous value
        payload->branch = strdup(value);
        printf("Branch: %s\n", payload->branch ? payload->branch : "ERROR: Failed to allocate memory");
    }
    else if (strcmp(key, "personal_access_token") == 0)
    {
        free(payload->personal_access_token); // Free any previous value
        payload->personal_access_token = strdup(value);
        printf("Personal Access Token: [REDACTED]\n");
    }
    else if (strcmp(key, "graph_data") == 0)
    {
        free(payload->graph_data); // Free any previous value
        payload->graph_data = strdup(value);
        printf("Graph Data: [%zu bytes]\n", value ? strlen(value) : 0);
    }
    else
    {
        // Unrecognized key
        recognized = false;
    }

    return recognized;
}

/**
 * Parse a line containing a key-value pair in the format "key=value"
 * @param line String containing the line to parse
 * @param payload Pointer to the payload structure to update
 * @return true if parsing was successful, false otherwise
 */
static bool parse_line(char *line, ReceivePayload *payload)
{
    if (!line || !payload)
        return false;

    char *saveptr = NULL;
    char *key = strtok_r(line, "=", &saveptr);
    if (!key)
        return false;

    // Trim whitespace from key
    key = trim_string(key);
    if (strlen(key) == 0)
        return false;

    // Get value (rest of line after '=')
    char *value = strtok_r(NULL, "", &saveptr);
    if (!value)
        return false;

    // Trim whitespace from value
    value = trim_string(value);

    // Process the key-value pair
    return process_key_value_pair(payload, key, value);
}

/**
 * Parse simple key-value pairs format (key=value, one per line)
 * @param body String containing the payload to parse
 * @return Initialized ReceivePayload structure
 */
ReceivePayload parse_payload(const char *body)
{
    // Initialize payload with NULL values
    ReceivePayload payload = {NULL, NULL, NULL, NULL};

    if (!body)
    {
        fprintf(stderr, "Error: NULL payload body provided\n");
        return payload;
    }

    // Create a copy of the body to work with
    char *body_copy = strdup(body);
    if (!body_copy)
    {
        fprintf(stderr, "Error: Failed to allocate memory for payload parsing\n");
        return payload;
    }

    char *line;
    char *saveptr = NULL;
    int line_count = 0;
    int processed_count = 0;

    // Split into lines and parse each key=value pair
    line = strtok_r(body_copy, "\n", &saveptr);
    while (line != NULL)
    {
        line_count++;
        if (parse_line(line, &payload))
        {
            processed_count++;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    // Free the copy of the body
    free(body_copy);

    printf("Payload parsing complete: %d of %d lines processed\n",
           processed_count, line_count);

    return payload;
}

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
        // Find the Content-Length header
        char *content_length_str = strstr(buffer, "Content-Length:");
        printf("Searching for Content-Length header... %s\n",
               content_length_str ? "found" : "not found");

        int content_length = 0;
        if (content_length_str)
        {
            content_length = atoi(content_length_str + 15);
            printf("Content-Length value: %d bytes\n", content_length);

            if (content_length <= 0)
            {
                printf("Warning: Invalid Content-Length value detected (%d)\n", content_length);
            }
            else if (content_length > BUFFER_SIZE)
            {
                printf("Warning: Content-Length (%d) exceeds buffer size (%d)\n",
                       content_length, BUFFER_SIZE);
            }
        }

        // Find the request body (after the header)
        char *body = strstr(buffer, "\r\n\r\n");
        if (body && content_length > 0)
        {
            body += 4;
            RequestResult result = handle_receive_request(body);
            // printf("Request processing result: success=%s, graph data %s\n",
            //        result.success ? "true" : "false",
            //        result.graph_data ? "available" : "not available");

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
        // Get repository name from GitHub link
        char repo_name[BUFFER_SIZE] = {0};
        char *last_slash = strrchr(payload.github_link, '/');
        if (last_slash && *(last_slash + 1) != '\0')
        {
            snprintf(repo_name, sizeof(repo_name), "%s", last_slash + 1);
            // Remove ".git" suffix if present
            char *dot = strstr(repo_name, ".git");
            if (dot)
                *dot = '\0';
        }
        else
        {
            strcpy(repo_name, "repo");
        }

        // Attempt to clone/update repository (but we don't care about success)
        clone_repository(&payload);

        // Always attempt to load analysis data file for this repository
        char graph_filename[BUFFER_SIZE] = {0};
        snprintf(graph_filename, sizeof(graph_filename), "graphdata_%s.json", repo_name);

        FILE *graph_file = fopen(graph_filename, "r");
        if (graph_file != NULL)
        {
            // Read analysis data file
            fseek(graph_file, 0, SEEK_END);
            long file_size = ftell(graph_file);
            fseek(graph_file, 0, SEEK_SET);

            if (file_size > 0)
            {
                char *file_content = malloc(file_size + 1);
                if (file_content != NULL)
                {
                    size_t read_size = fread(file_content, 1, file_size, graph_file);
                    if (read_size == file_size)
                    {
                        file_content[file_size] = '\0';
                        result.graph_data = strdup(file_content);

                        // Print the first 100 characters (or less) of graph data for debugging
                        int preview_length = file_size < 100 ? file_size : 100;
                        printf("Graph data preview (first %d chars): %.100s%s\n",
                               preview_length, file_content, file_size > 100 ? "..." : "");

                        printf("Returning graphdata_ from file: %s (size: %ld bytes)\n", graph_filename, file_size);
                    }
                    free(file_content);
                }
            }
            fclose(graph_file);
        }
        else
        {
            printf("Graph data file not found: %s\n", graph_filename);
        }

        // Set proper success value based solely on whether we loaded the graphdata file
        result.success = (result.graph_data != NULL);
    }
    else
    {
        printf("Error: No GitHub link provided\n");
    }

    free_payload(payload);
    return result;
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

    bool needs_codeflow = false;

    // Parse GitHub URL to get owner and repo
    char github_owner[BUFFER_SIZE] = {0};
    char github_repo[BUFFER_SIZE] = {0};

    char *github_url_copy = strdup(payload->github_link);
    if (!github_url_copy)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }
    char *dot_git = strstr(github_url_copy, ".git");
    if (dot_git)
    {
        *dot_git = '\0';
    }
    char *github_com = strstr(github_url_copy, "github.com/");
    if (github_com)
    {
        char *owner_start = github_com + 11; // Skip "github.com/"
        char *repo_divider = strchr(owner_start, '/');
        if (repo_divider)
        {
            size_t owner_len = repo_divider - owner_start;
            strncpy(github_owner, owner_start, owner_len);
            github_owner[owner_len] = '\0';
            strncpy(github_repo, repo_divider + 1, BUFFER_SIZE - 1);
        }
    }
    free(github_url_copy);
    if (github_owner[0] == '\0' || github_repo[0] == '\0')
    {
        fprintf(stderr, "Failed to parse GitHub URL: %s\n", payload->github_link);
        return false;
    }

    // Remove existing directory if needed.
    struct stat st = {0};
    if (stat(repo_name, &st) == 0 && S_ISDIR(st.st_mode))
    {
        char rm_command[BUFFER_SIZE] = {0};
        sprintf(rm_command, "rm -rf %s", repo_name);
        system(rm_command);
        printf("Removed existing directory %s for fresh download\n", repo_name);
    }

    // Determine branch to use.
    char branch[BUFFER_SIZE] = {0};
    if (payload->branch && strlen(payload->branch) > 0)
    {
        strncpy(branch, payload->branch, BUFFER_SIZE - 1);
    }
    else
    {
        strcpy(branch, "main"); // Default branch is 'main'
    }

    // Download zip file directly into the current folder as "repo.zip"
    char download_command[BUFFER_SIZE * 2] = {0};
    sprintf(download_command,
            "curl -sL https://github.com/%s/%s/archive/refs/heads/%s.zip -o repo.zip",
            github_owner, github_repo, branch);
    printf("Downloading repository archive...\n");
    int result = system(download_command);
    if (result != 0)
    {
        fprintf(stderr, "Failed to download repository archive. Exit code: %d\n", result);
        if (strcmp(branch, "main") == 0 && (!payload->branch || strlen(payload->branch) == 0))
        {
            strcpy(branch, "master");
            sprintf(download_command,
                    "curl -sL https://github.com/%s/%s/archive/refs/heads/%s.zip -o repo.zip",
                    github_owner, github_repo, branch);
            printf("Trying 'master' branch instead...\n");
            result = system(download_command);
            if (result != 0)
            {
                fprintf(stderr, "Failed to download repository archive (master branch). Exit code: %d\n", result);
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    // Extract the zip file in the current folder.
    char extract_command[BUFFER_SIZE] = {0};
    sprintf(extract_command, "unzip -q repo.zip");
    result = system(extract_command);
    if (result != 0)
    {
        fprintf(stderr, "Failed to extract repository archive. Exit code: %d\n", result);
        return false;
    }

    // Move the extracted directory (typically named {repo}-{branch}) to the final repo_name.
    char move_command[BUFFER_SIZE * 2] = {0};
    sprintf(move_command, "mv %s-%s %s", github_repo, branch, repo_name);
    result = system(move_command);
    if (result != 0)
    {
        fprintf(stderr, "Failed to move extracted directory. Exit code: %d\n", result);
        return false;
    }

    // Remove the downloaded zip file.
    system("rm -f repo.zip");

    printf("Downloaded repository %s (branch: %s) without git history\n", repo_name, branch);
    needs_codeflow = true;

    // Execute codeflow on the downloaded repository.
    if (needs_codeflow)
    {
        char codeflow_command[BUFFER_SIZE] = {0};
        sprintf(codeflow_command, "./goservice/codeflow %s", repo_name);
        printf("=== CODEFLOW EXECUTION ===\n");
        printf("Command: %s\n", codeflow_command);
        printf("Repository: %s\n", repo_name);
        printf("Branch: %s\n", branch);
        printf("Starting codeflow analysis...\n");

        time_t start_time = time(NULL);
        int codeflow_result = system(codeflow_command);
        time_t end_time = time(NULL);
        double elapsed_seconds = difftime(end_time, start_time);
        if (codeflow_result != 0)
        {
            fprintf(stderr, "Codeflow execution failed with exit code: %d\n", codeflow_result);
            fprintf(stderr, "Command was: %s\n", codeflow_command);
            fprintf(stderr, "Execution time: %.1f seconds\n", elapsed_seconds);
        }
        else
        {
            printf("Codeflow execution successful!\n");
            printf("Execution time: %.1f seconds\n", elapsed_seconds);
            printf("Analysis complete for repository: %s\n", repo_name);
            printf("Output files should be generated in the current directory\n");

            char expected_file[BUFFER_SIZE] = {0};
            sprintf(expected_file, "graphdata_%s.json", repo_name);
            struct stat st;
            if (stat(expected_file, &st) == 0)
            {
                printf("Verified: Output file %s exists (%.2f KB)\n",
                       expected_file, (float)st.st_size / 1024);
            }
            else
            {
                printf("Warning: Expected output file %s not found\n", expected_file);
            }
        }
        printf("=== CODEFLOW EXECUTION COMPLETE ===\n");
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