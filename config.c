#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CONFIG_FILE "server.conf"

void trim(char *s) {
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    for (int i = (int)strlen(s) - 1; i >= 0 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); i--)
        s[i] = '\0';
}

void url_decode(char *src, char *dst) {
    while (*src) {
        if (*src == '%' && isxdigit(src[1]) && isxdigit(src[2])) {
            int val = 0;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void escape_json(const char *src, char *dst, size_t max_len) {
    size_t i = 0;
    while (*src && i < max_len - 1) {
        if (*src == '"') {
            if (i + 1 < max_len - 1) {
                dst[i++] = '\\';
                dst[i++] = '"';
            } else break;
        } else if (*src == '\\') {
            if (i + 1 < max_len - 1) {
                dst[i++] = '\\';
                dst[i++] = '\\';
            } else break;
        } else if (*src == '\n') {
            if (i + 1 < max_len - 1) {
                dst[i++] = '\\';
                dst[i++] = 'n';
            } else break;
        } else if (*src == '\r') {
            if (i + 1 < max_len - 1) {
                dst[i++] = '\\';
                dst[i++] = 'r';
            } else break;
        } else {
            dst[i++] = *src;
        }
        src++;
    }
    dst[i] = '\0';
}

void read_config() {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        printf("Content-Type: application/json\r\n\r\n");
        printf("{\"error\":\"Failed to open config file\"}");
        return;
    }

    // Buffer JSON
    char *json_buffer = malloc(8192);
    if (!json_buffer) {
        printf("Content-Type: application/json\r\n\r\n");
        printf("{\"error\":\"Memory allocation failed\"}");
        fclose(f);
        return;
    }

    strcpy(json_buffer, "{");
    char line[512];
    int first = 1;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        trim(key);
        trim(value);

        char escaped_value[1024];
        escape_json(value, escaped_value, sizeof(escaped_value));

        if (!first) strcat(json_buffer, ",");
        strcat(json_buffer, "\"");
        strcat(json_buffer, key);
        strcat(json_buffer, "\":\"");
        strcat(json_buffer, escaped_value);
        strcat(json_buffer, "\"");
        first = 0;
    }

    strcat(json_buffer, "}");
    fclose(f);

    // Print Content-Length
    size_t json_len = strlen(json_buffer);
    printf("Content-Type: application/json\r\n");
    printf("Content-Length: %lu\r\n\r\n", (unsigned long)json_len);
    printf("%s", json_buffer);
    fflush(stdout);

    free(json_buffer);
}

void write_config(const char *key, const char *value) {
    if (!key || !value) {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: 36\r\n\r\n");
        printf("{\"error\":\"Missing key or value\"}");
        return;
    }

    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: 36\r\n\r\n");
        printf("{\"error\":\"Failed to open config\"}");
        return;
    }

    // Print into buffer
    char **lines = malloc(256 * sizeof(char*));
    if (!lines) {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: 41\r\n\r\n");
        printf("{\"error\":\"Memory allocation failed\"}");
        fclose(f);
        return;
    }

    int count = 0;
    char line_buf[512];
    while (fgets(line_buf, sizeof(line_buf), f) && count < 255) {
        lines[count] = malloc(strlen(line_buf) + 1);
        if (lines[count]) {
            strcpy(lines[count], line_buf);
            count++;
        }
    }
    fclose(f);

    // Update key
    int found = 0;
    for (int i = 0; i < count; i++) {
        char line_copy[512];
        strcpy(line_copy, lines[i]);
        trim(line_copy);

        if (!line_copy[0] || line_copy[0] == '#') continue;

        char *eq = strchr(line_copy, '=');
        if (eq) {
            *eq = '\0';
            char key_only[256];
            strcpy(key_only, line_copy);
            trim(key_only);

            if (strcmp(key_only, key) == 0) {
                free(lines[i]);
                lines[i] = malloc(strlen(key) + strlen(value) + 4);
                if (lines[i]) {
                    snprintf(lines[i], strlen(key) + strlen(value) + 4, "%s=%s\n", key, value);
                    found = 1;
                }
                break;
            }
        }
    }

    // Not found -> new line!
    if (!found && count < 255) {
        lines[count] = malloc(strlen(key) + strlen(value) + 4);
        if (lines[count]) {
            snprintf(lines[count], strlen(key) + strlen(value) + 4, "%s=%s\n", key, value);
            count++;
        }
    }

    // Write again
    f = fopen(CONFIG_FILE, "w");
    if (!f) {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: 33\r\n\r\n");
        printf("{\"error\":\"Failed to write config\"}");
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return;
    }

    for (int i = 0; i < count; i++) {
        fputs(lines[i], f);
        free(lines[i]);
    }
    free(lines);
    fclose(f);

    printf("Content-Type: application/json\r\n");
    printf("Content-Length: 37\r\n\r\n");
    printf("{\"status\":\"ok\",\"key\":\"%s\"}", key);
    fflush(stdout);
}

int main() {
    const char *method = getenv("REQUEST_METHOD");
    const char *query = getenv("QUERY_STRING");
    const char *content_length_str = getenv("CONTENT_LENGTH");

    char action[64] = {0};
    char key[256] = {0};
    char value[512] = {0};

    // Handle GET
    if (method && strcmp(method, "GET") == 0) {
        if (query) {
            // Parse query string
            char query_copy[1024];
            strncpy(query_copy, query, sizeof(query_copy) - 1);
            query_copy[sizeof(query_copy) - 1] = '\0';

            char *p = query_copy;
            while (*p) {
                if (strncmp(p, "action=", 7) == 0) {
                    p += 7;
                    int i = 0;
                    char buf[256] = {0};
                    while (*p && *p != '&' && i < 255) {
                        buf[i++] = *p++;
                    }
                    buf[i] = '\0';
                    url_decode(buf, action);
                } else if (strncmp(p, "key=", 4) == 0) {
                    p += 4;
                    int i = 0;
                    char buf[512] = {0};
                    while (*p && *p != '&' && i < 511) {
                        buf[i++] = *p++;
                    }
                    buf[i] = '\0';
                    url_decode(buf, key);
                } else if (strncmp(p, "value=", 6) == 0) {
                    p += 6;
                    int i = 0;
                    char buf[1024] = {0};
                    while (*p && *p != '&' && i < 1023) {
                        buf[i++] = *p++;
                    }
                    buf[i] = '\0';
                    url_decode(buf, value);
                } else {
                    p++;
                }
            }
        }
    }
    // Handle POST
    else if (method && strcmp(method, "POST") == 0) {
        if (content_length_str) {
            int content_length = atoi(content_length_str);
            if (content_length > 0 && content_length < 4096) {
                char body[4096] = {0};
                fread(body, 1, content_length, stdin);

                // Parse as GET
                char *p = body;
                while (*p) {
                    if (strncmp(p, "action=", 7) == 0) {
                        p += 7;
                        int i = 0;
                        char buf[256] = {0};
                        while (*p && *p != '&' && i < 255) {
                            buf[i++] = *p++;
                        }
                        buf[i] = '\0';
                        url_decode(buf, action);
                    } else if (strncmp(p, "key=", 4) == 0) {
                        p += 4;
                        int i = 0;
                        char buf[512] = {0};
                        while (*p && *p != '&' && i < 511) {
                            buf[i++] = *p++;
                        }
                        buf[i] = '\0';
                        url_decode(buf, key);
                    } else if (strncmp(p, "value=", 6) == 0) {
                        p += 6;
                        int i = 0;
                        char buf[1024] = {0};
                        while (*p && *p != '&' && i < 1023) {
                            buf[i++] = *p++;
                        }
                        buf[i] = '\0';
                        url_decode(buf, value);
                    } else {
                        p++;
                    }
                }
            }
        }
    }

    if (strcmp(action, "read") == 0) {
        read_config();
    } else if (strcmp(action, "write") == 0) {
        write_config(key, value);
    } else {
        printf("Content-Type: application/json\r\n");
        printf("Content-Length: 30\r\n\r\n");
        printf("{\"error\":\"Unknown action\"}");
    }

    return 0;
}