#include <string.h>
#include "nv_storage.h"
#include "nvs_flash.h"
#include "nvs.h"

esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            return ret;
        }
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_write_str(const char* key, char* value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Запись
    ret = nvs_set_str(nvs_handle, key, value);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_read_str(const char* key, char* value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Определение размера строки
    size_t required_size;
    ret = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }
    // Чтение
    ret = nvs_get_str(nvs_handle, key, value, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_write_u16(const char* key, uint16_t value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Запись
    ret = nvs_set_u16(nvs_handle, key, value);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_read_u16(const char* key, uint16_t* value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Чтение
    ret = nvs_get_u16(nvs_handle, key, value);
    if (ret != ESP_OK) {
        return ret;
    }
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_write_u32(const char* key, uint32_t value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Запись
    ret = nvs_set_u32(nvs_handle, key, value);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_read_u32(const char* key, uint32_t* value)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Чтение
    ret = nvs_get_u32(nvs_handle, key, value);
    if (ret != ESP_OK) {
        return ret;
    }
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_write_blob(const char* key, uint8_t* value, size_t size)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Запись
    ret = nvs_set_blob(nvs_handle, key, value, size);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t nvs_read_blob(const char* key, uint8_t* value, size_t* required_size)
{
    nvs_handle_t nvs_handle;
    // Открытие хранилища в пространстве имен "storage"
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Определение размера данных
    size_t size;
    ret = nvs_get_blob(nvs_handle, key, NULL, &size);
    if (ret != ESP_OK) {
        return ret;
    }
    // Чтение
    ret = nvs_get_blob(nvs_handle, key, value, &size);
    if (ret != ESP_OK) {
        return ret;
    }
    // Закрытие хранилища
    nvs_close(nvs_handle);
    return ret;
}
