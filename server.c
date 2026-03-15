#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/build_info.h>

#define BUFFER_SIZE 8192
#define QUEUE_SIZE 1024
#define MAX_HEADERS 64
#define MAX_CGI_ENV 128

// CONFIG
typedef struct {
    int port;
    char cert[PATH_MAX];
    char key[PATH_MAX];
    int threads;
    size_t max_request_size;
    char log_file[PATH_MAX];
    int enable_chroot;
    char chroot_dir[PATH_MAX];
    char user[64];
    int cors_enabled;
    char cors_allow_origin[256];
    char cors_allow_methods[256];
    char cors_allow_headers[256];
    char cgi_dir[PATH_MAX];
    char cgi_ext[64];
    char cgi_prefix[64];
    char error_404_page[PATH_MAX];
    int logging_ratelimit;
    int logging_time_window;
} config_t;

config_t g_cfg = {
    .cgi_prefix = "/cgi-bin/",
    .cgi_ext = ".cgi",
    .logging_ratelimit = 100,
    .logging_time_window = 60,
};

// DOS PROTECTION
typedef struct {
    uint32_t ip;
    int request_count;
    time_t window_start;
} ip_tracker_t;

#define MAX_TRACKED_IPS 256
static ip_tracker_t ip_trackers[MAX_TRACKED_IPS];
static int tracked_count = 0;
static pthread_mutex_t ip_tracker_mutex = PTHREAD_MUTEX_INITIALIZER;

int should_log_request(uint32_t ip) {
    if (g_cfg.logging_ratelimit == 0) return 1;
    
    time_t now = time(NULL);
    pthread_mutex_lock(&ip_tracker_mutex);
    
    int entry = -1;
    for (int i = 0; i < tracked_count; i++) {
        if (ip_trackers[i].ip == ip) {
            entry = i;
            break;
        }
    }
    
    if (entry == -1) {
        if (tracked_count < MAX_TRACKED_IPS) {
            entry = tracked_count++;
            ip_trackers[entry].ip = ip;
            ip_trackers[entry].request_count = 1;
            ip_trackers[entry].window_start = now;
            pthread_mutex_unlock(&ip_tracker_mutex);
            return 1;
        }
        pthread_mutex_unlock(&ip_tracker_mutex);
        return 0;
    }
    
    if (now - ip_trackers[entry].window_start >= g_cfg.logging_time_window) {
        ip_trackers[entry].request_count = 1;
        ip_trackers[entry].window_start = now;
        pthread_mutex_unlock(&ip_tracker_mutex);
        return 1;
    }
    
    ip_trackers[entry].request_count++;
    int can_log = (ip_trackers[entry].request_count <= g_cfg.logging_ratelimit);
    
    pthread_mutex_unlock(&ip_tracker_mutex);
    return can_log;
}

// LOGGING
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } log_level_t;
FILE *log_fp;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_write(log_level_t lvl, const char *fmt, ...) {
    static const char *lvl_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    struct timeval tv; gettimeofday(&tv, NULL);
    struct tm tm; localtime_r(&tv.tv_sec, &tm);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&log_mutex);
    fprintf(log_fp, "[%s.%03ld] [%s] [tid:%lu] ",
            tbuf, (long)tv.tv_usec/1000, lvl_str[lvl], (unsigned long)pthread_self());
    va_list args; va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    va_end(args);
    fprintf(log_fp, "\n");
    fflush(log_fp);
    pthread_mutex_unlock(&log_mutex);
}

// MIME
typedef struct { const char *ext; const char *type; } mime_t;
static const mime_t mime_table[] = {
    {".html", "text/html"}, {".htm", "text/html"}, {".css", "text/css"},
    {".js", "application/javascript"}, {".json", "application/json"},
    {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
    {".gif", "image/gif"}, {".svg", "image/svg+xml"}, {".txt", "text/plain"},
    {".pdf", "application/pdf"}, {NULL, NULL}
};

const char* detect_mime(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    for (int i = 0; mime_table[i].ext; i++)
        if (!strcasecmp(ext, mime_table[i].ext))
            return mime_table[i].type;
    return "application/octet-stream";
}

// CONFIG LOADER
void trim(char *s) {
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    for (int i = (int)strlen(s) - 1; i >= 0 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); i--)
        s[i] = '\0';
}

void load_config(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { perror("config"); exit(1); }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (strncmp(line, "port=", 5) == 0) g_cfg.port = atoi(line + 5);
        else if (strncmp(line, "cert=", 5) == 0) { strncpy(g_cfg.cert, line + 5, PATH_MAX - 1); g_cfg.cert[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "key=", 4) == 0) { strncpy(g_cfg.key, line + 4, PATH_MAX - 1); g_cfg.key[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "threads=", 8) == 0) g_cfg.threads = atoi(line + 8);
        else if (strncmp(line, "max_request_size=", 17) == 0) g_cfg.max_request_size = (size_t)atoi(line + 17);
        else if (strncmp(line, "log_file=", 9) == 0) { strncpy(g_cfg.log_file, line + 9, PATH_MAX - 1); g_cfg.log_file[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "enable_chroot=", 14) == 0) g_cfg.enable_chroot = atoi(line + 14);
        else if (strncmp(line, "chroot_dir=", 11) == 0) { strncpy(g_cfg.chroot_dir, line + 11, PATH_MAX - 1); g_cfg.chroot_dir[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "user=", 5) == 0) { strncpy(g_cfg.user, line + 5, 63); g_cfg.user[63] = '\0'; }
        else if (strncmp(line, "cors_enabled=", 13) == 0) g_cfg.cors_enabled = atoi(line + 13);
        else if (strncmp(line, "cors_allow_origin=", 18) == 0) { strncpy(g_cfg.cors_allow_origin, line + 18, 255); g_cfg.cors_allow_origin[255] = '\0'; }
        else if (strncmp(line, "cors_allow_methods=", 19) == 0) { strncpy(g_cfg.cors_allow_methods, line + 19, 255); g_cfg.cors_allow_methods[255] = '\0'; }
        else if (strncmp(line, "cors_allow_headers=", 19) == 0) { strncpy(g_cfg.cors_allow_headers, line + 19, 255); g_cfg.cors_allow_headers[255] = '\0'; }
        else if (strncmp(line, "cgi_dir=", 8) == 0) { strncpy(g_cfg.cgi_dir, line + 8, PATH_MAX - 1); g_cfg.cgi_dir[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "cgi_ext=", 8) == 0) { strncpy(g_cfg.cgi_ext, line + 8, 63); g_cfg.cgi_ext[63] = '\0'; }
        else if (strncmp(line, "cgi_prefix=", 11) == 0) { strncpy(g_cfg.cgi_prefix, line + 11, 63); g_cfg.cgi_prefix[63] = '\0'; }
        else if (strncmp(line, "error_404_page=", 15) == 0) { strncpy(g_cfg.error_404_page, line + 15, PATH_MAX - 1); g_cfg.error_404_page[PATH_MAX - 1] = '\0'; }
        else if (strncmp(line, "logging_ratelimit=", 18) == 0) g_cfg.logging_ratelimit = atoi(line + 18);
        else if (strncmp(line, "logging_time_window=", 20) == 0) g_cfg.logging_time_window = atoi(line + 20);
    }
    fclose(f);
    if (g_cfg.max_request_size == 0) g_cfg.max_request_size = 65536;
}

// QUEUE
typedef struct {
    int fd;
    struct sockaddr_in addr;
} client_t;

typedef struct {
    client_t items[QUEUE_SIZE];
    int front, rear, count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} queue_t;

static queue_t queue;

void queue_init(void) {
    pthread_mutex_init(&queue.mutex, NULL);
    pthread_cond_init(&queue.cond, NULL);
    queue.front = queue.rear = queue.count = 0;
}

void queue_push(int fd, struct sockaddr_in addr) {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == QUEUE_SIZE)
        pthread_cond_wait(&queue.cond, &queue.mutex);
    queue.items[queue.rear].fd = fd;
    queue.items[queue.rear].addr = addr;
    queue.rear = (queue.rear + 1) % QUEUE_SIZE;
    queue.count++;
    pthread_cond_signal(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);
}

client_t queue_pop(void) {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == 0)
        pthread_cond_wait(&queue.cond, &queue.mutex);
    client_t c = queue.items[queue.front];
    queue.front = (queue.front + 1) % QUEUE_SIZE;
    queue.count--;
    pthread_cond_signal(&queue.cond);
    pthread_mutex_unlock(&queue.mutex);
    return c;
}

// TLS
static mbedtls_ssl_config ssl_conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

extern int net_send(void *ctx, const unsigned char *buf, size_t len); // in server.asm
extern int net_recv(void *ctx, unsigned char *buf, size_t len); // in server.asm

// HTTP HELPERS
int send_all(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = mbedtls_ssl_write(ssl, buf + sent, len - sent);
        if (r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (r <= 0) {
            char errbuf[256];
            mbedtls_strerror(r, errbuf, sizeof(errbuf));
            log_write(LOG_ERROR, "SSL write error: %s (code: %d)", errbuf, r);
            return -1;
        }
        sent += (size_t)r;
    }
    return 0;
}

void send_cors_headers(char *header, size_t size) {
    if (!g_cfg.cors_enabled) return;
    size_t used = strlen(header);
    if (used < size - 1) {
        strncat(header + used, "Access-Control-Allow-Origin: ", size - used - 1);
        used = strlen(header);
        if (used < size - 1) {
            strncat(header + used, g_cfg.cors_allow_origin, size - used - 1);
            used = strlen(header);
            if (used < size - 2) strncat(header + used, "\r\n", size - used - 1);
        }
        used = strlen(header);
        if (used < size - 1) {
            strncat(header + used, "Access-Control-Allow-Methods: ", size - used - 1);
            used = strlen(header);
            if (used < size - 1) {
                strncat(header + used, g_cfg.cors_allow_methods, size - used - 1);
                used = strlen(header);
                if (used < size - 2) strncat(header + used, "\r\n", size - used - 1);
            }
        }
        used = strlen(header);
        if (used < size - 1) {
            strncat(header + used, "Access-Control-Allow-Headers: ", size - used - 1);
            used = strlen(header);
            if (used < size - 1) {
                strncat(header + used, g_cfg.cors_allow_headers, size - used - 1);
                used = strlen(header);
                if (used < size - 2) strncat(header + used, "\r\n", size - used - 1);
            }
        }
    }
}

extern const char* get_header(const char *request, const char *hdr); // in server.asm

// CGI
int is_cgi_script(const char *url) {
    if (g_cfg.cgi_prefix[0] && strncmp(url, g_cfg.cgi_prefix, strlen(g_cfg.cgi_prefix)) == 0)
        return 1;
    if (g_cfg.cgi_ext[0]) {
        const char *ext = strrchr(url, '.');
        if (ext && strcmp(ext, g_cfg.cgi_ext) == 0)
            return 1;
    }
    return 0;
}

// Safe script path check
int safe_cgi_path(const char *url, char *out_path, size_t out_sz) {
    const char *rel_path = url;
    if (g_cfg.cgi_prefix[0] && strncmp(url, g_cfg.cgi_prefix, strlen(g_cfg.cgi_prefix)) == 0)
        rel_path += strlen(g_cfg.cgi_prefix);
    else
        rel_path = url;

    char canonical[PATH_MAX];
    strncpy(canonical, rel_path, sizeof(canonical)-1);
    canonical[sizeof(canonical)-1] = '\0';
    
    if (strstr(canonical, "..") != NULL) {
        log_write(LOG_WARN, "CGI path traversal attempt: %s", url);
        return 0;
    }

    snprintf(out_path, out_sz, "%s/%s", g_cfg.cgi_dir, canonical);
    
    // Remove duplicate slashes
    char *p = out_path;
    while ((p = strstr(p, "//")) != NULL)
        memmove(p, p+1, strlen(p));
    
    return 1;
}

// Execute CGI script
void serve_cgi(mbedtls_ssl_context *ssl, const char *method, const char *url,
               const char *body, size_t body_len, const char *request,
               struct sockaddr_in *client_addr) {
    // Split URL into path and query
    char url_copy[PATH_MAX];
    strncpy(url_copy, url, sizeof(url_copy) - 1);
    url_copy[sizeof(url_copy) - 1] = '\0';
    
    char *query = strchr(url_copy, '?');
    if (query) {
        *query = '\0';
        query++;
    } else {
        query = "";
    }

    char script_path[PATH_MAX];
    if (!safe_cgi_path(url_copy, script_path, sizeof(script_path))) {
        const char *err = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        return;
    }

    // Check X_OK before execl
    if (access(script_path, X_OK) != 0) {
        log_write(LOG_WARN, "CGI script not executable or not found: %s", script_path);
        const char *err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        return;
    }

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        log_write(LOG_ERROR, "pipe() failed for CGI");
        const char *err = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_write(LOG_ERROR, "fork() failed for CGI");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        const char *err = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        return;
    }

    if (pid == 0) { // Child
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        setenv("REQUEST_METHOD", method, 1);
        setenv("QUERY_STRING", query, 1);
        char clen[32];
        // CONTENT_LENGTH for GET should be "0"
        if (strcmp(method, "GET") == 0)
            snprintf(clen, sizeof(clen), "0");
        else
            snprintf(clen, sizeof(clen), "%zu", body_len);
        setenv("CONTENT_LENGTH", clen, 1);
        const char *content_type = get_header(request, "Content-Type");
        if (content_type)
            setenv("CONTENT_TYPE", content_type, 1);
        else
            setenv("CONTENT_TYPE", "", 1);
        setenv("SCRIPT_NAME", url_copy, 1);
        setenv("PATH_INFO", "", 1);
        setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("REMOTE_ADDR", inet_ntoa(client_addr->sin_addr), 1);
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", ntohs(client_addr->sin_port));
        setenv("REMOTE_PORT", port_str, 1);

        char *req_copy = strdup(request);
        char *line = strtok(req_copy, "\r\n");
        line = strtok(NULL, "\r\n");
        while (line) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *hdr_name = line;
                char *hdr_value = colon + 1;
                while (*hdr_value == ' ') hdr_value++;
                char env_name[256];
                snprintf(env_name, sizeof(env_name), "HTTP_%s", hdr_name);
                for (char *p = env_name; *p; p++) {
                    if (*p == '-') *p = '_';
                    else *p = toupper(*p);
                }
                setenv(env_name, hdr_value, 1);
            }
            line = strtok(NULL, "\r\n");
        }
        free(req_copy);

        execl(script_path, script_path, NULL);
        exit(1);
    }

    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    if (body && body_len > 0) {
        ssize_t written = write(stdin_pipe[1], body, body_len);
        // (void)written;
    }
    close(stdin_pipe[1]);

    char buf[BUFFER_SIZE];
    ssize_t n;
    int header_done = 0;
    char response_header[4096] = {0};
    size_t header_len = 0;
    int content_length = -1;
    char *response_body = NULL;
    size_t body_total = 0;

    while ((n = read(stdout_pipe[0], buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';
        log_write(LOG_DEBUG, "CGI read %zd bytes, header_done=%d", n, header_done);
        if (!header_done) {
            char *end = strstr(buf, "\r\n\r\n");
            if (end) {
                // end points to first '\r' of '\r\n\r\n'
                // Need to include last '\r\n' before '\r\n\r\n' in response_header
                *end = '\0';
                strncat(response_header + header_len, buf, sizeof(response_header)-header_len-1);
                header_len = strlen(response_header);
                // Add final \r\n if missing
                if (header_len > 0 && (header_len < 2 || response_header[header_len-2] != '\r' || response_header[header_len-1] != '\n')) {
                    strncat(response_header, "\r\n", sizeof(response_header) - header_len - 1);
                    header_len = strlen(response_header);
                }

                header_done = 1;

                char *cl = strstr(response_header, "Content-Length:");
                if (cl) {
                    cl += 15;
                    while (*cl == ' ') cl++;
                    content_length = atoi(cl);
                    log_write(LOG_DEBUG, "CGI Content-Length=%d", content_length);
                }

                char *body_start = end + 4;
                size_t body_chunk = n - (body_start - buf);
                if (body_chunk > 0) {
                    response_body = malloc(body_chunk);
                    if (response_body) {
                        memcpy(response_body, body_start, body_chunk);
                        body_total = body_chunk;
                        log_write(LOG_DEBUG, "CGI initial body_chunk=%zu", body_chunk);
                    }
                }
                break;
            } else {
                strncat(response_header + header_len, buf, sizeof(response_header)-header_len-1);
                header_len = strlen(response_header);
            }
        }
    }

    if (header_done) {
        if (content_length > 0 && body_total < (size_t)content_length) {
            size_t remaining = content_length - body_total;
            char *body_ptr = realloc(response_body, content_length);
            if (body_ptr) {
                response_body = body_ptr;
                ssize_t r = read(stdout_pipe[0], response_body + body_total, remaining);
                if (r > 0) body_total += r;
            }
        } else {
            int more;
            while ((more = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
                char *new_body = realloc(response_body, body_total + more);
                if (!new_body) break;
                response_body = new_body;
                memcpy(response_body + body_total, buf, more);
                body_total += more;
            }
        }
    }

    close(stdout_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        log_write(LOG_WARN, "CGI script %s exited with status %d", script_path, WEXITSTATUS(status));
    }

    char final_header[4096] = {0};
    if (header_done) {
        // response_header contains headers WITHOUT final \r\n\r\n
        // Check if CGI emitted Content-Length
        if (strstr(response_header, "Content-Length:")) {
            // CGI already emitted Content-Length, use as is
            snprintf(final_header, sizeof(final_header),
                "HTTP/1.1 200 OK\r\n"
                "%s", response_header);
        } else {
            // No Content-Length from CGI, add ourselves
            snprintf(final_header, sizeof(final_header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: %zu\r\n"
                "%s", body_total, response_header);
        }
        log_write(LOG_DEBUG, "CGI response_header len=%zu, content: %s", header_len, response_header);
        // Log hex representation
        char hex_buf[512] = {0};
        for (size_t i = 0; i < header_len && i < 100; i++) {
            size_t pos = strlen(hex_buf);
            snprintf(hex_buf + pos, sizeof(hex_buf) - pos - 1, "%02x ", (unsigned char)response_header[i]);
        }
        log_write(LOG_DEBUG, "CGI response_header hex: %s", hex_buf);
        log_write(LOG_DEBUG, "CGI body_total: %zu, final_header before CORS: %zu", body_total, strlen(final_header));
    } else {
        log_write(LOG_ERROR, "CGI script %s returned no headers", script_path);
        const char *err = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        free(response_body);
        return;
    }

    send_cors_headers(final_header, sizeof(final_header));

    // Ensure ends with \r\n\r\n before JSON
    size_t hdr_len = strlen(final_header);
    
    // Remove trailing \r\n if present (from response_header)
    if (hdr_len > 1 && final_header[hdr_len-2] == '\r' && final_header[hdr_len-1] == '\n') {
        final_header[hdr_len-2] = '\0';
        hdr_len -= 2;
    }
    
    // Add \r\n\r\n
    snprintf(final_header + hdr_len, sizeof(final_header) - hdr_len - 1, "\r\n\r\n");

    log_write(LOG_DEBUG, "CGI final_header after CORS and padding: %zu", strlen(final_header));
    
    char hex_dump[1024] = {0};
    size_t final_len = strlen(final_header);
    for (size_t i = 0; i < final_len && i < 150; i++) {
        size_t pos = strlen(hex_dump);
        if (final_header[i] == '\r') {
            snprintf(hex_dump + pos, sizeof(hex_dump) - pos - 1, "[CR]");
        } else if (final_header[i] == '\n') {
            snprintf(hex_dump + pos, sizeof(hex_dump) - pos - 1, "[LF]");
        } else if (final_header[i] >= 32 && final_header[i] < 127) {
            snprintf(hex_dump + pos, sizeof(hex_dump) - pos - 1, "%c", final_header[i]);
        } else {
            snprintf(hex_dump + pos, sizeof(hex_dump) - pos - 1, "<%02x>", (unsigned char)final_header[i]);
        }
    }
    log_write(LOG_DEBUG, "CGI final_header content: %s", hex_dump);

    send_all(ssl, (const unsigned char*)final_header, strlen(final_header));
    log_write(LOG_DEBUG, "CGI header sent (%zu bytes)", strlen(final_header));

    // Debug: hexdump final_header
    char hexbuf[1024] = {0};
    size_t fh_len = strlen(final_header);
    for (size_t i = 0; i < fh_len && i < 200; i++) {
        size_t pos = strlen(hexbuf);
        unsigned char c = (unsigned char)final_header[i];
        if (c == '\r') snprintf(hexbuf + pos, sizeof(hexbuf) - pos - 1, "[CR]");
        else if (c == '\n') snprintf(hexbuf + pos, sizeof(hexbuf) - pos - 1, "[LF]");
        else if (c >= 32 && c < 127) snprintf(hexbuf + pos, sizeof(hexbuf) - pos - 1, "%c", c);
        else snprintf(hexbuf + pos, sizeof(hexbuf) - pos - 1, "[%02x]", c);
    }
    log_write(LOG_DEBUG, "Final HTTP header: %s", hexbuf);

    // Extract and log Content-Length from final_header
    char *cl_str = strstr(final_header, "Content-Length:");
    if (cl_str) {
        int cl_val = 0;
        sscanf(cl_str, "Content-Length: %d", &cl_val);
        log_write(LOG_DEBUG, "CGI Content-Length in header: %d", cl_val);
    }
    
    if (response_body && body_total > 0) {
        log_write(LOG_DEBUG, "CGI sending body: %zu bytes", body_total);
        int sent = send_all(ssl, (const unsigned char*)response_body, body_total);
        log_write(LOG_DEBUG, "CGI body send result: %d, body_total=%zu", sent, body_total);
    } else {
        log_write(LOG_DEBUG, "CGI no body to send, body_total=%zu", body_total);
    }

    free(response_body);
}

// STATIC FILE
void serve_file(mbedtls_ssl_context *ssl, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // Serve custom 404 page if configured
        if (g_cfg.error_404_page[0]) {
            int fd404 = open(g_cfg.error_404_page, O_RDONLY);
            if (fd404 >= 0) {
                struct stat st;
                if (fstat(fd404, &st) == 0) {
                    char header[2048] = {0};
                    snprintf(header, sizeof(header) - 1,
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Length: %ld\r\n"
                        "Content-Type: text/html\r\n"
                        "Connection: keep-alive\r\n",
                        (long)st.st_size);
                    send_cors_headers(header, sizeof(header));
                    size_t hlen = strlen(header);
                    if (hlen < sizeof(header) - 2) strcat(header + hlen, "\r\n");
                    send_all(ssl, (const unsigned char*)header, strlen(header));

                    char buf[BUFFER_SIZE];
                    ssize_t n;
                    while ((n = read(fd404, buf, sizeof(buf))) > 0)
                        send_all(ssl, (const unsigned char*)buf, (size_t)n);
                    close(fd404);
                    return;
                }
                close(fd404);
            }
        }
        const char *err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(ssl, (const unsigned char*)err, strlen(err));
        return;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return; }

    char header[2048] = {0};
    snprintf(header, sizeof(header) - 1,
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: %s\r\n"
        "Connection: keep-alive\r\n",
        (long)st.st_size, detect_mime(path));

    send_cors_headers(header, sizeof(header));
    size_t hlen = strlen(header);
    if (hlen < sizeof(header) - 2) strcat(header + hlen, "\r\n");

    send_all(ssl, (const unsigned char*)header, strlen(header));

    char buf[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        send_all(ssl, (const unsigned char*)buf, (size_t)n);
    close(fd);
}

// WORKER
void handle_connection(int client_fd, struct sockaddr_in client_addr) {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);

    int ret = mbedtls_ssl_setup(&ssl, &ssl_conf);
    if (ret != 0) {
        char errbuf[256];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "SSL setup failed: %s", errbuf);
        close(client_fd);
        return;
    }

    mbedtls_ssl_set_bio(&ssl, &client_fd, net_send, net_recv, NULL);

    log_write(LOG_DEBUG, "Starting TLS handshake with fd=%d", client_fd);
    ret = mbedtls_ssl_handshake(&ssl);
    if (ret != 0) {
        char errbuf[256];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_WARN, "TLS handshake failed (fd=%d): %s (code: %d)", client_fd, errbuf, ret);
        goto cleanup;
    }

    log_write(LOG_DEBUG, "TLS handshake completed");

    char *request = malloc(g_cfg.max_request_size + 1);
    if (!request) {
        log_write(LOG_ERROR, "Failed to allocate request buffer");
        goto cleanup;
    }

    while (1) {
        memset(request, 0, g_cfg.max_request_size + 1);
        int len = mbedtls_ssl_read(&ssl, (unsigned char*)request, g_cfg.max_request_size);

        if (len == MBEDTLS_ERR_SSL_WANT_READ || len == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (len == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (len <= 0) {
            if (len < 0) {
                char errbuf[256];
                mbedtls_strerror(len, errbuf, sizeof(errbuf));
                log_write(LOG_DEBUG, "SSL read error: %s (code: %d)", errbuf, len);
            }
            break;
        }

        if ((size_t)len >= g_cfg.max_request_size) {
            log_write(LOG_WARN, "Request too large");
            break;
        }

        char method[16] = {0}, url[PATH_MAX] = {0}, version[16] = {0};
        if (sscanf(request, "%15s %1023s %15s", method, url, version) < 2) {
            log_write(LOG_DEBUG, "Failed to parse request line");
            continue;
        }

        if (should_log_request(client_addr.sin_addr.s_addr)) {
            log_write(LOG_INFO, "Request: %s %s", method, url);
        }

        if (strcmp(method, "OPTIONS") == 0) {
            char resp[512] = "HTTP/1.1 204 No Content\r\nConnection: keep-alive\r\n";
            send_cors_headers(resp, sizeof(resp));
            size_t rlen = strlen(resp);
            if (rlen < sizeof(resp) - 2) strcat(resp + rlen, "\r\n");
            send_all(&ssl, (const unsigned char*)resp, strlen(resp));
            continue;
        }

        if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
            const char *err = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            send_all(&ssl, (const unsigned char*)err, strlen(err));
            break;
        }

        if (strstr(url, "..")) {
            log_write(LOG_WARN, "Path traversal attempt: %s", url);
            const char *err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            send_all(&ssl, (const unsigned char*)err, strlen(err));
            break;
        }

        char *body = NULL;
        size_t body_len = 0;
        int body_allocated = 0;
        const char *content_length_str = get_header(request, "Content-Length");
        if (content_length_str) {
            body_len = atoi(content_length_str);
            char *headers_end = strstr(request, "\r\n\r\n");
            if (headers_end) {
                body = headers_end + 4;
                size_t already_read = len - (body - request);
                if (already_read < body_len) {
                    size_t remaining = body_len - already_read;
                    if (remaining > g_cfg.max_request_size - len) {
                        log_write(LOG_WARN, "Request body too large");
                        break;
                    }
                    char *body_buf = malloc(body_len);
                    if (!body_buf) break;
                    memcpy(body_buf, body, already_read);
                    size_t total_read = already_read;
                    while (total_read < body_len) {
                        int r = mbedtls_ssl_read(&ssl, (unsigned char*)body_buf + total_read, body_len - total_read);
                        if (r <= 0) {
                            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
                            break;
                        }
                        total_read += r;
                    }
                    if (total_read == body_len) {
                        body = body_buf;
                        body_allocated = 1;
                    } else {
                        free(body_buf);
                        body = NULL;
                        body_len = 0;
                    }
                }
            }
        }

        if (is_cgi_script(url)) {
            serve_cgi(&ssl, method, url, body, body_len, request, &client_addr);
            if (body_allocated) free(body);
        } else {
            char path[PATH_MAX] = {0};
            if (strcmp(url, "/") == 0)
                snprintf(path, sizeof(path) - 1, "www/index.html");
            else
                snprintf(path, sizeof(path) - 1, "www/%s", url + 1);

            char *p = path;
            while ((p = strstr(p, "//")) != NULL)
                memmove(p, p + 1, strlen(p));

            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                char tmp[PATH_MAX];
                snprintf(tmp, sizeof(tmp) - 1, "%s/index.html", path);
                strncpy(path, tmp, sizeof(path) - 1);
                path[sizeof(path) - 1] = '\0';
            }

            serve_file(&ssl, path);
            if (body_allocated) free(body);
        }

        break;
    }

    free(request);

cleanup:
    if (mbedtls_ssl_is_handshake_over(&ssl))
        mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    close(client_fd);
    log_write(LOG_DEBUG, "Connection closed fd=%d", client_fd);
}

// WORKER THREAD
extern void* worker(void *arg); // in server.asm

// SANDBOX
void apply_sandbox(void) {
    if (!g_cfg.enable_chroot) return;
    if (strlen(g_cfg.chroot_dir)) {
        if (chroot(g_cfg.chroot_dir) != 0) { perror("chroot"); exit(1); }
        chdir("/");
    }
    if (strlen(g_cfg.user)) {
        struct passwd *pw = getpwnam(g_cfg.user);
        if (!pw) { perror("user"); exit(1); }
        setgid(pw->pw_gid);
        setuid(pw->pw_uid);
    }
    log_write(LOG_INFO, "Sandbox applied");
}

// MAIN
int main(int argc, char *argv[]) {
    const char *config_file = "server.conf";
    if (argc > 1) config_file = argv[1];

    signal(SIGPIPE, SIG_IGN);
    load_config(config_file);

    log_fp = fopen(g_cfg.log_file, "a");
    if (!log_fp) { perror("log"); return 1; }
    setlinebuf(log_fp);

    log_write(LOG_INFO, "=== Server starting ===");
    queue_init();

    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "https_server";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
        (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        char errbuf[256]; mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "DRBG seed failed: %s", errbuf);
        return 1;
    }

    ret = mbedtls_ssl_config_defaults(&ssl_conf,
        MBEDTLS_SSL_IS_SERVER,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char errbuf[256]; mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "SSL config defaults failed: %s", errbuf);
        return 1;
    }

    // Configure TLS
    mbedtls_ssl_conf_min_version(&ssl_conf,
        MBEDTLS_SSL_MAJOR_VERSION_3,
        MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&ssl_conf,
        MBEDTLS_SSL_MAJOR_VERSION_3,
        MBEDTLS_SSL_MINOR_VERSION_3);

    extern const int ciphersuites[]; // in server.asm
    mbedtls_ssl_conf_ciphersuites(&ssl_conf, ciphersuites);

    static const char *alpn_protocols[] = { "http/1.1", NULL };
    mbedtls_ssl_conf_alpn_protocols(&ssl_conf, alpn_protocols);

    mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse_file(&cert, g_cfg.cert);
    if (ret < 0) {
        char errbuf[256]; mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "Failed to parse certificate '%s': %s (code: %d)", g_cfg.cert, errbuf, ret);
        return 1;
    }
    log_write(LOG_INFO, "Certificate loaded: %s", g_cfg.cert);

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    ret = mbedtls_pk_parse_keyfile(&key, g_cfg.key, NULL, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret < 0) {
        char errbuf[256]; mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "Failed to parse private key '%s': %s (code: %d)", g_cfg.key, errbuf, ret);
        return 1;
    }
    log_write(LOG_INFO, "Private key loaded: %s", g_cfg.key);

    ret = mbedtls_ssl_conf_own_cert(&ssl_conf, &cert, &key);
    if (ret != 0) {
        char errbuf[256]; mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_write(LOG_ERROR, "Failed to set own cert: %s", errbuf);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_cfg.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(sock, 128) < 0) { perror("listen"); return 1; }

    apply_sandbox();

    for (int i = 0; i < g_cfg.threads; i++) {
        pthread_t t;
        ret = pthread_create(&t, NULL, worker, NULL);
        if (ret != 0) {
            errno = ret; perror("pthread_create"); return 1;
        }
        pthread_detach(t);
        log_write(LOG_INFO, "Worker thread %d started", i + 1);
    }

    log_write(LOG_INFO, "✓ Server listening on port %d (HTTPS/TLS 1.2)", g_cfg.port);
    log_write(LOG_INFO, "  Certificate: %s", g_cfg.cert);
    log_write(LOG_INFO, "  Private key: %s", g_cfg.key);
    log_write(LOG_INFO, "  Threads: %d", g_cfg.threads);
    log_write(LOG_INFO, "  CGI directory: %s", g_cfg.cgi_dir);
    if (g_cfg.cors_enabled)
        log_write(LOG_INFO, "  CORS enabled: %s", g_cfg.cors_allow_origin);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(sock, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        log_write(LOG_DEBUG, "New connection from %s:%d", ip, ntohs(client_addr.sin_port));

        queue_push(client, client_addr);
    }

    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&key);
    mbedtls_ssl_config_free(&ssl_conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    fclose(log_fp);
    return 0;
}