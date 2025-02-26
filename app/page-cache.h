//
// Created by danis on 27.02.2025.
//

#ifndef PAGE_CACHE_H
#define PAGE_CACHE_H

#include <stddef.h>  // Для size_t
#include <sys/types.h>  // Для off_t и ssize_t

#ifdef _WIN32
    #define LAB2_API __declspec(dllexport)
#else
    #define LAB2_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

    // Открытие файла по заданному пути файла, доступного для чтения.
    // Процедура возвращает некоторый хэндл на файл.
    // Возвращает -1 в случае ошибки.
    LAB2_API int lab2_open(const char *path);

    // Закрытие файла по хэндлу.
    // Возвращает 0 в случае успеха, -1 в случае ошибки.
    LAB2_API int lab2_close(int fd);

    // Чтение данных из файла.
    // fd — дескриптор файла.
    // buf — буфер для чтения данных.
    // count — количество байт для чтения.
    // Возвращает количество прочитанных байт или -1 в случае ошибки.
    LAB2_API ssize_t lab2_read(int fd, void *buf, size_t count);

    // Запись данных в файл.
    // fd — дескриптор файла.
    // buf — буфер с данными для записи.
    // count — количество байт для записи.
    // Возвращает количество записанных байт или -1 в случае ошибки.
    LAB2_API ssize_t lab2_write(int fd, const void *buf, size_t count);

    // Перестановка позиции указателя на данные файла.
    // fd — дескриптор файла.
    // offset — новое смещение.
    // whence — флаг (например, SEEK_SET для абсолютного смещения).
    // Возвращает новое смещение или -1 в случае ошибки.
    LAB2_API off_t lab2_lseek(int fd, off_t offset, int whence);

    // Синхронизация данных из кэша с диском.
    // fd — дескриптор файла.
    // Возвращает 0 в случае успеха, -1 в случае ошибки.
    LAB2_API int lab2_fsync(int fd);

#ifdef __cplusplus
}
#endif

#endif // PAGE_CACHE_H
