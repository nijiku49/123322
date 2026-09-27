#ifndef DB_H
#define DB_H

#include <sqlite3.h>

sqlite3 *db_init(const char *filename);

void db_close(sqlite3 *db);

char *db_get_all_flowers(sqlite3 *db);

char *db_get_flower(sqlite3 *db, int id);

char *db_add_flower(sqlite3 *db, const char *name, const char *description,
                     double price, int stock, const char *image_url);

char *db_update_flower(sqlite3 *db, int id, const char *name, const char *description,
                        double price, int stock, const char *image_url);

int db_delete_flower(sqlite3 *db, int id);

int db_delete_flower_cascade(sqlite3 *db, int flower_id,
                              int *out_cancelled_order_ids, int max_ids, int *out_cancelled_count);

char *db_create_user(sqlite3 *db, const char *name, const char *email, const char *password);
char *db_get_user_by_email(sqlite3 *db, const char *email);
char *db_get_user_by_id(sqlite3 *db, int id);
char *db_get_all_users(sqlite3 *db);
char *db_update_user(sqlite3 *db, int id, const char *name, const char *email, const char *password);
int db_delete_user(sqlite3 *db, int id);

typedef struct {
    int flower_id;
    int quantity;
} BouquetItemInput;

char *db_get_all_bouquets(sqlite3 *db);

char *db_get_user_bouquets(sqlite3 *db, int user_id);

char *db_get_bouquet(sqlite3 *db, int bouquet_id);

int db_get_bouquet_owner(sqlite3 *db, int bouquet_id);

int db_get_bouquet_availability(sqlite3 *db, int bouquet_id);

int db_create_bouquet(sqlite3 *db, const char *name, const char *description, const char *image_url,
                       int owner_user_id, const BouquetItemInput *items, int item_count);

int db_delete_bouquet(sqlite3 *db, int bouquet_id);

char *db_get_all_bouquets_admin(sqlite3 *db);

int db_update_bouquet(sqlite3 *db, int bouquet_id, const char *name, const char *description,
                       const char *image_url, const BouquetItemInput *items, int item_count);

char *db_get_cart_items(sqlite3 *db, int user_id);

int db_get_cart_item_quantity(sqlite3 *db, int user_id, int bouquet_id);
int db_upsert_cart_item(sqlite3 *db, int user_id, int bouquet_id, int quantity);
int db_delete_cart_item(sqlite3 *db, int user_id, int bouquet_id);
void db_clear_cart(sqlite3 *db, int user_id);

char *db_get_cart_postcard(sqlite3 *db, int user_id);
int db_set_cart_postcard(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text);

int db_create_order(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text,
                     const char *delivery_address, const char *phone,
                     const char *delivery_date, const char *delivery_time);

int db_add_order_item(sqlite3 *db, int order_id, int bouquet_id,
                       const char *bouquet_name, double price, int quantity);

int db_decrement_flower_stock(sqlite3 *db, int flower_id, int amount);

char *db_get_orders_for_user(sqlite3 *db, int user_id);

char *db_get_all_orders(sqlite3 *db, int filter_user_id);

int db_update_order_status(sqlite3 *db, int order_id, const char *status);
int db_cancel_order(sqlite3 *db, int order_id, const char *reason);

int db_get_order_info(sqlite3 *db, int order_id, int *out_user_id, char *out_status, size_t out_status_size);

int db_checkout(sqlite3 *db, int user_id, int has_postcard, const char *postcard_text,
                 const char *delivery_address, const char *phone,
                 const char *delivery_date, const char *delivery_time,
                 int *out_order_id, char *out_error, size_t out_error_size);

char *db_get_admin_stats(sqlite3 *db);

char *db_get_addresses(sqlite3 *db, int user_id);

char *db_add_address(sqlite3 *db, int user_id, const char *label, const char *address, const char *phone);

int db_delete_address(sqlite3 *db, int user_id, int address_id);

char *db_get_favorites(sqlite3 *db, int user_id);

int db_add_favorite(sqlite3 *db, int user_id, int bouquet_id);

int db_remove_favorite(sqlite3 *db, int user_id, int bouquet_id);

int db_is_favorite(sqlite3 *db, int user_id, int bouquet_id);

#endif
