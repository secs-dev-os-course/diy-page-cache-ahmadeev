// benchmark.cpp
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #include <malloc.h> // Для _aligned_malloc и _aligned_free
#endif

#include <bits/fs_fwd.h>

#include "page-cache.h"

#define PRE_RESULT_LOG(opType, cType, time, speed) std::cout << std::format("[{}] [{}] Записано {} МБ за {:.6f} сек. ({:.4f} МБ/c)", opType, cType, TOTAL_SIZE / 1024 / 1024, time, speed) << std::endl
#define RESULT_LOG(opType, cType, time, speed) std::cout << std::format("[{}] [{}] Записано {} МБ за {:.6f} сек. ({:.4f} МБ/c)", opType, cType, TOTAL_SIZE / 1024 / 1024, time, speed) << std::endl

// Размер блока и общий объём данных (100 МБ)
const size_t BLOCK_SIZE = 4096;                  // 4 КБ
const size_t TOTAL_SIZE = (1024 * 4) * 1024 * 25;       // 1 МБ = 1024 * 1024 * 1
const size_t NUM_BLOCKS = TOTAL_SIZE / BLOCK_SIZE;

// Функция для бенчмарка с использованием кэширования ОС (стандартный вывод в файл)
std::array<double, 2> benchmarkOSCacheWrite(const std::string &filename) {
    std::vector<char> buffer(BLOCK_SIZE, 'A'); // Заполняем буфер символами 'A'

    auto start = std::chrono::high_resolution_clock::now();
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Ошибка открытия файла: " << filename << std::endl;
        return {NULL, NULL};
    }

    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        ofs.write(buffer.data(), BLOCK_SIZE);
        if (!ofs) {
            std::cerr << "Ошибка записи в файл." << std::endl;
            break;
        }
    }
    ofs.close();
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;

    PRE_RESULT_LOG("WRITE", "OS Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
}

// Функция для бенчмарка без кэширования ОС
std::array<double, 2> benchmarkNoCacheWrite(const std::string &filename) {
#ifdef _WIN32
    // Открытие файла с флагами для отключения кэширования
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Ошибка CreateFile: " << filename << std::endl;
        return {NULL, NULL};
    }

    // Выделяем выровненный буфер. Выравнивание — как минимум размер сектора (предположим 4096)
    char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
    if (!buffer) {
        std::cerr << "Ошибка выделения выровненной памяти." << std::endl;
        CloseHandle(hFile);
        return {NULL, NULL};
    }
    // Заполняем буфер
    memset(buffer, 'B', BLOCK_SIZE);

    DWORD bytesWritten = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        if (!WriteFile(hFile, buffer, BLOCK_SIZE, &bytesWritten, NULL) || bytesWritten != BLOCK_SIZE) {
            std::cerr << "Ошибка WriteFile на блоке " << i << std::endl;
            break;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    _aligned_free(buffer);
    CloseHandle(hFile);

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;

    PRE_RESULT_LOG("WRITE", "No Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
#else
    std::cerr << "NoCache benchmark доступен только на Windows." << std::endl;
#endif
}

// Функция для бенчмарка с использованием кастомного кэша
std::array<double, 2> benchmarkCustomCacheWrite(const std::string &filename) {
    // Используем lab2_open для открытия файла
    int fd = lab2_open(filename.c_str());
    if (fd == -1) {
        std::cerr << "Ошибка lab2_open: " << filename << std::endl;
        return {NULL, NULL};
    }

    std::vector<char> buffer(BLOCK_SIZE, 'C'); // Заполняем буфер символами 'C'

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        off_t offset = i * BLOCK_SIZE;
        lab2_lseek(fd, offset, SEEK_SET); // Перемещаем указатель
        ssize_t written = lab2_write(fd, buffer.data(), BLOCK_SIZE);
        if (written != BLOCK_SIZE) {
            std::cerr << "Ошибка lab2_write на блоке " << i << std::endl;
            break;
        }
        // fix
        lab2_fsync(fd);
    }
    // можно вызвать lab2_fsync(fd);
    // вывод hit-miss
    // print_hm();
    lab2_close(fd);
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    PRE_RESULT_LOG("WRITE", "Custom Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
}

std::array<double, 2> benchmarkOSCacheReWrite(const std::string &filename) {
    std::vector<char> buffer(BLOCK_SIZE, 'A'); // Заполняем буфер символами 'A'

    // Создаем файл и записываем начальные данные
    /*{
        std::ofstream ofs(filename, std::ios::binary | std::ios::in | std::ios::out);
        if (!ofs) {
            std::cerr << "Ошибка открытия файла: " << filename << std::endl;
            return;
        }

        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            ofs.write(buffer.data(), BLOCK_SIZE);
            if (!ofs) {
                std::cerr << "Ошибка записи в файл." << std::endl;
                break;
            }
        }
        //ofs.close();
    }*/

    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Ошибка открытия файла: " << filename << std::endl;
        return {NULL, NULL};
    }

    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        ofs.write(buffer.data(), BLOCK_SIZE);
        if (!ofs) {
            std::cerr << "Ошибка записи в файл." << std::endl;
            break;
        }
    }
    //ofs.close();

    ofs.seekp(0, std::ios::beg);
    // Перезапись данных
    auto start = std::chrono::high_resolution_clock::now();
    // std::ofstream ofs(filename, std::ios::binary);
    // if (!ofs) {
    //     std::cerr << "Ошибка открытия файла: " << filename << std::endl;
    //     return;
    // }

    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        ofs.write(buffer.data(), BLOCK_SIZE);
        if (!ofs) {
            std::cerr << "Ошибка записи в файл." << std::endl;
            break;
        }
    }
    ofs.close();
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    PRE_RESULT_LOG("REWRITE", "OS Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
}

std::array<double, 2> benchmarkNoCacheReWrite(const std::string &filename) {
#ifdef _WIN32
    // Создаем файл и записываем начальные данные
    {
        HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                   FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL); // FILE_ATTRIBUTE_NORMAL |
        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "Ошибка CreateFile: " << filename << std::endl;
            return {NULL, NULL};
        }

        char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
        if (!buffer) {
            std::cerr << "Ошибка выделения выровненной памяти." << std::endl;
            CloseHandle(hFile);
            return {NULL, NULL};
        }
        memset(buffer, 'B', BLOCK_SIZE);

        DWORD bytesWritten = 0;
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            if (!WriteFile(hFile, buffer, BLOCK_SIZE, &bytesWritten, NULL) || bytesWritten != BLOCK_SIZE) {
                std::cerr << "Ошибка WriteFile на блоке " << i << std::endl;
                break;
            }
        }

        _aligned_free(buffer);
        CloseHandle(hFile);
    }

    // Перезапись данных
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Ошибка CreateFile: " << filename << std::endl;
        return {NULL, NULL};
    }

    char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
    if (!buffer) {
        std::cerr << "Ошибка выделения выровненной памяти." << std::endl;
        CloseHandle(hFile);
        return {NULL, NULL};
    }
    memset(buffer, 'B', BLOCK_SIZE);

    DWORD bytesWritten = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        if (!WriteFile(hFile, buffer, BLOCK_SIZE, &bytesWritten, NULL) || bytesWritten != BLOCK_SIZE) {
            std::cerr << "Ошибка WriteFile на блоке " << i << std::endl;
            break;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    _aligned_free(buffer);
    CloseHandle(hFile);

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    PRE_RESULT_LOG("REWRITE", "No Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
#else
    std::cerr << "NoCache benchmark доступен только на Windows." << std::endl;
#endif
}

std::array<double, 2> benchmarkCustomCacheReWrite(const std::string &filename) {
    int fd = lab2_open(filename.c_str());
    if (fd == -1) {
        std::cerr << "Ошибка lab2_open: " << filename << std::endl;
        return {NULL, NULL};
    }

    std::vector<char> buffer(BLOCK_SIZE, 'C');

    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        off_t offset = i * BLOCK_SIZE;
        lab2_lseek(fd, offset, SEEK_SET); // Перемещаем указатель
        ssize_t written = lab2_write(fd, buffer.data(), BLOCK_SIZE);
        if (written != BLOCK_SIZE) {
            std::cerr << "Ошибка lab2_write на блоке " << i << std::endl;
            break;
        }
        // fix
        // lab2_fsync(fd);
    }
    // fix
    lab2_fsync(fd);
    // вывод hit-miss
    // print_hm();

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        off_t offset = i * BLOCK_SIZE;
        lab2_lseek(fd, offset, SEEK_SET); // Перемещаем указатель
        ssize_t written = lab2_write(fd, buffer.data(), BLOCK_SIZE);
        if (written != BLOCK_SIZE) {
            std::cerr << "Ошибка lab2_write на блоке " << i << std::endl;
            break;
        }
        // fix
        // lab2_fsync(fd);
    }
    // fix
    lab2_fsync(fd);
    // вывод hit-miss
    // print_hm();
    lab2_close(fd);
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    PRE_RESULT_LOG("REWRITE", "Custom Cache", durationSec, throughputMBs);

    return {durationSec, throughputMBs};
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    // Файлы для каждого теста
    std::string fileOS = "benchmark_os.dat";
    std::string fileNoCache = "benchmark_nocache.dat";
    std::string fileCustom = "benchmark_custom.dat";
    std::string fileOS_2 = "benchmark_os_2.dat";
    std::string fileNoCache_2 = "benchmark_nocache_2.dat";
    std::string fileCustom_2 = "benchmark_custom_2.dat";

    std::cout << "Запуск бенчмарков записи (блок " << BLOCK_SIZE / 1024 << " КБ, общий объём " << TOTAL_SIZE / 1024 / 1024 << " МБ)" << std::endl;

    /*// переменные для каждого теста
    int ITERATIONS = 10;

    double speedFileOS = 0;
    double speedFileNoCache = 0;
    double speedFileCustom = 0;
    double speedFileOS_2 = 0;
    double speedFileNoCache_2 = 0;
    double speedFileCustom_2 = 0;

    double timeFileOS = 0;
    double timeFileNoCache = 0;
    double timeFileCustom = 0;
    double timeFileOS_2 = 0;
    double timeFileNoCache_2 = 0;
    double timeFileCustom_2 = 0;

    for(int i = 0; i < ITERATIONS; i++) {
        remove(fileOS.c_str());
        remove(fileNoCache.c_str());
        remove(fileCustom.c_str());

        auto resultOS = benchmarkOSCacheWrite(fileOS);
        auto resultNoCache = benchmarkNoCacheWrite(fileNoCache);
        auto resultCustom = benchmarkCustomCacheWrite(fileCustom);

        timeFileOS += resultOS[0];
        speedFileOS += resultOS[1];
        timeFileNoCache += resultNoCache[0];
        speedFileNoCache += resultNoCache[1];
        timeFileCustom += resultCustom[0];
        speedFileCustom += resultCustom[1];
    }

    RESULT_LOG("WRITE", "OS Cache", timeFileOS / ITERATIONS, speedFileOS / ITERATIONS);
    RESULT_LOG("WRITE", "No Cache", timeFileNoCache / ITERATIONS, speedFileNoCache / ITERATIONS);
    RESULT_LOG("WRITE", "Custom Cache", timeFileCustom / ITERATIONS, speedFileCustom / ITERATIONS);

    for(int i = 0; i < ITERATIONS; i++) {
        remove(fileOS_2.c_str());
        remove(fileNoCache_2.c_str());
        remove(fileCustom_2.c_str());

        auto resultOS = benchmarkOSCacheReWrite(fileOS_2);
        auto resultNoCache = benchmarkNoCacheReWrite(fileNoCache_2);
        auto resultCustom = benchmarkCustomCacheReWrite(fileCustom_2);

        timeFileOS_2 += resultOS[0];
        speedFileOS_2 += resultOS[1];
        timeFileNoCache_2 += resultNoCache[0];
        speedFileNoCache_2 += resultNoCache[1];
        timeFileCustom_2 += resultCustom[0];
        speedFileCustom_2 += resultCustom[1];
    }

    RESULT_LOG("REWRITE", "OS Cache", timeFileOS_2 / ITERATIONS, speedFileOS_2 / ITERATIONS);
    RESULT_LOG("REWRITE", "No Cache", timeFileNoCache_2 / ITERATIONS, speedFileNoCache_2 / ITERATIONS);
    RESULT_LOG("REWRITE", "Custom Cache", timeFileCustom_2 / ITERATIONS, speedFileCustom_2 / ITERATIONS);*/

    remove(fileOS.c_str());
    remove(fileNoCache.c_str());
    remove(fileCustom.c_str());

    benchmarkOSCacheWrite(fileOS);
    benchmarkNoCacheWrite(fileNoCache);
    benchmarkCustomCacheWrite(fileCustom);

    remove(fileOS_2.c_str());
    remove(fileNoCache_2.c_str());
    remove(fileCustom_2.c_str());

    benchmarkOSCacheReWrite(fileOS_2);
    benchmarkNoCacheReWrite(fileNoCache_2);
    benchmarkCustomCacheReWrite(fileCustom_2);

    return 0;
}
