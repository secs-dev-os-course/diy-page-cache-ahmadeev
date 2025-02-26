//
// Created by danis on 27.02.2025.
//

#include "page-cache.h"

#include <windows.h>
#include <unordered_map>
#include <queue>
#include <vector>
#include <mutex>
#include <cstring>
#include <iostream>

#define BLOCK_SIZE 4096       // Размер блока (4 КБ)
#define CACHE_CAPACITY 256    // Максимальное количество блоков в кэше

// Логирование
#define DEBUG_LOG(message) std::cout << "[DEBUG] " << message << std::endl

// Структура блока кэша
struct CacheBlock {
    int fd;                  // Дескриптор файла
    off_t offset;            // Смещение блока в файле
    std::vector<char> data;  // Данные блока (4 КБ)
    bool dirty;              // Флаг "грязного" блока
};

// Хэш-функция для пары (fd, offset)
struct PairHash {
    size_t operator()(const std::pair<int, off_t>& p) const {
        return std::hash<int>()(p.first) ^ std::hash<off_t>()(p.second);
    }
};

// Глобальные структуры для управления кэшем
std::unordered_map<int, HANDLE> open_files;  // Дескрипторы файлов
std::queue<CacheBlock*> fifo_queue;          // Очередь для FIFO
std::unordered_map<std::pair<int, off_t>, CacheBlock*, PairHash> blocks_map; // Быстрый поиск блоков
std::mutex cache_mutex;                      // Мьютекс для потокобезопасности

// Открытие файла
int lab2_open(const char *path) {
    DEBUG_LOG("lab2_open: Открытие файла " << path);
    HANDLE hFile = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,  // Обход кэша ОС
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        DEBUG_LOG("lab2_open: Ошибка открытия файла " << path);
        return -1;
    }

    std::lock_guard<std::mutex> lock(cache_mutex);
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(hFile));
    open_files[fd] = hFile;
    DEBUG_LOG("lab2_open: Файл открыт, fd=" << fd);
    return fd;
}

// Закрытие файла
int lab2_close(int fd) {
    DEBUG_LOG("lab2_close: Закрытие файла с fd=" << fd);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = open_files.find(fd);
    if (it == open_files.end()) {
        DEBUG_LOG("lab2_close: Файл с fd=" << fd << " не найден");
        return -1;
    }

    // Сброс всех "грязных" блоков на диск
    for (auto& pair : blocks_map) {
        if (pair.first.first == fd && pair.second->dirty) {
            DEBUG_LOG("lab2_close: Сброс грязного блока (fd=" << fd << ", offset=" << pair.second->offset << ") на диск");
            LARGE_INTEGER pos;
            pos.QuadPart = pair.second->offset;
            SetFilePointerEx(it->second, pos, NULL, FILE_BEGIN);
            DWORD written;
            WriteFile(it->second, pair.second->data.data(), BLOCK_SIZE, &written, NULL);
            pair.second->dirty = false;
        }
    }

    // Удаление всех блоков, связанных с файлом
    auto block_it = blocks_map.begin();
    while (block_it != blocks_map.end()) {
        if (block_it->first.first == fd) {
            DEBUG_LOG("lab2_close: Удаление блока (fd=" << fd << ", offset=" << block_it->second->offset << ") из кэша");
            delete block_it->second;
            block_it = blocks_map.erase(block_it);
        } else {
            ++block_it;
        }
    }

    CloseHandle(it->second);
    open_files.erase(it);
    DEBUG_LOG("lab2_close: Файл с fd=" << fd << " успешно закрыт");
    return 0;
}

// Чтение данных
ssize_t lab2_read(int fd, void *buf, size_t count) {
    DEBUG_LOG("lab2_read: Чтение из файла с fd=" << fd << ", count=" << count);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = open_files.find(fd);
    if (it == open_files.end()) {
        DEBUG_LOG("lab2_read: Файл с fd=" << fd << " не найден");
        return -1;
    }

    // Получаем текущую позицию в файле
    off_t current_pos = lab2_lseek(fd, 0, SEEK_CUR);
    if (current_pos == -1) {
        DEBUG_LOG("lab2_read: Ошибка получения текущей позиции файла");
        return -1;
    }

    off_t aligned_offset = current_pos & ~(BLOCK_SIZE - 1);

    // Поиск блока в кэше
    auto key = std::make_pair(fd, aligned_offset);
    auto block_it = blocks_map.find(key);
    if (block_it == blocks_map.end()) {
        DEBUG_LOG("lab2_read: Блок (fd=" << fd << ", offset=" << aligned_offset << ") не найден в кэше, загрузка с диска");
        CacheBlock* new_block = new CacheBlock{fd, aligned_offset, std::vector<char>(BLOCK_SIZE), false};
        LARGE_INTEGER pos;
        pos.QuadPart = aligned_offset;
        SetFilePointerEx(it->second, pos, NULL, FILE_BEGIN);
        DWORD bytes_read;
        ReadFile(it->second, new_block->data.data(), BLOCK_SIZE, &bytes_read, NULL);

        // Вытеснение старого блока, если кэш заполнен
        if (blocks_map.size() >= CACHE_CAPACITY) {
            CacheBlock* oldest = fifo_queue.front();
            DEBUG_LOG("lab2_read: Вытеснение блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") из кэша");
            if (oldest->dirty) {
                DEBUG_LOG("lab2_read: Сброс грязного блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") на диск");
                LARGE_INTEGER old_pos;
                old_pos.QuadPart = oldest->offset;
                SetFilePointerEx(it->second, old_pos, NULL, FILE_BEGIN);
                DWORD written;
                WriteFile(it->second, oldest->data.data(), BLOCK_SIZE, &written, NULL);
            }
            blocks_map.erase(std::make_pair(oldest->fd, oldest->offset));
            fifo_queue.pop();
            delete oldest;
        }

        // Добавление нового блока
        blocks_map[key] = new_block;
        fifo_queue.push(new_block);
        block_it = blocks_map.find(key);
        DEBUG_LOG("lab2_read: Блок (fd=" << fd << ", offset=" << aligned_offset << ") добавлен в кэш");
    }

    // Копирование данных в буфер
    size_t bytes_to_read = std::min(count, static_cast<size_t>(BLOCK_SIZE - (current_pos - aligned_offset)));
    memcpy(buf, block_it->second->data.data() + (current_pos - aligned_offset), bytes_to_read);
    DEBUG_LOG("lab2_read: Прочитано " << bytes_to_read << " байт из блока (fd=" << fd << ", offset=" << aligned_offset << ")");

    // Превентивная загрузка следующего блока
    off_t next_offset = aligned_offset + BLOCK_SIZE;
    auto next_key = std::make_pair(fd, next_offset);
    if (blocks_map.find(next_key) == blocks_map.end()) {
        DEBUG_LOG("lab2_read: Превентивная загрузка блока (fd=" << fd << ", offset=" << next_offset << ")");
        CacheBlock* next_block = new CacheBlock{fd, next_offset, std::vector<char>(BLOCK_SIZE), false};
        LARGE_INTEGER next_pos;
        next_pos.QuadPart = next_offset;
        SetFilePointerEx(it->second, next_pos, NULL, FILE_BEGIN);
        DWORD next_bytes_read;
        ReadFile(it->second, next_block->data.data(), BLOCK_SIZE, &next_bytes_read, NULL);

        if (blocks_map.size() >= CACHE_CAPACITY) {
            CacheBlock* oldest = fifo_queue.front();
            DEBUG_LOG("lab2_read: Вытеснение блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") из кэша");
            if (oldest->dirty) {
                DEBUG_LOG("lab2_read: Сброс грязного блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") на диск");
                LARGE_INTEGER old_pos;
                old_pos.QuadPart = oldest->offset;
                SetFilePointerEx(it->second, old_pos, NULL, FILE_BEGIN);
                DWORD written;
                WriteFile(it->second, oldest->data.data(), BLOCK_SIZE, &written, NULL);
            }
            blocks_map.erase(std::make_pair(oldest->fd, oldest->offset));
            fifo_queue.pop();
            delete oldest;
        }

        blocks_map[next_key] = next_block;
        fifo_queue.push(next_block);
        DEBUG_LOG("lab2_read: Блок (fd=" << fd << ", offset=" << next_offset << ") добавлен в кэш");
    }

    return bytes_to_read;
}

// Запись данных
ssize_t lab2_write(int fd, const void *buf, size_t count) {
    DEBUG_LOG("lab2_write: Запись в файл с fd=" << fd << ", count=" << count);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = open_files.find(fd);
    if (it == open_files.end()) {
        DEBUG_LOG("lab2_write: Файл с fd=" << fd << " не найден");
        return -1;
    }

    // Получаем текущую позицию в файле
    off_t current_pos = lab2_lseek(fd, 0, SEEK_CUR);
    if (current_pos == -1) {
        DEBUG_LOG("lab2_write: Ошибка получения текущей позиции файла");
        return -1;
    }

    off_t aligned_offset = current_pos & ~(BLOCK_SIZE - 1);

    // Поиск блока в кэше
    auto key = std::make_pair(fd, aligned_offset);
    auto block_it = blocks_map.find(key);
    if (block_it == blocks_map.end()) {
        DEBUG_LOG("lab2_write: Блок (fd=" << fd << ", offset=" << aligned_offset << ") не найден в кэше, создание нового");
        CacheBlock* new_block = new CacheBlock{fd, aligned_offset, std::vector<char>(BLOCK_SIZE), false};

        // Вытеснение старого блока, если кэш заполнен
        if (blocks_map.size() >= CACHE_CAPACITY) {
            CacheBlock* oldest = fifo_queue.front();
            DEBUG_LOG("lab2_write: Вытеснение блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") из кэша");
            if (oldest->dirty) {
                DEBUG_LOG("lab2_write: Сброс грязного блока (fd=" << oldest->fd << ", offset=" << oldest->offset << ") на диск");
                LARGE_INTEGER old_pos;
                old_pos.QuadPart = oldest->offset;
                SetFilePointerEx(it->second, old_pos, NULL, FILE_BEGIN);
                DWORD written;
                WriteFile(it->second, oldest->data.data(), BLOCK_SIZE, &written, NULL);
            }
            blocks_map.erase(std::make_pair(oldest->fd, oldest->offset));
            fifo_queue.pop();
            delete oldest;
        }

        // Добавление нового блока
        blocks_map[key] = new_block;
        fifo_queue.push(new_block);
        block_it = blocks_map.find(key);
        DEBUG_LOG("lab2_write: Блок (fd=" << fd << ", offset=" << aligned_offset << ") добавлен в кэш");
    }

    // Запись данных в кэш
    size_t bytes_to_write = std::min(count, static_cast<size_t>(BLOCK_SIZE - (current_pos - aligned_offset)));
    memcpy(block_it->second->data.data() + (current_pos - aligned_offset), buf, bytes_to_write);
    block_it->second->dirty = true;
    DEBUG_LOG("lab2_write: Записано " << bytes_to_write << " байт в блок (fd=" << fd << ", offset=" << aligned_offset << ")");

    return bytes_to_write;
}

// Перемещение указателя файла
off_t lab2_lseek(int fd, off_t offset, int whence) {
    DEBUG_LOG("lab2_lseek: Перемещение указателя файла с fd=" << fd << ", offset=" << offset << ", whence=" << whence);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = open_files.find(fd);
    if (it == open_files.end()) {
        DEBUG_LOG("lab2_lseek: Файл с fd=" << fd << " не найден");
        return -1;
    }

    LARGE_INTEGER new_pos;
    new_pos.QuadPart = offset;
    if (!SetFilePointerEx(it->second, new_pos, NULL, whence)) {
        DEBUG_LOG("lab2_lseek: Ошибка перемещения указателя файла");
        return -1;
    }

    DEBUG_LOG("lab2_lseek: Указатель файла перемещен на позицию " << offset);
    return offset;
}

// Синхронизация данных с диском
int lab2_fsync(int fd) {
    DEBUG_LOG("lab2_fsync: Синхронизация файла с fd=" << fd);
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = open_files.find(fd);
    if (it == open_files.end()) {
        DEBUG_LOG("lab2_fsync: Файл с fd=" << fd << " не найден");
        return -1;
    }

    // Сброс всех "грязных" блоков на диск
    for (auto& pair : blocks_map) {
        if (pair.first.first == fd && pair.second->dirty) {
            DEBUG_LOG("lab2_fsync: Сброс грязного блока (fd=" << fd << ", offset=" << pair.second->offset << ") на диск");
            LARGE_INTEGER pos;
            pos.QuadPart = pair.second->offset;
            SetFilePointerEx(it->second, pos, NULL, FILE_BEGIN);
            DWORD written;
            WriteFile(it->second, pair.second->data.data(), BLOCK_SIZE, &written, NULL);
            pair.second->dirty = false;
        }
    }

    DEBUG_LOG("lab2_fsync: Синхронизация завершена для файла с fd=" << fd);
    return 0;
}