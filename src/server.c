#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "db.h"

#define DEFAULT_PORT 8080
#define BUF_SIZE 65536

#define ADMIN_PASSWORD       "9119"
#define ADMIN_EMAIL          "flower@admin.com"
#define CODE_TTL_SECONDS     300
#define SESSION_TTL_SECONDS  3600
#define MAX_SESSIONS 20
#define MAX_PENDING_REGISTRATIONS 10

typedef enum { ROLE_ADMIN, ROLE_USER } Role;

typedef struct {
    const char *url_path;
    const char *file;
} PageRoute;

typedef struct {
    char token[33];
    Role role;
    int user_id;
    time_t expires_at;
} Session;

typedef struct {
    char name[256];
    char email[256];
    char password[256];
    char code[7];
    time_t expires_at;
    int active;
} PendingRegistration;

static Session sessions[MAX_SESSIONS];
static PendingRegistration pending_regs[MAX_PENDING_REGISTRATIONS];

static sqlite3 *db;

void send_response(int client_fd, const char *status,
                    const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Admin-Token, X-User-Token\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, body_len);

    send(client_fd, header, header_len, 0);
    send(client_fd, body, body_len, 0);
}

void send_json(int client_fd, const char *status, const char *json_body) {
    send_response(client_fd, status, "application/json; charset=utf-8", json_body, strlen(json_body));
}

const char *guess_content_type(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

void send_file(int client_fd, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        send_json(client_fd, "404 Not Found", "{\"error\": \"Файл не найден на сервере\"}");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size);
    fread(content, 1, size, f);
    fclose(f);

    send_response(client_fd, "200 OK", guess_content_type(filepath), content, (size_t)size);
    free(content);
}

void send_static(int client_fd, const char *url_path) {
    if (strstr(url_path, "..")) {
        send_json(client_fd, "400 Bad Request", "{\"error\": \"Некорректный путь\"}");
        return;
    }
    char disk_path[300];
    snprintf(disk_path, sizeof(disk_path), "public%s", url_path);
    send_file(client_fd, disk_path);
}

void generate_code(char *out) {
    unsigned char buf[4];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(buf, 1, 4, f); fclose(f); }
    else { for (int i = 0; i < 4; i++) buf[i] = rand() % 256; }

    unsigned int val = ((unsigned int)buf[0] << 24) | ((unsigned int)buf[1] << 16)
                      | ((unsigned int)buf[2] << 8)  |  (unsigned int)buf[3];
    snprintf(out, 7, "%06u", val % 1000000);
}

void generate_token(char *out) {
    unsigned char buf[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { fread(buf, 1, 16, f); fclose(f); }
    else { for (int i = 0; i < 16; i++) buf[i] = rand() % 256; }

    for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", buf[i]);
    out[32] = '\0';
}

int send_email(const char *to, const char *subject, const char *body) {
    FILE *mail = popen("msmtp -t", "w");
    if (!mail) return 0;
    fprintf(mail, "To: %s\r\nSubject: %s\r\n\r\n%s\r\n", to, subject, body);
    int status = pclose(mail);
    return status == 0;
}

void create_session(char *out_token, Role role, int user_id) {
    generate_token(out_token);
    time_t now = time(NULL);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].expires_at <= now) {
            strcpy(sessions[i].token, out_token);
            sessions[i].role = role;
            sessions[i].user_id = user_id;
            sessions[i].expires_at = now + SESSION_TTL_SECONDS;
            return;
        }
    }
    strcpy(sessions[0].token, out_token);
    sessions[0].role = role;
    sessions[0].user_id = user_id;
    sessions[0].expires_at = now + SESSION_TTL_SECONDS;
}

int is_valid_token(const char *token, int required_role) {
    if (!token || token[0] == '\0') return 0;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].expires_at > now && strcmp(sessions[i].token, token) == 0) {
            if (required_role == -1 || (int)sessions[i].role == required_role) return 1;
            return 0;
        }
    }
    return 0;
}

int get_user_id_from_token(const char *token) {
    if (!token || token[0] == '\0') return -1;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].expires_at > now && sessions[i].role == ROLE_USER
            && strcmp(sessions[i].token, token) == 0) {
            return sessions[i].user_id;
        }
    }
    return -1;
}

PendingRegistration *find_or_create_pending(const char *email) {
    time_t now = time(NULL);
    PendingRegistration *free_slot = NULL;

    for (int i = 0; i < MAX_PENDING_REGISTRATIONS; i++) {
        if (pending_regs[i].active && strcmp(pending_regs[i].email, email) == 0) {
            return &pending_regs[i];
        }
        if (!free_slot && (!pending_regs[i].active || pending_regs[i].expires_at <= now)) {
            free_slot = &pending_regs[i];
        }
    }
    return free_slot;
}

int extract_header(const char *raw, const char *header_name, char *out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\r\n%s:", header_name);

    const char *p = strstr(raw, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;

    const char *end = strstr(p, "\r\n");
    if (!end) return 0;

    size_t len = end - p;
    if (len >= out_size) len = out_size - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int require_user_id(const char *raw_request) {
    char token[64];
    if (!extract_header(raw_request, "X-User-Token", token, sizeof(token))) return -1;
    return get_user_id_from_token(token);
}

int json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = strchr(p, '"');
    if (!p) return 0;
    p++;

    const char *end = strchr(p, '"');
    if (!end) return 0;

    size_t len = end - p;
    if (len >= out_size) len = out_size - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return 1;
}

const char *status_label(const char *status) {
    if (strcmp(status, "new") == 0) return "Новый";
    if (strcmp(status, "processing") == 0) return "В обработке";
    if (strcmp(status, "delivered") == 0) return "Доставлен";
    if (strcmp(status, "cancelled") == 0) return "Отменён";
    return status;
}

void notify_customer(int order_id, const char *new_status, const char *cancel_reason) {
    int owner_id;
    char current_status[32];
    if (!db_get_order_info(db, order_id, &owner_id, current_status, sizeof(current_status))) return;

    char *user_json = db_get_user_by_id(db, owner_id);
    if (!user_json) return;

    char email[256], name[256];
    json_get_string(user_json, "email", email, sizeof(email));
    json_get_string(user_json, "name", name, sizeof(name));
    free(user_json);

    char subject[200];
    char email_body[700];

    if (cancel_reason) {
        snprintf(subject, sizeof(subject), "Заказ №%d отменён", order_id);
        snprintf(email_body, sizeof(email_body),
                 "Здравствуйте, %s!\n\nВаш заказ №%d был отменён.\nПричина: %s",
                 name, order_id, cancel_reason);
    } else {
        snprintf(subject, sizeof(subject), "Заказ №%d — новый статус", order_id);
        snprintf(email_body, sizeof(email_body),
                 "Здравствуйте, %s!\n\nСтатус вашего заказа №%d изменён: %s",
                 name, order_id, status_label(new_status));
    }

    send_email(email, subject, email_body);
}

void notify_admin_new_order(int order_id, int user_id) {
    char *user_json = db_get_user_by_id(db, user_id);
    if (!user_json) return;

    char email[256], name[256];
    json_get_string(user_json, "email", email, sizeof(email));
    json_get_string(user_json, "name", name, sizeof(name));
    free(user_json);

    char subject[128];
    snprintf(subject, sizeof(subject), "Новый заказ №%d", order_id);

    char email_body[400];
    snprintf(email_body, sizeof(email_body),
             "Поступил новый заказ №%d.\nПокупатель: %s (%s)\nПодробности — в админ-панели.",
             order_id, name, email);

    send_email(ADMIN_EMAIL, subject, email_body);
}

int json_get_int(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p == ' ') p++;
    return atoi(p);
}

int json_get_bool(const char *json, const char *key, int default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return default_val;
    p = strchr(p, ':');
    if (!p) return default_val;
    p++;
    while (*p == ' ') p++;
    return strncmp(p, "true", 4) == 0;
}

int parse_bouquet_items(const char *json, BouquetItemInput *out, int max_items) {
    const char *p = strstr(json, "\"items\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int count = 0;
    while (count < max_items) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']' || *p == '\0') break;
        if (*p != '{') break;

        const char *obj_end = strchr(p, '}');
        if (!obj_end) break;

        char obj_buf[128];
        size_t len = (size_t)(obj_end - p) + 1;
        if (len >= sizeof(obj_buf)) len = sizeof(obj_buf) - 1;
        strncpy(obj_buf, p, len);
        obj_buf[len] = '\0';

        int fid = json_get_int(obj_buf, "flower_id");
        int qty = json_get_int(obj_buf, "quantity");
        if (fid > 0 && qty > 0) {
            out[count].flower_id = fid;
            out[count].quantity = qty;
            count++;
        }

        p = obj_end + 1;
    }
    return count;
}

int get_query_param_int(const char *full_path, const char *key) {
    const char *q = strchr(full_path, '?');
    if (!q) return -1;

    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(q, search);
    if (!p) return -1;
    p += strlen(search);
    return atoi(p);
}

int extract_id(const char *path, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0) return -1;
    const char *id_str = path + prefix_len;
    if (*id_str == '\0') return -1;
    for (const char *p = id_str; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
    }
    return atoi(id_str);
}

void parse_flower_json(const char *json, char *name, char *description,
                        double *price, int *stock, char *image_url) {
    name[0] = description[0] = image_url[0] = '\0';
    *price = 0;
    *stock = 0;

    const char *p;

    if ((p = strstr(json, "\"name\""))) {
        p = strchr(p, ':'); p = strchr(p, '"') + 1;
        const char *end = strchr(p, '"');
        size_t len = end - p;
        if (len >= 256) len = 255;
        strncpy(name, p, len);
        name[len] = '\0';
    }
    if ((p = strstr(json, "\"description\""))) {
        p = strchr(p, ':'); p = strchr(p, '"') + 1;
        const char *end = strchr(p, '"');
        size_t len = end - p;
        if (len >= 1024) len = 1023;
        strncpy(description, p, len);
        description[len] = '\0';
    }
    if ((p = strstr(json, "\"price\""))) {
        p = strchr(p, ':') + 1;
        *price = atof(p);
    }
    if ((p = strstr(json, "\"stock\""))) {
        p = strchr(p, ':') + 1;
        *stock = atoi(p);
    }
    if ((p = strstr(json, "\"image_url\""))) {
        p = strchr(p, ':'); p = strchr(p, '"') + 1;
        const char *end = strchr(p, '"');
        size_t len = end - p;
        if (len >= 512) len = 511;
        strncpy(image_url, p, len);
        image_url[len] = '\0';
    }
}

void handle_request(int client_fd, const char *method, const char *path, const char *body, const char *raw_request) {
    printf("-> %s %s\n", method, path);

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client_fd, "204 No Content", "text/plain", "", 0);
        return;
    }

    if (strcmp(method, "GET") == 0) {
        static const PageRoute pages[] = {
            { "/",            "index.html" },
            { "/login",       "login.html" },
            { "/register",    "register.html" },
            { "/admin",       "admin.html" },
            { "/cart",        "cart.html" },
            { "/checkout",    "checkout.html" },
            { "/orders",      "orders.html" },
            { "/build",       "build.html" },
            { "/my-bouquets", "my-bouquets.html" },
            { "/favorites",   "favorites.html" },
        };

        for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
            if (strcmp(path, pages[i].url_path) == 0) {
                char disk_path[128];
                snprintf(disk_path, sizeof(disk_path), "public/pages/%s", pages[i].file);
                send_file(client_fd, disk_path);
                return;
            }
        }

        if (strncmp(path, "/api", 4) != 0) {
            send_static(client_fd, path);
            return;
        }
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/register") == 0) {
        char name[256], email[256], password[256];

        if (!json_get_string(body, "name", name, sizeof(name))
            || !json_get_string(body, "email", email, sizeof(email))
            || !json_get_string(body, "password", password, sizeof(password))
            || name[0] == '\0' || email[0] == '\0' || password[0] == '\0') {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Заполните имя, email и пароль\"}");
            return;
        }

        if (!strchr(email, '@') || !strchr(email, '.')) {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Некорректный email\"}");
            return;
        }

        char *existing = db_get_user_by_email(db, email);
        if (existing) {
            free(existing);
            send_json(client_fd, "409 Conflict", "{\"error\": \"Этот email уже зарегистрирован\"}");
            return;
        }

        PendingRegistration *reg = find_or_create_pending(email);
        if (!reg) {
            send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Сервер занят, попробуйте позже\"}");
            return;
        }

        strncpy(reg->name, name, sizeof(reg->name) - 1);
        strncpy(reg->email, email, sizeof(reg->email) - 1);
        strncpy(reg->password, password, sizeof(reg->password) - 1);
        generate_code(reg->code);
        reg->expires_at = time(NULL) + CODE_TTL_SECONDS;
        reg->active = 1;

        char email_body[256];
        snprintf(email_body, sizeof(email_body),
                 "Здравствуйте, %s!\nВаш код подтверждения регистрации: %s\nДействителен 5 минут.",
                 name, reg->code);

        if (send_email(email, "Подтверждение регистрации — Магазин цветов", email_body)) {
            send_json(client_fd, "200 OK", "{\"status\": \"code_sent\"}");
        } else {
            printf("Не удалось отправить письмо (проверьте настройку msmtp)\n");
            send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось отправить письмо\"}");
        }
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/register/verify") == 0) {
        char email[256], code[16];

        if (!json_get_string(body, "email", email, sizeof(email))
            || !json_get_string(body, "code", code, sizeof(code))) {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Нужны email и код\"}");
            return;
        }

        PendingRegistration *reg = NULL;
        for (int i = 0; i < MAX_PENDING_REGISTRATIONS; i++) {
            if (pending_regs[i].active && strcmp(pending_regs[i].email, email) == 0) {
                reg = &pending_regs[i];
                break;
            }
        }

        time_t now = time(NULL);
        if (!reg || now > reg->expires_at || strcmp(code, reg->code) != 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Неверный или просроченный код\"}");
            return;
        }

        char *user_json = db_create_user(db, reg->name, reg->email, reg->password);
        reg->active = 0;

        if (!user_json) {
            send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось создать аккаунт\"}");
            return;
        }

        char token[33];
        int new_user_id = json_get_int(user_json, "id");
        create_session(token, ROLE_USER, new_user_id);

        char resp[600];
        snprintf(resp, sizeof(resp), "{\"token\": \"%s\", \"user\": %s}", token, user_json);
        free(user_json);

        send_json(client_fd, "201 Created", resp);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/login") == 0) {
        char email[256], password[256];
        if (!json_get_string(body, "email", email, sizeof(email))
            || !json_get_string(body, "password", password, sizeof(password))) {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Нужны email и пароль\"}");
            return;
        }

        if (strcmp(email, ADMIN_EMAIL) == 0) {
            if (strcmp(password, ADMIN_PASSWORD) != 0) {
                send_json(client_fd, "401 Unauthorized", "{\"error\": \"Неверный email или пароль\"}");
                return;
            }

            char token[33];
            create_session(token, ROLE_ADMIN, -1);

            char resp[200];
            snprintf(resp, sizeof(resp),
                     "{\"token\": \"%s\", \"email\": \"%s\", \"role\": \"admin\"}", token, ADMIN_EMAIL);
            send_json(client_fd, "200 OK", resp);
            return;
        }

        char *user_json = db_get_user_by_email(db, email);
        if (!user_json) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Неверный email или пароль\"}");
            return;
        }

        char stored_password[256];
        json_get_string(user_json, "password", stored_password, sizeof(stored_password));

        if (strcmp(password, stored_password) != 0) {
            free(user_json);
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Неверный email или пароль\"}");
            return;
        }

        char name[256];
        json_get_string(user_json, "name", name, sizeof(name));
        int logged_in_user_id = json_get_int(user_json, "id");
        free(user_json);

        char token[33];
        create_session(token, ROLE_USER, logged_in_user_id);

        char resp[400];
        snprintf(resp, sizeof(resp),
                 "{\"token\": \"%s\", \"name\": \"%s\", \"role\": \"user\"}", token, name);
        send_json(client_fd, "200 OK", resp);
        return;
    }

    int needs_admin =
        ((strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "DELETE") == 0)
         && strncmp(path, "/api/flowers", 12) == 0)
        || strncmp(path, "/api/users", 10) == 0;

    if (needs_admin) {
        char token[64];
        if (!extract_header(raw_request, "X-Admin-Token", token, sizeof(token))
            || !is_valid_token(token, ROLE_ADMIN)) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется авторизация администратора\"}");
            return;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/flowers") == 0) {
        char *json = db_get_all_flowers(db);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/flowers") == 0) {
        char name[256], description[1024], image_url[512];
        double price; int stock;
        parse_flower_json(body, name, description, &price, &stock, image_url);

        if (price < 0 || stock < 0) {
            printf("Некорректный ввод: price=%.2f, stock=%d\n", price, stock);
            send_json(client_fd, "400 Bad Request",
                       "{\"error\": \"Цена и количество не могут быть отрицательными\"}");
            return;
        }

        char *json = db_add_flower(db, name, description, price, stock, image_url);
        if (json) {
            send_json(client_fd, "201 Created", json);
            free(json);
        } else {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Не удалось создать запись\"}");
        }
        return;
    }

    int id = extract_id(path, "/api/flowers/");
    if (id > 0) {
        if (strcmp(method, "GET") == 0) {
            char *json = db_get_flower(db, id);
            if (json) {
                send_json(client_fd, "200 OK", json);
                free(json);
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Цветок не найден\"}");
            }
            return;
        }

        if (strcmp(method, "PUT") == 0) {
            char name[256], description[1024], image_url[512];
            double price; int stock;
            parse_flower_json(body, name, description, &price, &stock, image_url);

            if (price < 0 || stock < 0) {
                printf("Некорректный ввод: price=%.2f, stock=%d\n", price, stock);
                send_json(client_fd, "400 Bad Request",
                           "{\"error\": \"Цена и количество не могут быть отрицательными\"}");
                return;
            }

            char *json = db_update_flower(db, id, name, description, price, stock, image_url);
            if (json) {
                send_json(client_fd, "200 OK", json);
                free(json);
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Цветок не найден\"}");
            }
            return;
        }

        if (strcmp(method, "DELETE") == 0) {
            int cancelled_ids[50];
            int cancelled_count = 0;
            if (db_delete_flower_cascade(db, id, cancelled_ids, 50, &cancelled_count)) {
                for (int i = 0; i < cancelled_count && i < 50; i++) {
                    notify_customer(cancelled_ids[i], NULL, "Один из цветов закончился на складе :<");
                }

                char resp[160];
                snprintf(resp, sizeof(resp),
                         "{\"message\": \"Удалено\", \"cancelled_orders\": %d}", cancelled_count);
                send_json(client_fd, "200 OK", resp);
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Цветок не найден\"}");
            }
            return;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/users") == 0) {
        char *json = db_get_all_users(db);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    int user_id = extract_id(path, "/api/users/");
    if (user_id > 0) {
        if (strcmp(method, "PUT") == 0) {
            char name[256], email[256], password[256];
            int has_password = json_get_string(body, "password", password, sizeof(password));

            if (!json_get_string(body, "name", name, sizeof(name))
                || !json_get_string(body, "email", email, sizeof(email))
                || name[0] == '\0' || email[0] == '\0') {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Нужны хотя бы имя и email\"}");
                return;
            }

            char *json = db_update_user(db, user_id, name, email, has_password ? password : NULL);
            if (json) {
                send_json(client_fd, "200 OK", json);
                free(json);
            } else {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Пользователь не найден или email занят\"}");
            }
            return;
        }

        if (strcmp(method, "DELETE") == 0) {
            if (db_delete_user(db, user_id)) {
                send_json(client_fd, "200 OK", "{\"message\": \"Удалено\"}");
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Пользователь не найден\"}");
            }
            return;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/bouquets") == 0) {
        char *json = db_get_all_bouquets(db);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/bouquets/mine") == 0) {
        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }
        char *json = db_get_user_bouquets(db, uid);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/bouquets/all") == 0) {
        char admin_token[64];
        if (!extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
            || !is_valid_token(admin_token, ROLE_ADMIN)) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется авторизация администратора\"}");
            return;
        }
        char *json = db_get_all_bouquets_admin(db);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/bouquets") == 0) {
        char admin_token[64];
        int is_admin = extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
                       && is_valid_token(admin_token, ROLE_ADMIN);

        int owner_user_id = -1;
        if (!is_admin) {
            int uid = require_user_id(raw_request);
            if (uid < 0) {
                send_json(client_fd, "401 Unauthorized",
                           "{\"error\": \"Требуется вход администратора или пользователя\"}");
                return;
            }
            owner_user_id = uid;
        }

        char name[256], description[1024], image_url[512];
        if (!json_get_string(body, "name", name, sizeof(name)) || name[0] == '\0') {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Укажите название букета\"}");
            return;
        }
        if (!json_get_string(body, "description", description, sizeof(description))) description[0] = '\0';
        if (!json_get_string(body, "image_url", image_url, sizeof(image_url))) image_url[0] = '\0';

        BouquetItemInput items[20];
        int item_count = parse_bouquet_items(body, items, 20);
        if (item_count == 0) {
            send_json(client_fd, "400 Bad Request", "{\"error\": \"Добавьте хотя бы один цветок в состав\"}");
            return;
        }

        for (int i = 0; i < item_count; i++) {
            char *f = db_get_flower(db, items[i].flower_id);
            if (!f) {
                char resp[128];
                snprintf(resp, sizeof(resp), "{\"error\": \"Цветок с id %d не найден\"}", items[i].flower_id);
                send_json(client_fd, "400 Bad Request", resp);
                return;
            }
            free(f);
        }

        int bouquet_id = db_create_bouquet(db, name, description, image_url, owner_user_id, items, item_count);
        if (bouquet_id < 0) {
            send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось создать букет\"}");
            return;
        }

        char *json = db_get_bouquet(db, bouquet_id);
        send_json(client_fd, "201 Created", json ? json : "{}");
        free(json);
        return;
    }

    if (strcmp(method, "PUT") == 0) {
        int bouquet_id = extract_id(path, "/api/bouquets/");
        if (bouquet_id > 0) {
            char admin_token[64];
            if (!extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
                || !is_valid_token(admin_token, ROLE_ADMIN)) {
                send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется авторизация администратора\"}");
                return;
            }

            char name[256], description[1024], image_url[512];
            if (!json_get_string(body, "name", name, sizeof(name)) || name[0] == '\0') {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Укажите название букета\"}");
                return;
            }
            if (!json_get_string(body, "description", description, sizeof(description))) description[0] = '\0';
            if (!json_get_string(body, "image_url", image_url, sizeof(image_url))) image_url[0] = '\0';

            BouquetItemInput items[20];
            int item_count = parse_bouquet_items(body, items, 20);
            if (item_count == 0) {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Состав не может быть пустым\"}");
                return;
            }

            if (db_update_bouquet(db, bouquet_id, name, description, image_url, items, item_count)) {
                char *json = db_get_bouquet(db, bouquet_id);
                send_json(client_fd, "200 OK", json ? json : "{}");
                free(json);
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Букет не найден\"}");
            }
            return;
        }
    }

    if (strcmp(method, "DELETE") == 0) {
        int bouquet_id = extract_id(path, "/api/bouquets/");
        if (bouquet_id > 0) {
            int owner = db_get_bouquet_owner(db, bouquet_id);
            if (owner == -2) {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Букет не найден\"}");
                return;
            }

            char admin_token[64];
            int is_admin = extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
                           && is_valid_token(admin_token, ROLE_ADMIN);

            if (owner == -1 && !is_admin) {
                send_json(client_fd, "403 Forbidden", "{\"error\": \"Только администратор может удалить каталожный букет\"}");
                return;
            }
            if (owner >= 0) {
                int uid = require_user_id(raw_request);
                if (uid != owner && !is_admin) {
                    send_json(client_fd, "403 Forbidden", "{\"error\": \"Это не ваш букет\"}");
                    return;
                }
            }

            if (db_delete_bouquet(db, bouquet_id)) {
                send_json(client_fd, "200 OK", "{\"message\": \"Удалено\"}");
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Букет не найден\"}");
            }
            return;
        }
    }

    if (strncmp(path, "/api/cart", 9) == 0) {
        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }

        if (strcmp(method, "GET") == 0 && strcmp(path, "/api/cart") == 0) {
            char *items = db_get_cart_items(db, uid);
            char *postcard = db_get_cart_postcard(db, uid);

            size_t resp_size = strlen(items ? items : "[]") + strlen(postcard ? postcard : "{}") + 64;
            char *resp = malloc(resp_size);
            snprintf(resp, resp_size, "{\"items\": %s, \"postcard\": %s}",
                     items ? items : "[]", postcard ? postcard : "{}");
            send_json(client_fd, "200 OK", resp);

            free(items); free(postcard); free(resp);
            return;
        }

        if (strcmp(method, "POST") == 0 && strcmp(path, "/api/cart") == 0) {
            int bouquet_id = json_get_int(body, "bouquet_id");
            int add_qty = json_get_int(body, "quantity");
            if (add_qty <= 0) add_qty = 1;

            if (bouquet_id <= 0) {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Не указан букет\"}");
                return;
            }

            int owner = db_get_bouquet_owner(db, bouquet_id);
            if (owner == -2) {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Букет не найден\"}");
                return;
            }
            if (owner != -1 && owner != uid) {
                send_json(client_fd, "403 Forbidden", "{\"error\": \"Это чужой личный букет\"}");
                return;
            }

            int availability = db_get_bouquet_availability(db, bouquet_id);
            int current = db_get_cart_item_quantity(db, uid, bouquet_id);
            if (current < 0) current = 0;
            int new_qty = current + add_qty;

            if (new_qty > availability) {
                char resp[200];
                snprintf(resp, sizeof(resp),
                         "{\"error\": \"Недостаточно цветов на складе для такого количества букетов\", "
                         "\"available\": %d}", availability);
                send_json(client_fd, "400 Bad Request", resp);
                return;
            }

            if (!db_upsert_cart_item(db, uid, bouquet_id, new_qty)) {
                send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось сохранить корзину\"}");
                return;
            }
            send_json(client_fd, "200 OK", "{\"status\": \"ok\"}");
            return;
        }

        if (strcmp(method, "PUT") == 0 && strcmp(path, "/api/cart/postcard") == 0) {
            int has_postcard = json_get_bool(body, "has_postcard", 0);
            char postcard_text[512];
            if (!json_get_string(body, "postcard_text", postcard_text, sizeof(postcard_text))) {
                postcard_text[0] = '\0';
            }
            db_set_cart_postcard(db, uid, has_postcard, postcard_text);
            send_json(client_fd, "200 OK", "{\"status\": \"ok\"}");
            return;
        }

        int cart_bouquet_id = extract_id(path, "/api/cart/");
        if (cart_bouquet_id > 0) {
            if (strcmp(method, "PUT") == 0) {
                int quantity = json_get_int(body, "quantity");

                if (quantity <= 0) {
                    db_delete_cart_item(db, uid, cart_bouquet_id);
                    send_json(client_fd, "200 OK", "{\"status\": \"removed\"}");
                    return;
                }

                int availability = db_get_bouquet_availability(db, cart_bouquet_id);

                if (quantity > availability) {
                    char resp[200];
                    snprintf(resp, sizeof(resp),
                             "{\"error\": \"Недостаточно на складе\", \"available\": %d}", availability);
                    send_json(client_fd, "400 Bad Request", resp);
                    return;
                }

                if (!db_upsert_cart_item(db, uid, cart_bouquet_id, quantity)) {
                    send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось сохранить корзину\"}");
                    return;
                }
                send_json(client_fd, "200 OK", "{\"status\": \"ok\"}");
                return;
            }

            if (strcmp(method, "DELETE") == 0) {
                db_delete_cart_item(db, uid, cart_bouquet_id);
                send_json(client_fd, "200 OK", "{\"status\": \"removed\"}");
                return;
            }
        }

        send_json(client_fd, "404 Not Found", "{\"error\": \"Маршрут не найден\"}");
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/orders") == 0) {
        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }

        char delivery_address[512], phone[64], delivery_date[32], delivery_time[32];
        if (!json_get_string(body, "delivery_address", delivery_address, sizeof(delivery_address))
            || !json_get_string(body, "phone", phone, sizeof(phone))
            || !json_get_string(body, "delivery_date", delivery_date, sizeof(delivery_date))
            || !json_get_string(body, "delivery_time", delivery_time, sizeof(delivery_time))
            || delivery_address[0] == '\0' || phone[0] == '\0'
            || delivery_date[0] == '\0' || delivery_time[0] == '\0') {
            send_json(client_fd, "400 Bad Request",
                       "{\"error\": \"Укажите адрес доставки, телефон, дату и время доставки\"}");
            return;
        }

        char *postcard_json = db_get_cart_postcard(db, uid);
        int has_postcard = json_get_bool(postcard_json, "has_postcard", 0);
        char postcard_text[512];
        json_get_string(postcard_json, "postcard_text", postcard_text, sizeof(postcard_text));
        free(postcard_json);

        int order_id;
        char error_msg[300];
        if (db_checkout(db, uid, has_postcard, postcard_text,
                         delivery_address, phone, delivery_date, delivery_time,
                         &order_id, error_msg, sizeof(error_msg))) {
            notify_customer(order_id, "new", NULL);
            notify_admin_new_order(order_id, uid);

            char resp[128];
            snprintf(resp, sizeof(resp), "{\"order_id\": %d}", order_id);
            send_json(client_fd, "201 Created", resp);
        } else {
            char error_esc[350];
            size_t j = 0;
            for (size_t i = 0; error_msg[i] && j < sizeof(error_esc) - 2; i++) {
                if (error_msg[i] == '"') error_esc[j++] = '\\';
                error_esc[j++] = error_msg[i];
            }
            error_esc[j] = '\0';

            char resp[400];
            snprintf(resp, sizeof(resp), "{\"error\": \"%s\"}", error_esc);
            send_json(client_fd, "400 Bad Request", resp);
        }
        return;
    }

    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/orders", 11) == 0
        && (path[11] == '\0' || path[11] == '?')) {
        char admin_token[64];
        if (extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
            && is_valid_token(admin_token, ROLE_ADMIN)) {
            int filter_user_id = get_query_param_int(path, "user_id");
            char *json = db_get_all_orders(db, filter_user_id);
            send_json(client_fd, "200 OK", json ? json : "[]");
            free(json);
            return;
        }

        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }

        char *json = db_get_orders_for_user(db, uid);
        send_json(client_fd, "200 OK", json ? json : "[]");
        free(json);
        return;
    }

    if (strcmp(method, "PUT") == 0) {
        int order_id = extract_id(path, "/api/orders/");
        if (order_id > 0) {
            char status[32];
            if (!json_get_string(body, "status", status, sizeof(status))) {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Не указан статус\"}");
                return;
            }

            if (strcmp(status, "new") != 0 && strcmp(status, "processing") != 0
                && strcmp(status, "delivered") != 0 && strcmp(status, "cancelled") != 0) {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Недопустимый статус\"}");
                return;
            }

            char admin_token[64];
            int is_admin = extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
                           && is_valid_token(admin_token, ROLE_ADMIN);

            if (strcmp(status, "cancelled") == 0) {
                char reason[512];
                if (!json_get_string(body, "reason", reason, sizeof(reason)) || reason[0] == '\0') {
                    send_json(client_fd, "400 Bad Request", "{\"error\": \"Укажите причину отмены\"}");
                    return;
                }

                if (!is_admin) {

                    int order_owner_id;
                    char current_status[32];
                    if (!db_get_order_info(db, order_id, &order_owner_id, current_status, sizeof(current_status))) {
                        send_json(client_fd, "404 Not Found", "{\"error\": \"Заказ не найден\"}");
                        return;
                    }

                    int uid = require_user_id(raw_request);
                    if (uid < 0 || uid != order_owner_id) {
                        send_json(client_fd, "403 Forbidden", "{\"error\": \"Это не ваш заказ\"}");
                        return;
                    }
                    if (strcmp(current_status, "delivered") == 0) {
                        send_json(client_fd, "400 Bad Request",
                                   "{\"error\": \"Заказ уже доставлен, отменить его больше нельзя\"}");
                        return;
                    }
                }

                int result = db_cancel_order(db, order_id, reason);
                if (result == 1) {
                    notify_customer(order_id, NULL, reason);
                    send_json(client_fd, "200 OK", "{\"status\": \"cancelled\"}");
                } else if (result == 0) {
                    send_json(client_fd, "200 OK", "{\"status\": \"already_cancelled\"}");
                } else {
                    send_json(client_fd, "404 Not Found", "{\"error\": \"Заказ не найден\"}");
                }
                return;
            }

            if (!is_admin) {
                send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется авторизация администратора\"}");
                return;
            }

            if (db_update_order_status(db, order_id, status)) {
                notify_customer(order_id, status, NULL);
                send_json(client_fd, "200 OK", "{\"status\": \"ok\"}");
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Заказ не найден\"}");
            }
            return;
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/stats") == 0) {
        char admin_token[64];
        if (!extract_header(raw_request, "X-Admin-Token", admin_token, sizeof(admin_token))
            || !is_valid_token(admin_token, ROLE_ADMIN)) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется авторизация администратора\"}");
            return;
        }
        char *json = db_get_admin_stats(db);
        send_json(client_fd, "200 OK", json ? json : "{}");
        free(json);
        return;
    }

    if (strncmp(path, "/api/addresses", 14) == 0) {
        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }

        if (strcmp(method, "GET") == 0 && strcmp(path, "/api/addresses") == 0) {
            char *json = db_get_addresses(db, uid);
            send_json(client_fd, "200 OK", json ? json : "[]");
            free(json);
            return;
        }

        if (strcmp(method, "POST") == 0 && strcmp(path, "/api/addresses") == 0) {
            char label[128], address[512], phone[64];
            if (!json_get_string(body, "address", address, sizeof(address)) || address[0] == '\0'
                || !json_get_string(body, "phone", phone, sizeof(phone)) || phone[0] == '\0') {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Укажите адрес и телефон\"}");
                return;
            }
            if (!json_get_string(body, "label", label, sizeof(label))) label[0] = '\0';

            char *json = db_add_address(db, uid, label, address, phone);
            if (json) {
                send_json(client_fd, "201 Created", json);
                free(json);
            } else {
                send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось сохранить адрес\"}");
            }
            return;
        }

        int address_id = extract_id(path, "/api/addresses/");
        if (address_id > 0 && strcmp(method, "DELETE") == 0) {
            if (db_delete_address(db, uid, address_id)) {
                send_json(client_fd, "200 OK", "{\"message\": \"Удалено\"}");
            } else {
                send_json(client_fd, "404 Not Found", "{\"error\": \"Адрес не найден\"}");
            }
            return;
        }

        send_json(client_fd, "404 Not Found", "{\"error\": \"Маршрут не найден\"}");
        return;
    }

    if (strncmp(path, "/api/favorites", 14) == 0) {
        int uid = require_user_id(raw_request);
        if (uid < 0) {
            send_json(client_fd, "401 Unauthorized", "{\"error\": \"Требуется вход в аккаунт\"}");
            return;
        }

        if (strcmp(method, "GET") == 0 && strcmp(path, "/api/favorites") == 0) {
            char *json = db_get_favorites(db, uid);
            send_json(client_fd, "200 OK", json ? json : "[]");
            free(json);
            return;
        }

        if (strcmp(method, "POST") == 0 && strcmp(path, "/api/favorites") == 0) {
            int bouquet_id = json_get_int(body, "bouquet_id");
            if (bouquet_id <= 0) {
                send_json(client_fd, "400 Bad Request", "{\"error\": \"Не указан букет\"}");
                return;
            }
            if (db_add_favorite(db, uid, bouquet_id)) {
                send_json(client_fd, "200 OK", "{\"status\": \"ok\"}");
            } else {
                send_json(client_fd, "500 Internal Server Error", "{\"error\": \"Не удалось добавить в избранное\"}");
            }
            return;
        }

        int fav_bouquet_id = extract_id(path, "/api/favorites/");
        if (fav_bouquet_id > 0 && strcmp(method, "DELETE") == 0) {
            db_remove_favorite(db, uid, fav_bouquet_id);
            send_json(client_fd, "200 OK", "{\"status\": \"removed\"}");
            return;
        }

        send_json(client_fd, "404 Not Found", "{\"error\": \"Маршрут не найден\"}");
        return;
    }

    send_json(client_fd, "404 Not Found", "{\"error\": \"Маршрут не найден\"}");
}

ssize_t read_full_request(int client_fd, char *buffer, size_t buffer_size) {
    size_t total = 0;
    char *headers_end = NULL;

    while (total < buffer_size - 1) {
        ssize_t n = read(client_fd, buffer + total, buffer_size - 1 - total);
        if (n <= 0) {
            if (total == 0) return -1;
            break;
        }
        total += (size_t)n;
        buffer[total] = '\0';

        headers_end = strstr(buffer, "\r\n\r\n");
        if (headers_end) break;
    }

    if (!headers_end) return (ssize_t)total;

    size_t already_have_body = total - (size_t)((headers_end + 4) - buffer);

    long content_length = 0;
    const char *cl = strstr(buffer, "Content-Length:");
    if (cl) content_length = atol(cl + strlen("Content-Length:"));

    while ((long)already_have_body < content_length && total < buffer_size - 1) {
        ssize_t n = read(client_fd, buffer + total, buffer_size - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
        already_have_body += (size_t)n;
        buffer[total] = '\0';
    }

    return (ssize_t)total;
}

int main(void) {
    int port = DEFAULT_PORT;
    const char *port_env = getenv("PORT");
    if (port_env && atoi(port_env) > 0) {
        port = atoi(port_env);
    }

    db = db_init("flowershop.db");
    if (!db) {
        fprintf(stderr, "Не удалось инициализировать БД\n");
        exit(EXIT_FAILURE);
    }

    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    static char buffer[BUF_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); close(server_fd); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); close(server_fd); exit(EXIT_FAILURE);
    }

    printf("Магазин цветов: сервер запущен на http://localhost:%d\n", port);
    printf("Главная: http://localhost:%d/\n", port);
    printf("Админка: http://localhost:%d/login\n", port);
    printf("Регистрация: http://localhost:%d/register\n", port);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_fd < 0) { perror("accept"); continue; }

        ssize_t bytes_read = read_full_request(client_fd, buffer, BUF_SIZE);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';

            char method[16] = {0};
            char path[256] = {0};
            sscanf(buffer, "%15s %255s", method, path);

            const char *body = strstr(buffer, "\r\n\r\n");
            body = body ? body + 4 : "";

            handle_request(client_fd, method, path, body, buffer);
        }

        close(client_fd);
    }

    close(server_fd);
    db_close(db);
    return 0;
}
