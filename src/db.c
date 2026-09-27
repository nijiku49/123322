#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "db.h"

static void json_escape(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

sqlite3 *db_init(const char *filename) {
    sqlite3 *db;
    int rc = sqlite3_open(filename, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Не удалось открыть БД: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS flowers ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  price REAL NOT NULL,"
        "  stock INTEGER NOT NULL DEFAULT 0,"
        "  image_url TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  email TEXT NOT NULL UNIQUE,"
        "  password TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS bouquets ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  description TEXT,"
        "  image_url TEXT,"
        "  owner_user_id INTEGER,"
        "  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS bouquet_items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  bouquet_id INTEGER NOT NULL,"
        "  flower_id INTEGER NOT NULL,"
        "  quantity INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS cart_items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  bouquet_id INTEGER,"
        "  quantity INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS cart_postcards ("
        "  user_id INTEGER PRIMARY KEY,"
        "  has_postcard INTEGER NOT NULL DEFAULT 0,"
        "  postcard_text TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS orders ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  status TEXT NOT NULL DEFAULT 'new',"
        "  has_postcard INTEGER NOT NULL DEFAULT 0,"
        "  postcard_text TEXT,"
        "  delivery_address TEXT NOT NULL DEFAULT '',"
        "  phone TEXT NOT NULL DEFAULT '',"
        "  delivery_date TEXT NOT NULL DEFAULT '',"
        "  delivery_time TEXT NOT NULL DEFAULT '',"
        "  cancel_reason TEXT,"
        "  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS order_items ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  order_id INTEGER NOT NULL,"
        "  bouquet_id INTEGER,"
        "  bouquet_name TEXT NOT NULL,"
        "  price REAL NOT NULL,"
        "  quantity INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS addresses ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  label TEXT,"
        "  address TEXT NOT NULL,"
        "  phone TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS favorites ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  bouquet_id INTEGER NOT NULL,"
        "  UNIQUE(user_id, bouquet_id)"
        ");";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Ошибка создания таблицы: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return NULL;
    }

    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN delivery_address TEXT NOT NULL DEFAULT ''", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN phone TEXT NOT NULL DEFAULT ''", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN delivery_date TEXT NOT NULL DEFAULT ''", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN delivery_time TEXT NOT NULL DEFAULT ''", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN cancel_reason TEXT", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE cart_items ADD COLUMN bouquet_id INTEGER", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE order_items ADD COLUMN bouquet_id INTEGER", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE order_items ADD COLUMN bouquet_name TEXT", NULL, NULL, NULL);

    {
        sqlite3_stmt *pragma_stmt;
        int needs_rebuild = 0;
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(cart_items)", -1, &pragma_stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(pragma_stmt) == SQLITE_ROW) {
                const char *col_name = (const char *)sqlite3_column_text(pragma_stmt, 1);
                int notnull = sqlite3_column_int(pragma_stmt, 3);
                if (col_name && strcmp(col_name, "flower_id") == 0 && notnull) {
                    needs_rebuild = 1;
                }
            }
            sqlite3_finalize(pragma_stmt);
        }
        if (needs_rebuild) {
            sqlite3_exec(db,
                "CREATE TABLE cart_items_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  user_id INTEGER NOT NULL,"
                "  bouquet_id INTEGER,"
                "  quantity INTEGER NOT NULL"
                ");"
                "INSERT INTO cart_items_new (id, user_id, bouquet_id, quantity) "
                "  SELECT id, user_id, bouquet_id, quantity FROM cart_items;"
                "DROP TABLE cart_items;"
                "ALTER TABLE cart_items_new RENAME TO cart_items;",
                NULL, NULL, NULL);
        }
    }

    {
        sqlite3_stmt *pragma_stmt;
        int needs_rebuild = 0;
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(order_items)", -1, &pragma_stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(pragma_stmt) == SQLITE_ROW) {
                const char *col_name = (const char *)sqlite3_column_text(pragma_stmt, 1);
                int notnull = sqlite3_column_int(pragma_stmt, 3);
                if (col_name && strcmp(col_name, "flower_name") == 0 && notnull) {
                    needs_rebuild = 1;
                }
            }
            sqlite3_finalize(pragma_stmt);
        }
        if (needs_rebuild) {
            sqlite3_exec(db,
                "CREATE TABLE order_items_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  order_id INTEGER NOT NULL,"
                "  bouquet_id INTEGER,"
                "  bouquet_name TEXT,"
                "  price REAL NOT NULL,"
                "  quantity INTEGER NOT NULL"
                ");"

                "INSERT INTO order_items_new (id, order_id, bouquet_id, bouquet_name, price, quantity) "
                "  SELECT id, order_id, bouquet_id, COALESCE(bouquet_name, flower_name), price, quantity "
                "  FROM order_items;"
                "DROP TABLE order_items;"
                "ALTER TABLE order_items_new RENAME TO order_items;",
                NULL, NULL, NULL);
        }
    }

    sqlite3_stmt *check;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM flowers", -1, &check, NULL);
    sqlite3_step(check);
    int flower_count = sqlite3_column_int(check, 0);
    sqlite3_finalize(check);

    if (flower_count == 0) {
        sqlite3_exec(db,
            "INSERT INTO flowers (name, description, price, stock, image_url) VALUES "
            "('Роза', 'Красная роза на длинном стебле', 150.0, 200, '/img/rose.jpg'),"
            "('Тюльпан', 'Свежий весенний тюльпан', 90.0, 150, '/img/tulip.jpg'),"
            "('Хризантема', 'Пышная осенняя хризантема', 110.0, 120, '/img/chrys.jpg');",
            NULL, NULL, NULL);
    }

    sqlite3_stmt *check_b;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM bouquets", -1, &check_b, NULL);
    sqlite3_step(check_b);
    int bouquet_count = sqlite3_column_int(check_b, 0);
    sqlite3_finalize(check_b);

    if (bouquet_count == 0) {
        sqlite3_stmt *find_flower;
        int rose_id = 0, tulip_id = 0, chrys_id = 0;
        if (sqlite3_prepare_v2(db, "SELECT id FROM flowers WHERE name = ?", -1, &find_flower, NULL) == SQLITE_OK) {
            sqlite3_bind_text(find_flower, 1, "Роза", -1, SQLITE_STATIC);
            if (sqlite3_step(find_flower) == SQLITE_ROW) rose_id = sqlite3_column_int(find_flower, 0);
            sqlite3_finalize(find_flower);
        }
        if (sqlite3_prepare_v2(db, "SELECT id FROM flowers WHERE name = ?", -1, &find_flower, NULL) == SQLITE_OK) {
            sqlite3_bind_text(find_flower, 1, "Тюльпан", -1, SQLITE_STATIC);
            if (sqlite3_step(find_flower) == SQLITE_ROW) tulip_id = sqlite3_column_int(find_flower, 0);
            sqlite3_finalize(find_flower);
        }
        if (sqlite3_prepare_v2(db, "SELECT id FROM flowers WHERE name = ?", -1, &find_flower, NULL) == SQLITE_OK) {
            sqlite3_bind_text(find_flower, 1, "Хризантема", -1, SQLITE_STATIC);
            if (sqlite3_step(find_flower) == SQLITE_ROW) chrys_id = sqlite3_column_int(find_flower, 0);
            sqlite3_finalize(find_flower);
        }

        if (rose_id && tulip_id && chrys_id) {
            BouquetItemInput roses11[1] = { { rose_id, 11 } };
            db_create_bouquet(db, "11 красных роз", "Классика — 11 роз на длинных стеблях",
                               "/img/roses.jpg", -1, roses11, 1);

            BouquetItemInput tulips15[1] = { { tulip_id, 15 } };
            db_create_bouquet(db, "Весенние тюльпаны", "15 свежих тюльпанов",
                               "/img/tulips.jpg", -1, tulips15, 1);

            BouquetItemInput mix[3] = { { rose_id, 5 }, { tulip_id, 5 }, { chrys_id, 3 } };
            db_create_bouquet(db, "Весенний микс", "Ассорти из роз, тюльпанов и хризантем",
                               "/img/mix.jpg", -1, mix, 3);
        }
    }

    return db;
}

void db_close(sqlite3 *db) {
    if (db) sqlite3_close(db);
}

static void flower_row_to_json(sqlite3_stmt *stmt, char *out, size_t out_size) {
    char name_esc[512], desc_esc[1024], img_esc[512];

    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *desc = (const char *)sqlite3_column_text(stmt, 2);
    const char *img  = (const char *)sqlite3_column_text(stmt, 5);

    json_escape(name ? name : "", name_esc, sizeof(name_esc));
    json_escape(desc ? desc : "", desc_esc, sizeof(desc_esc));
    json_escape(img ? img : "", img_esc, sizeof(img_esc));

    snprintf(out, out_size,
        "{\"id\": %d, \"name\": \"%s\", \"description\": \"%s\", "
        "\"price\": %.2f, \"stock\": %d, \"image_url\": \"%s\"}",
        sqlite3_column_int(stmt, 0),
        name_esc, desc_esc,
        sqlite3_column_double(stmt, 3),
        sqlite3_column_int(stmt, 4),
        img_esc);
}

char *db_get_all_flowers(sqlite3 *db) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, description, price, stock, image_url FROM flowers ORDER BY id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    char row_buf[2048];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        flower_row_to_json(stmt, row_buf, sizeof(row_buf));

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_flower(sqlite3 *db, int id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, description, price, stock, image_url FROM flowers WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, id);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = malloc(2048);
        flower_row_to_json(stmt, result, 2048);
    }

    sqlite3_finalize(stmt);
    return result;
}

char *db_add_flower(sqlite3 *db, const char *name, const char *description,
                     double price, int stock, const char *image_url) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO flowers (name, description, price, stock, image_url) VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, price);
    sqlite3_bind_int(stmt, 4, stock);
    sqlite3_bind_text(stmt, 5, image_url, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return NULL;

    int new_id = (int)sqlite3_last_insert_rowid(db);
    return db_get_flower(db, new_id);
}

char *db_update_flower(sqlite3 *db, int id, const char *name, const char *description,
                        double price, int stock, const char *image_url) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE flowers SET name = ?, description = ?, price = ?, stock = ?, image_url = ? "
        "WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, price);
    sqlite3_bind_int(stmt, 4, stock);
    sqlite3_bind_text(stmt, 5, image_url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (changes == 0) return NULL;
    return db_get_flower(db, id);
}

int db_delete_flower(sqlite3 *db, int id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM flowers WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

int db_delete_flower_cascade(sqlite3 *db, int flower_id,
                              int *out_cancelled_order_ids, int max_ids, int *out_cancelled_count) {
    *out_cancelled_count = 0;

    sqlite3_stmt *chk;
    int exists = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM flowers WHERE id = ?", -1, &chk, NULL) == SQLITE_OK) {
        sqlite3_bind_int(chk, 1, flower_id);
        exists = sqlite3_step(chk) == SQLITE_ROW;
        sqlite3_finalize(chk);
    }
    if (!exists) return 0;

    int affected_bouquets[500];
    int affected_count = 0;
    sqlite3_stmt *bq;
    if (sqlite3_prepare_v2(db, "SELECT DISTINCT bouquet_id FROM bouquet_items WHERE flower_id = ?",
                            -1, &bq, NULL) == SQLITE_OK) {
        sqlite3_bind_int(bq, 1, flower_id);
        while (sqlite3_step(bq) == SQLITE_ROW && affected_count < 500) {
            affected_bouquets[affected_count++] = sqlite3_column_int(bq, 0);
        }
        sqlite3_finalize(bq);
    }

    for (int i = 0; i < affected_count; i++) {
        sqlite3_stmt *ord;
        const char *osql =
            "SELECT DISTINCT orders.id FROM orders "
            "JOIN order_items ON order_items.order_id = orders.id "
            "WHERE order_items.bouquet_id = ? AND orders.status IN ('new', 'processing')";
        if (sqlite3_prepare_v2(db, osql, -1, &ord, NULL) == SQLITE_OK) {
            sqlite3_bind_int(ord, 1, affected_bouquets[i]);

            int order_ids[200];
            int order_count = 0;
            while (sqlite3_step(ord) == SQLITE_ROW && order_count < 200) {
                order_ids[order_count++] = sqlite3_column_int(ord, 0);
            }
            sqlite3_finalize(ord);

            for (int j = 0; j < order_count; j++) {
                if (db_cancel_order(db, order_ids[j], "Один из цветов закончился на складе :<") == 1) {
                    if (*out_cancelled_count < max_ids) {
                        out_cancelled_order_ids[*out_cancelled_count] = order_ids[j];
                    }
                    (*out_cancelled_count)++;
                }
            }
        }
    }

    sqlite3_stmt *del_items;
    if (sqlite3_prepare_v2(db, "DELETE FROM bouquet_items WHERE flower_id = ?", -1, &del_items, NULL) == SQLITE_OK) {
        sqlite3_bind_int(del_items, 1, flower_id);
        sqlite3_step(del_items);
        sqlite3_finalize(del_items);
    }

    return db_delete_flower(db, flower_id);
}

static void user_row_to_json(sqlite3_stmt *stmt, char *out, size_t out_size, int include_password) {
    char name_esc[256], email_esc[256], password_esc[256];

    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *email = (const char *)sqlite3_column_text(stmt, 2);
    const char *password = (const char *)sqlite3_column_text(stmt, 3);

    json_escape(name ? name : "", name_esc, sizeof(name_esc));
    json_escape(email ? email : "", email_esc, sizeof(email_esc));

    if (include_password) {
        json_escape(password ? password : "", password_esc, sizeof(password_esc));
        snprintf(out, out_size,
            "{\"id\": %d, \"name\": \"%s\", \"email\": \"%s\", \"password\": \"%s\"}",
            sqlite3_column_int(stmt, 0), name_esc, email_esc, password_esc);
    } else {
        snprintf(out, out_size,
            "{\"id\": %d, \"name\": \"%s\", \"email\": \"%s\"}",
            sqlite3_column_int(stmt, 0), name_esc, email_esc);
    }
}

char *db_create_user(sqlite3 *db, const char *name, const char *email, const char *password) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (name, email, password) VALUES (?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, password, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return NULL;

    int new_id = (int)sqlite3_last_insert_rowid(db);

    sqlite3_stmt *sel;
    if (sqlite3_prepare_v2(db, "SELECT id, name, email, password FROM users WHERE id = ?", -1, &sel, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(sel, 1, new_id);

    char *result = NULL;
    if (sqlite3_step(sel) == SQLITE_ROW) {
        result = malloc(512);
        user_row_to_json(sel, result, 512, 0);
    }
    sqlite3_finalize(sel);
    return result;
}

char *db_get_user_by_email(sqlite3 *db, const char *email) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, email, password FROM users WHERE email = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_STATIC);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = malloc(512);
        user_row_to_json(stmt, result, 512, 1);
    }

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_user_by_id(sqlite3 *db, int id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, email, password FROM users WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, id);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = malloc(512);
        user_row_to_json(stmt, result, 512, 0);
    }

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_all_users(sqlite3 *db) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, email, password FROM users ORDER BY id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    char row_buf[600];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        user_row_to_json(stmt, row_buf, sizeof(row_buf), 0);

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_update_user(sqlite3 *db, int id, const char *name, const char *email, const char *password) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE users SET name = ?, email = ?, password = COALESCE(?, password) WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, email, -1, SQLITE_STATIC);
    if (password && password[0] != '\0') {
        sqlite3_bind_text(stmt, 3, password, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, id);

    int rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE || changes == 0) return NULL;

    sqlite3_stmt *sel;
    if (sqlite3_prepare_v2(db, "SELECT id, name, email, password FROM users WHERE id = ?", -1, &sel, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(sel, 1, id);

    char *result = NULL;
    if (sqlite3_step(sel) == SQLITE_ROW) {
        result = malloc(512);
        user_row_to_json(sel, result, 512, 0);
    }
    sqlite3_finalize(sel);
    return result;
}

int db_delete_user(sqlite3 *db, int id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM users WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

static char *bouquet_components_json(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT flowers.id, flowers.name, flowers.price, flowers.stock, bouquet_items.quantity "
        "FROM bouquet_items JOIN flowers ON bouquet_items.flower_id = flowers.id "
        "WHERE bouquet_items.bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char *empty = malloc(3); strcpy(empty, "[]"); return empty;
    }
    sqlite3_bind_int(stmt, 1, bouquet_id);

    size_t cap = 1024;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;
    char name_esc[256], row_buf[400];

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        json_escape(name ? name : "", name_esc, sizeof(name_esc));

        snprintf(row_buf, sizeof(row_buf),
            "{\"flower_id\": %d, \"name\": \"%s\", \"price\": %.2f, \"stock\": %d, \"quantity\": %d}",
            sqlite3_column_int(stmt, 0), name_esc,
            sqlite3_column_double(stmt, 2), sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4));

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

static double bouquet_price(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT COALESCE(SUM(flowers.price * bouquet_items.quantity), 0) "
        "FROM bouquet_items JOIN flowers ON bouquet_items.flower_id = flowers.id "
        "WHERE bouquet_items.bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, bouquet_id);

    double price = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) price = sqlite3_column_double(stmt, 0);
    sqlite3_finalize(stmt);
    return price;
}

int db_get_bouquet_availability(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT MIN(flowers.stock / bouquet_items.quantity) "
        "FROM bouquet_items JOIN flowers ON bouquet_items.flower_id = flowers.id "
        "WHERE bouquet_items.bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, bouquet_id);

    int avail = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        avail = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return avail;
}

static char *bouquet_to_json(sqlite3 *db, sqlite3_stmt *stmt) {
    int id = sqlite3_column_int(stmt, 0);
    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    const char *desc = (const char *)sqlite3_column_text(stmt, 2);
    const char *img = (const char *)sqlite3_column_text(stmt, 3);
    int has_owner = sqlite3_column_type(stmt, 4) != SQLITE_NULL;
    int owner_id = has_owner ? sqlite3_column_int(stmt, 4) : -1;
    const char *created = (const char *)sqlite3_column_text(stmt, 5);

    char name_esc[256], desc_esc[1024], img_esc[512], created_esc[64];
    json_escape(name ? name : "", name_esc, sizeof(name_esc));
    json_escape(desc ? desc : "", desc_esc, sizeof(desc_esc));
    json_escape(img ? img : "", img_esc, sizeof(img_esc));
    json_escape(created ? created : "", created_esc, sizeof(created_esc));

    double price = bouquet_price(db, id);
    int availability = db_get_bouquet_availability(db, id);
    char *components = bouquet_components_json(db, id);

    size_t out_size = strlen(components) + 1024;
    char *out = malloc(out_size);
    snprintf(out, out_size,
        "{\"id\": %d, \"name\": \"%s\", \"description\": \"%s\", \"image_url\": \"%s\", "
        "\"owner_user_id\": %d, \"created_at\": \"%s\", \"price\": %.2f, \"availability\": %d, "
        "\"components\": %s}",
        id, name_esc, desc_esc, img_esc, owner_id, created_esc, price, availability, components);
    free(components);
    return out;
}

char *db_get_all_bouquets(sqlite3 *db) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, name, description, image_url, owner_user_id, created_at "
        "FROM bouquets WHERE owner_user_id IS NULL ORDER BY id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    size_t cap = 8192;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char *obj = bouquet_to_json(db, stmt);
        size_t needed = strlen(result) + strlen(obj) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, obj);
        free(obj);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_user_bouquets(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, name, description, image_url, owner_user_id, created_at "
        "FROM bouquets WHERE owner_user_id = ? ORDER BY id DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char *obj = bouquet_to_json(db, stmt);
        size_t needed = strlen(result) + strlen(obj) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, obj);
        free(obj);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_bouquet(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, name, description, image_url, owner_user_id, created_at FROM bouquets WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, bouquet_id);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = bouquet_to_json(db, stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

int db_get_bouquet_owner(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT owner_user_id FROM bouquets WHERE id = ?", -1, &stmt, NULL) != SQLITE_OK)
        return -2;
    sqlite3_bind_int(stmt, 1, bouquet_id);

    int result = -2;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

int db_create_bouquet(sqlite3 *db, const char *name, const char *description, const char *image_url,
                       int owner_user_id, const BouquetItemInput *items, int item_count) {
    if (item_count <= 0) return -1;

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO bouquets (name, description, image_url, owner_user_id) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, image_url ? image_url : "", -1, SQLITE_STATIC);
    if (owner_user_id >= 0) {
        sqlite3_bind_int(stmt, 4, owner_user_id);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;

    int bouquet_id = (int)sqlite3_last_insert_rowid(db);

    for (int i = 0; i < item_count; i++) {
        sqlite3_stmt *item_stmt;
        if (sqlite3_prepare_v2(db, "INSERT INTO bouquet_items (bouquet_id, flower_id, quantity) VALUES (?, ?, ?)",
                                -1, &item_stmt, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int(item_stmt, 1, bouquet_id);
        sqlite3_bind_int(item_stmt, 2, items[i].flower_id);
        sqlite3_bind_int(item_stmt, 3, items[i].quantity);
        sqlite3_step(item_stmt);
        sqlite3_finalize(item_stmt);
    }

    return bouquet_id;
}

int db_delete_bouquet(sqlite3 *db, int bouquet_id) {
    sqlite3_stmt *del_items;
    if (sqlite3_prepare_v2(db, "DELETE FROM bouquet_items WHERE bouquet_id = ?", -1, &del_items, NULL) == SQLITE_OK) {
        sqlite3_bind_int(del_items, 1, bouquet_id);
        sqlite3_step(del_items);
        sqlite3_finalize(del_items);
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "DELETE FROM bouquets WHERE id = ?", -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, bouquet_id);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

char *db_get_all_bouquets_admin(sqlite3 *db) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT bouquets.id, bouquets.name, bouquets.description, bouquets.image_url, "
        "       bouquets.owner_user_id, bouquets.created_at, users.name, users.email "
        "FROM bouquets LEFT JOIN users ON bouquets.owner_user_id = users.id "
        "ORDER BY bouquets.id DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;

    size_t cap = 8192;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *desc = (const char *)sqlite3_column_text(stmt, 2);
        const char *img = (const char *)sqlite3_column_text(stmt, 3);
        int has_owner = sqlite3_column_type(stmt, 4) != SQLITE_NULL;
        int owner_id = has_owner ? sqlite3_column_int(stmt, 4) : -1;
        const char *created = (const char *)sqlite3_column_text(stmt, 5);
        const char *owner_name = (const char *)sqlite3_column_text(stmt, 6);
        const char *owner_email = (const char *)sqlite3_column_text(stmt, 7);

        char name_esc[256], desc_esc[1024], img_esc[512], created_esc[64], oname_esc[256], oemail_esc[256];
        json_escape(name ? name : "", name_esc, sizeof(name_esc));
        json_escape(desc ? desc : "", desc_esc, sizeof(desc_esc));
        json_escape(img ? img : "", img_esc, sizeof(img_esc));
        json_escape(created ? created : "", created_esc, sizeof(created_esc));
        json_escape(owner_name ? owner_name : "", oname_esc, sizeof(oname_esc));
        json_escape(owner_email ? owner_email : "", oemail_esc, sizeof(oemail_esc));

        double price = bouquet_price(db, id);
        int availability = db_get_bouquet_availability(db, id);
        char *components = bouquet_components_json(db, id);

        size_t row_cap = strlen(components) + 1200;
        char *row_buf = malloc(row_cap);
        snprintf(row_buf, row_cap,
            "{\"id\": %d, \"name\": \"%s\", \"description\": \"%s\", \"image_url\": \"%s\", "
            "\"owner_user_id\": %d, \"owner_name\": \"%s\", \"owner_email\": \"%s\", "
            "\"created_at\": \"%s\", \"price\": %.2f, \"availability\": %d, \"components\": %s}",
            id, name_esc, desc_esc, img_esc, owner_id, oname_esc, oemail_esc,
            created_esc, price, availability, components);
        free(components);

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        free(row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

int db_update_bouquet(sqlite3 *db, int bouquet_id, const char *name, const char *description,
                       const char *image_url, const BouquetItemInput *items, int item_count) {
    if (item_count <= 0) return 0;

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE bouquets SET name = ?, description = ?, image_url = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, description ? description : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, image_url ? image_url : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, bouquet_id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    if (changes == 0) return 0;

    sqlite3_stmt *del_items;
    if (sqlite3_prepare_v2(db, "DELETE FROM bouquet_items WHERE bouquet_id = ?", -1, &del_items, NULL) == SQLITE_OK) {
        sqlite3_bind_int(del_items, 1, bouquet_id);
        sqlite3_step(del_items);
        sqlite3_finalize(del_items);
    }

    for (int i = 0; i < item_count; i++) {
        sqlite3_stmt *item_stmt;
        if (sqlite3_prepare_v2(db, "INSERT INTO bouquet_items (bouquet_id, flower_id, quantity) VALUES (?, ?, ?)",
                                -1, &item_stmt, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int(item_stmt, 1, bouquet_id);
        sqlite3_bind_int(item_stmt, 2, items[i].flower_id);
        sqlite3_bind_int(item_stmt, 3, items[i].quantity);
        sqlite3_step(item_stmt);
        sqlite3_finalize(item_stmt);
    }

    return 1;
}

char *db_get_cart_items(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT bouquets.id, bouquets.name, bouquets.image_url, cart_items.quantity "
        "FROM cart_items JOIN bouquets ON cart_items.bouquet_id = bouquets.id "
        "WHERE cart_items.user_id = ? ORDER BY cart_items.id";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;
    char name_esc[256], img_esc[512], row_buf[900];

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int bouquet_id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *img = (const char *)sqlite3_column_text(stmt, 2);
        json_escape(name ? name : "", name_esc, sizeof(name_esc));
        json_escape(img ? img : "", img_esc, sizeof(img_esc));

        double price = bouquet_price(db, bouquet_id);
        int availability = db_get_bouquet_availability(db, bouquet_id);

        snprintf(row_buf, sizeof(row_buf),
            "{\"bouquet_id\": %d, \"name\": \"%s\", \"price\": %.2f, \"availability\": %d, "
            "\"image_url\": \"%s\", \"quantity\": %d}",
            bouquet_id, name_esc, price, availability, img_esc, sqlite3_column_int(stmt, 3));

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

int db_get_cart_item_quantity(sqlite3 *db, int user_id, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT quantity FROM cart_items WHERE user_id = ? AND bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

int db_upsert_cart_item(sqlite3 *db, int user_id, int bouquet_id, int quantity) {
    int existing = db_get_cart_item_quantity(db, user_id, bouquet_id);

    sqlite3_stmt *stmt;
    if (existing >= 0) {
        if (sqlite3_prepare_v2(db, "UPDATE cart_items SET quantity = ? WHERE user_id = ? AND bouquet_id = ?",
                                -1, &stmt, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int(stmt, 1, quantity);
        sqlite3_bind_int(stmt, 2, user_id);
        sqlite3_bind_int(stmt, 3, bouquet_id);
    } else {
        if (sqlite3_prepare_v2(db, "INSERT INTO cart_items (user_id, bouquet_id, quantity) VALUES (?, ?, ?)",
                                -1, &stmt, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, bouquet_id);
        sqlite3_bind_int(stmt, 3, quantity);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int db_delete_cart_item(sqlite3 *db, int user_id, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM cart_items WHERE user_id = ? AND bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

void db_clear_cart(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM cart_items WHERE user_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

char *db_get_cart_postcard(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT has_postcard, postcard_text FROM cart_postcards WHERE user_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    char *result = malloc(600);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        char text_esc[512];
        const char *text = (const char *)sqlite3_column_text(stmt, 1);
        json_escape(text ? text : "", text_esc, sizeof(text_esc));
        snprintf(result, 600, "{\"has_postcard\": %s, \"postcard_text\": \"%s\"}",
                 sqlite3_column_int(stmt, 0) ? "true" : "false", text_esc);
    } else {
        snprintf(result, 600, "{\"has_postcard\": false, \"postcard_text\": \"\"}");
    }

    sqlite3_finalize(stmt);
    return result;
}

int db_set_cart_postcard(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO cart_postcards (user_id, has_postcard, postcard_text) VALUES (?, ?, ?) "
        "ON CONFLICT(user_id) DO UPDATE SET has_postcard = excluded.has_postcard, "
        "postcard_text = excluded.postcard_text";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, has_postcard ? 1 : 0);
    sqlite3_bind_text(stmt, 3, postcard_text, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int db_create_order(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text,
                     const char *delivery_address, const char *phone,
                     const char *delivery_date, const char *delivery_time) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO orders (user_id, status, has_postcard, postcard_text, "
        "delivery_address, phone, delivery_date, delivery_time) "
        "VALUES (?, 'new', ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, has_postcard ? 1 : 0);
    sqlite3_bind_text(stmt, 3, postcard_text ? postcard_text : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, delivery_address ? delivery_address : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, phone ? phone : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, delivery_date ? delivery_date : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, delivery_time ? delivery_time : "", -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(db);
}

int db_add_order_item(sqlite3 *db, int order_id, int bouquet_id,
                       const char *bouquet_name, double price, int quantity) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO order_items (order_id, bouquet_id, bouquet_name, price, quantity) VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, order_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);
    sqlite3_bind_text(stmt, 3, bouquet_name, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, price);
    sqlite3_bind_int(stmt, 5, quantity);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int db_decrement_flower_stock(sqlite3 *db, int flower_id, int amount) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE flowers SET stock = stock - ? WHERE id = ? AND stock >= ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, amount);
    sqlite3_bind_int(stmt, 2, flower_id);
    sqlite3_bind_int(stmt, 3, amount);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

static char *order_items_json(sqlite3 *db, int order_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT bouquet_id, bouquet_name, price, quantity FROM order_items WHERE order_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char *empty = malloc(3); strcpy(empty, "[]"); return empty;
    }
    sqlite3_bind_int(stmt, 1, order_id);

    size_t cap = 1024;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;
    char name_esc[256], row_buf[400];

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        json_escape(name ? name : "", name_esc, sizeof(name_esc));

        snprintf(row_buf, sizeof(row_buf),
            "{\"bouquet_id\": %d, \"name\": \"%s\", \"price\": %.2f, \"quantity\": %d}",
            sqlite3_column_int(stmt, 0), name_esc,
            sqlite3_column_double(stmt, 2), sqlite3_column_int(stmt, 3));

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_orders_for_user(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id, status, has_postcard, postcard_text, created_at, "
        "       delivery_address, phone, delivery_date, delivery_time, cancel_reason "
        "FROM orders WHERE user_id = ? ORDER BY id DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int order_id = sqlite3_column_int(stmt, 0);
        const char *status = (const char *)sqlite3_column_text(stmt, 1);
        const char *created = (const char *)sqlite3_column_text(stmt, 4);
        const char *address = (const char *)sqlite3_column_text(stmt, 5);
        const char *phone = (const char *)sqlite3_column_text(stmt, 6);
        const char *ddate = (const char *)sqlite3_column_text(stmt, 7);
        const char *dtime = (const char *)sqlite3_column_text(stmt, 8);
        const char *cancel_reason = (const char *)sqlite3_column_text(stmt, 9);

        char text_esc[512], addr_esc[512], phone_esc[64], date_esc[32], time_esc[32], reason_esc[512];
        const char *text = (const char *)sqlite3_column_text(stmt, 3);
        json_escape(text ? text : "", text_esc, sizeof(text_esc));
        json_escape(address ? address : "", addr_esc, sizeof(addr_esc));
        json_escape(phone ? phone : "", phone_esc, sizeof(phone_esc));
        json_escape(ddate ? ddate : "", date_esc, sizeof(date_esc));
        json_escape(dtime ? dtime : "", time_esc, sizeof(time_esc));
        json_escape(cancel_reason ? cancel_reason : "", reason_esc, sizeof(reason_esc));

        char *items = order_items_json(db, order_id);

        size_t row_cap = strlen(items) + 800;
        char *row_buf = malloc(row_cap);
        snprintf(row_buf, row_cap,
            "{\"id\": %d, \"status\": \"%s\", \"has_postcard\": %s, \"postcard_text\": \"%s\", "
            "\"created_at\": \"%s\", \"delivery_address\": \"%s\", \"phone\": \"%s\", "
            "\"delivery_date\": \"%s\", \"delivery_time\": \"%s\", \"cancel_reason\": \"%s\", "
            "\"items\": %s}",
            order_id, status ? status : "new",
            sqlite3_column_int(stmt, 2) ? "true" : "false",
            text_esc, created ? created : "",
            addr_esc, phone_esc, date_esc, time_esc, reason_esc, items);
        free(items);

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        free(row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_get_all_orders(sqlite3 *db, int filter_user_id) {
    sqlite3_stmt *stmt;
    const char *sql_all =
        "SELECT orders.id, orders.status, orders.has_postcard, orders.postcard_text, orders.created_at, "
        "       users.id, users.name, users.email, "
        "       orders.delivery_address, orders.phone, orders.delivery_date, orders.delivery_time, "
        "       orders.cancel_reason "
        "FROM orders JOIN users ON orders.user_id = users.id "
        "ORDER BY orders.id DESC";
    const char *sql_filtered =
        "SELECT orders.id, orders.status, orders.has_postcard, orders.postcard_text, orders.created_at, "
        "       users.id, users.name, users.email, "
        "       orders.delivery_address, orders.phone, orders.delivery_date, orders.delivery_time, "
        "       orders.cancel_reason "
        "FROM orders JOIN users ON orders.user_id = users.id "
        "WHERE orders.user_id = ? "
        "ORDER BY orders.id DESC";

    int filtering = filter_user_id >= 0;
    if (sqlite3_prepare_v2(db, filtering ? sql_filtered : sql_all, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    if (filtering) sqlite3_bind_int(stmt, 1, filter_user_id);

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int order_id = sqlite3_column_int(stmt, 0);
        const char *status = (const char *)sqlite3_column_text(stmt, 1);
        const char *created = (const char *)sqlite3_column_text(stmt, 4);
        const char *user_name = (const char *)sqlite3_column_text(stmt, 6);
        const char *user_email = (const char *)sqlite3_column_text(stmt, 7);
        const char *address = (const char *)sqlite3_column_text(stmt, 8);
        const char *phone = (const char *)sqlite3_column_text(stmt, 9);
        const char *ddate = (const char *)sqlite3_column_text(stmt, 10);
        const char *dtime = (const char *)sqlite3_column_text(stmt, 11);
        const char *cancel_reason = (const char *)sqlite3_column_text(stmt, 12);

        char text_esc[512], name_esc[256], email_esc[256];
        char addr_esc[512], phone_esc[64], date_esc[32], time_esc[32], reason_esc[512];
        const char *text = (const char *)sqlite3_column_text(stmt, 3);
        json_escape(text ? text : "", text_esc, sizeof(text_esc));
        json_escape(user_name ? user_name : "", name_esc, sizeof(name_esc));
        json_escape(user_email ? user_email : "", email_esc, sizeof(email_esc));
        json_escape(address ? address : "", addr_esc, sizeof(addr_esc));
        json_escape(phone ? phone : "", phone_esc, sizeof(phone_esc));
        json_escape(ddate ? ddate : "", date_esc, sizeof(date_esc));
        json_escape(dtime ? dtime : "", time_esc, sizeof(time_esc));
        json_escape(cancel_reason ? cancel_reason : "", reason_esc, sizeof(reason_esc));

        char *items = order_items_json(db, order_id);

        size_t row_cap = strlen(items) + 1000;
        char *row_buf = malloc(row_cap);
        snprintf(row_buf, row_cap,
            "{\"id\": %d, \"status\": \"%s\", \"has_postcard\": %s, \"postcard_text\": \"%s\", "
            "\"created_at\": \"%s\", \"user_id\": %d, \"user_name\": \"%s\", \"user_email\": \"%s\", "
            "\"delivery_address\": \"%s\", \"phone\": \"%s\", \"delivery_date\": \"%s\", "
            "\"delivery_time\": \"%s\", \"cancel_reason\": \"%s\", \"items\": %s}",
            order_id, status ? status : "new",
            sqlite3_column_int(stmt, 2) ? "true" : "false",
            text_esc, created ? created : "",
            sqlite3_column_int(stmt, 5), name_esc, email_esc,
            addr_esc, phone_esc, date_esc, time_esc, reason_esc, items);
        free(items);

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        free(row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

int db_update_order_status(sqlite3 *db, int order_id, const char *status) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE orders SET status = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, order_id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

int db_cancel_order(sqlite3 *db, int order_id, const char *reason) {
    sqlite3_stmt *check;
    if (sqlite3_prepare_v2(db, "SELECT status FROM orders WHERE id = ?", -1, &check, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(check, 1, order_id);

    int found = 0, already_cancelled = 0;
    if (sqlite3_step(check) == SQLITE_ROW) {
        found = 1;
        const char *status = (const char *)sqlite3_column_text(check, 0);
        already_cancelled = status && strcmp(status, "cancelled") == 0;
    }
    sqlite3_finalize(check);

    if (!found) return -1;
    if (already_cancelled) return 0;

    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);

    sqlite3_stmt *upd;
    if (sqlite3_prepare_v2(db, "UPDATE orders SET status = 'cancelled', cancel_reason = ? WHERE id = ?",
                            -1, &upd, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }
    sqlite3_bind_text(upd, 1, reason ? reason : "", -1, SQLITE_STATIC);
    sqlite3_bind_int(upd, 2, order_id);
    sqlite3_step(upd);
    sqlite3_finalize(upd);

    sqlite3_stmt *items;
    if (sqlite3_prepare_v2(db, "SELECT bouquet_id, quantity FROM order_items WHERE order_id = ?",
                            -1, &items, NULL) == SQLITE_OK) {
        sqlite3_bind_int(items, 1, order_id);
        while (sqlite3_step(items) == SQLITE_ROW) {
            int bouquet_id = sqlite3_column_int(items, 0);
            int order_qty = sqlite3_column_int(items, 1);

            sqlite3_stmt *comp;
            if (sqlite3_prepare_v2(db, "SELECT flower_id, quantity FROM bouquet_items WHERE bouquet_id = ?",
                                    -1, &comp, NULL) == SQLITE_OK) {
                sqlite3_bind_int(comp, 1, bouquet_id);
                while (sqlite3_step(comp) == SQLITE_ROW) {
                    int flower_id = sqlite3_column_int(comp, 0);
                    int per_unit = sqlite3_column_int(comp, 1);

                    sqlite3_stmt *restore;
                    if (sqlite3_prepare_v2(db, "UPDATE flowers SET stock = stock + ? WHERE id = ?",
                                            -1, &restore, NULL) == SQLITE_OK) {
                        sqlite3_bind_int(restore, 1, per_unit * order_qty);
                        sqlite3_bind_int(restore, 2, flower_id);
                        sqlite3_step(restore);
                        sqlite3_finalize(restore);
                    }
                }
                sqlite3_finalize(comp);
            }
        }
        sqlite3_finalize(items);
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    return 1;
}

int db_get_order_info(sqlite3 *db, int order_id, int *out_user_id, char *out_status, size_t out_status_size) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT user_id, status FROM orders WHERE id = ?", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(stmt, 1, order_id);

    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = 1;
        *out_user_id = sqlite3_column_int(stmt, 0);
        const char *status = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(out_status, status ? status : "new", out_status_size - 1);
        out_status[out_status_size - 1] = '\0';
    }
    sqlite3_finalize(stmt);
    return found;
}

#define MAX_CHECKOUT_BOUQUETS 50
#define MAX_REQUIRED_FLOWERS 200

int db_checkout(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text,
                 const char *delivery_address, const char *phone,
                 const char *delivery_date, const char *delivery_time,
                 int *out_order_id, char *out_error, size_t out_error_size) {
    typedef struct {
        int bouquet_id;
        int quantity;
        char name[256];
        double price;
    } CartRow;

    CartRow rows[MAX_CHECKOUT_BOUQUETS];
    int count = 0;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT bouquets.id, cart_items.quantity, bouquets.name "
        "FROM cart_items JOIN bouquets ON cart_items.bouquet_id = bouquets.id "
        "WHERE cart_items.user_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(out_error, out_error_size, "Ошибка базы данных");
        return 0;
    }
    sqlite3_bind_int(stmt, 1, user_id);

    while (sqlite3_step(stmt) == SQLITE_ROW && count < MAX_CHECKOUT_BOUQUETS) {
        rows[count].bouquet_id = sqlite3_column_int(stmt, 0);
        rows[count].quantity = sqlite3_column_int(stmt, 1);
        const char *name = (const char *)sqlite3_column_text(stmt, 2);
        strncpy(rows[count].name, name ? name : "", sizeof(rows[count].name) - 1);
        rows[count].name[sizeof(rows[count].name) - 1] = '\0';
        rows[count].price = bouquet_price(db, rows[count].bouquet_id);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        snprintf(out_error, out_error_size, "Корзина пуста");
        return 0;
    }

    int req_flower_id[MAX_REQUIRED_FLOWERS];
    int req_amount[MAX_REQUIRED_FLOWERS];
    char req_flower_name[MAX_REQUIRED_FLOWERS][256];
    int req_count = 0;

    for (int i = 0; i < count; i++) {
        sqlite3_stmt *comp;
        if (sqlite3_prepare_v2(db,
                "SELECT flowers.id, flowers.name, bouquet_items.quantity "
                "FROM bouquet_items JOIN flowers ON bouquet_items.flower_id = flowers.id "
                "WHERE bouquet_items.bouquet_id = ?",
                -1, &comp, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int(comp, 1, rows[i].bouquet_id);

        while (sqlite3_step(comp) == SQLITE_ROW) {
            int fid = sqlite3_column_int(comp, 0);
            const char *fname = (const char *)sqlite3_column_text(comp, 1);
            int per_unit = sqlite3_column_int(comp, 2);
            int needed = per_unit * rows[i].quantity;

            int found = -1;
            for (int k = 0; k < req_count; k++) {
                if (req_flower_id[k] == fid) { found = k; break; }
            }
            if (found >= 0) {
                req_amount[found] += needed;
            } else if (req_count < MAX_REQUIRED_FLOWERS) {
                req_flower_id[req_count] = fid;
                req_amount[req_count] = needed;
                strncpy(req_flower_name[req_count], fname ? fname : "", 255);
                req_flower_name[req_count][255] = '\0';
                req_count++;
            }
        }
        sqlite3_finalize(comp);
    }

    if (req_count == 0) {
        snprintf(out_error, out_error_size, "В корзине есть букет без состава — оформление невозможно");
        return 0;
    }

    for (int k = 0; k < req_count; k++) {
        sqlite3_stmt *stock_stmt;
        int stock = 0;
        if (sqlite3_prepare_v2(db, "SELECT stock FROM flowers WHERE id = ?", -1, &stock_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stock_stmt, 1, req_flower_id[k]);
            if (sqlite3_step(stock_stmt) == SQLITE_ROW) stock = sqlite3_column_int(stock_stmt, 0);
            sqlite3_finalize(stock_stmt);
        }
        if (req_amount[k] > stock) {
            snprintf(out_error, out_error_size,
                     "Недостаточно на складе: \"%s\" (нужно %d, в наличии %d)",
                     req_flower_name[k], req_amount[k], stock);
            return 0;
        }
    }

    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);

    int order_id = db_create_order(db, user_id, has_postcard, postcard_text,
                                    delivery_address, phone, delivery_date, delivery_time);
    if (order_id < 0) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        snprintf(out_error, out_error_size, "Не удалось создать заказ");
        return 0;
    }

    int ok = 1;

    for (int k = 0; k < req_count && ok; k++) {
        if (!db_decrement_flower_stock(db, req_flower_id[k], req_amount[k])) {
            ok = 0;
            snprintf(out_error, out_error_size, "Недостаточно на складе: \"%s\"", req_flower_name[k]);
        }
    }

    for (int i = 0; i < count && ok; i++) {
        if (!db_add_order_item(db, order_id, rows[i].bouquet_id, rows[i].name, rows[i].price, rows[i].quantity)) {
            ok = 0;
            snprintf(out_error, out_error_size, "Ошибка при сохранении заказа");
        }
    }

    if (!ok) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return 0;
    }

    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

    db_clear_cart(db, user_id);

    sqlite3_stmt *pc;
    if (sqlite3_prepare_v2(db, "DELETE FROM cart_postcards WHERE user_id = ?", -1, &pc, NULL) == SQLITE_OK) {
        sqlite3_bind_int(pc, 1, user_id);
        sqlite3_step(pc);
        sqlite3_finalize(pc);
    }

    *out_order_id = order_id;
    return 1;
}

char *db_get_admin_stats(sqlite3 *db) {
    char status_counts[300] = "{";
    const char *statuses[] = {"new", "processing", "delivered", "cancelled"};
    int first = 1;

    for (int i = 0; i < 4; i++) {
        sqlite3_stmt *stmt;
        int count = 0;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM orders WHERE status = ?", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, statuses[i], -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        char piece[64];
        snprintf(piece, sizeof(piece), "%s\"%s\": %d", first ? "" : ", ", statuses[i], count);
        strcat(status_counts, piece);
        first = 0;
    }
    strcat(status_counts, "}");

    double revenue = 0;
    sqlite3_stmt *rev_stmt;
    const char *rev_sql =
        "SELECT COALESCE(SUM(order_items.price * order_items.quantity), 0) "
        "FROM order_items JOIN orders ON order_items.order_id = orders.id "
        "WHERE orders.status = 'delivered'";
    if (sqlite3_prepare_v2(db, rev_sql, -1, &rev_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(rev_stmt) == SQLITE_ROW) revenue = sqlite3_column_double(rev_stmt, 0);
        sqlite3_finalize(rev_stmt);
    }

    char top_bouquets[2048] = "[";
    first = 1;
    sqlite3_stmt *top_stmt;
    const char *top_sql =
        "SELECT order_items.bouquet_name, SUM(order_items.quantity) as total_qty "
        "FROM order_items JOIN orders ON order_items.order_id = orders.id "
        "WHERE orders.status != 'cancelled' "
        "GROUP BY order_items.bouquet_name "
        "ORDER BY total_qty DESC LIMIT 5";
    if (sqlite3_prepare_v2(db, top_sql, -1, &top_stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(top_stmt) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(top_stmt, 0);
            char name_esc[256];
            json_escape(name ? name : "", name_esc, sizeof(name_esc));

            char piece[350];
            snprintf(piece, sizeof(piece), "%s{\"name\": \"%s\", \"total_quantity\": %d}",
                     first ? "" : ", ", name_esc, sqlite3_column_int(top_stmt, 1));
            strcat(top_bouquets, piece);
            first = 0;
        }
        sqlite3_finalize(top_stmt);
    }
    strcat(top_bouquets, "]");

    size_t out_size = strlen(status_counts) + strlen(top_bouquets) + 128;
    char *result = malloc(out_size);
    snprintf(result, out_size,
        "{\"status_counts\": %s, \"revenue\": %.2f, \"top_bouquets\": %s}",
        status_counts, revenue, top_bouquets);
    return result;
}

static void address_row_to_json(sqlite3_stmt *stmt, char *out, size_t out_size) {
    char label_esc[128], addr_esc[512], phone_esc[64];

    const char *label = (const char *)sqlite3_column_text(stmt, 1);
    const char *address = (const char *)sqlite3_column_text(stmt, 2);
    const char *phone = (const char *)sqlite3_column_text(stmt, 3);

    json_escape(label ? label : "", label_esc, sizeof(label_esc));
    json_escape(address ? address : "", addr_esc, sizeof(addr_esc));
    json_escape(phone ? phone : "", phone_esc, sizeof(phone_esc));

    snprintf(out, out_size,
        "{\"id\": %d, \"label\": \"%s\", \"address\": \"%s\", \"phone\": \"%s\"}",
        sqlite3_column_int(stmt, 0), label_esc, addr_esc, phone_esc);
}

char *db_get_addresses(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, label, address, phone FROM addresses WHERE user_id = ? ORDER BY id DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    size_t cap = 2048;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;
    char row_buf[700];

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        address_row_to_json(stmt, row_buf, sizeof(row_buf));

        size_t needed = strlen(result) + strlen(row_buf) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, row_buf);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

char *db_add_address(sqlite3 *db, int user_id, const char *label, const char *address, const char *phone) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO addresses (user_id, label, address, phone) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, label ? label : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, address, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, phone, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return NULL;

    int new_id = (int)sqlite3_last_insert_rowid(db);

    sqlite3_stmt *sel;
    if (sqlite3_prepare_v2(db, "SELECT id, label, address, phone FROM addresses WHERE id = ?", -1, &sel, NULL) != SQLITE_OK)
        return NULL;
    sqlite3_bind_int(sel, 1, new_id);

    char *result = NULL;
    if (sqlite3_step(sel) == SQLITE_ROW) {
        result = malloc(700);
        address_row_to_json(sel, result, 700);
    }
    sqlite3_finalize(sel);
    return result;
}

int db_delete_address(sqlite3 *db, int user_id, int address_id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM addresses WHERE id = ? AND user_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, address_id);
    sqlite3_bind_int(stmt, 2, user_id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

char *db_get_favorites(sqlite3 *db, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT bouquets.id, bouquets.name, bouquets.description, bouquets.image_url, "
        "       bouquets.owner_user_id, bouquets.created_at "
        "FROM favorites JOIN bouquets ON favorites.bouquet_id = bouquets.id "
        "WHERE favorites.user_id = ? ORDER BY favorites.id DESC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_int(stmt, 1, user_id);

    size_t cap = 4096;
    char *result = malloc(cap);
    strcpy(result, "[");
    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char *obj = bouquet_to_json(db, stmt);
        size_t needed = strlen(result) + strlen(obj) + 4;
        if (needed > cap) { cap = needed * 2; result = realloc(result, cap); }
        if (!first) strcat(result, ",");
        strcat(result, obj);
        free(obj);
        first = 0;
    }
    strcat(result, "]");

    sqlite3_finalize(stmt);
    return result;
}

int db_add_favorite(sqlite3 *db, int user_id, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO favorites (user_id, bouquet_id) VALUES (?, ?) "
        "ON CONFLICT(user_id, bouquet_id) DO NOTHING";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int db_remove_favorite(sqlite3 *db, int user_id, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM favorites WHERE user_id = ? AND bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

int db_is_favorite(sqlite3 *db, int user_id, int bouquet_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT 1 FROM favorites WHERE user_id = ? AND bouquet_id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, bouquet_id);

    int found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}
