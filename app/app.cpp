/*
#include <iostream>
#include <string.h>
#include <windows.h>

#include "page-cache.h"

int main() {
    std::cout << "Hello world from app.cpp" << std::endl;

    SetConsoleOutputCP(CP_UTF8);

    const char* file_path = "test_file.txt";  // Путь к файлу
    const char* test_data = "Hello, this is a test string!";  // Тестовые данные
    size_t data_size = strlen(test_data) + 1;  // Размер данных (включая нулевой символ)

    // 1. Открываем файл
    int fd = lab2_open(file_path);
    if (fd == -1) {
        std::cerr << "Ошибка открытия файла!" << std::endl;
        return 1;
    }
    std::cout << "Файл успешно открыт, fd=" << fd << std::endl;

    // 2. Записываем данные в файл
    ssize_t bytes_written = lab2_write(fd, test_data, data_size);
    if (bytes_written == -1) {
        std::cerr << "Ошибка записи в файл!" << std::endl;
        lab2_close(fd);
        return 1;
    }
    std::cout << "Записано " << bytes_written << " байт в файл." << std::endl;

    // 3. Перемещаем указатель на начало файла
    off_t new_pos = lab2_lseek(fd, 0, SEEK_SET);
    if (new_pos == -1) {
        std::cerr << "Ошибка перемещения указателя файла!" << std::endl;
        lab2_close(fd);
        return 1;
    }
    std::cout << "Указатель файла перемещен на позицию " << new_pos << std::endl;

    // 4. Читаем данные из файла
    char buffer[1024] = {0};  // Буфер для чтения
    ssize_t bytes_read = lab2_read(fd, buffer, data_size);
    if (bytes_read == -1) {
        std::cerr << "Ошибка чтения из файла!" << std::endl;
        lab2_close(fd);
        return 1;
    }
    std::cout << "Прочитано " << bytes_read << " байт из файла: " << buffer << std::endl;

    // 5. Закрываем файл
    if (lab2_close(fd) == -1) {
        std::cerr << "Ошибка закрытия файла!" << std::endl;
        return 1;
    }
    std::cout << "Файл успешно закрыт." << std::endl;

    return 0;
}
*/

// benchmark.cpp
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
    #include <malloc.h> // Для _aligned_malloc и _aligned_free
#endif

#include "page-cache.h"

// Размер блока и общий объём данных (100 МБ)
const size_t BLOCK_SIZE = 4096;                  // 4 КБ
const size_t TOTAL_SIZE = 100 * 1024 * 1024;       // 100 МБ
const size_t NUM_BLOCKS = TOTAL_SIZE / BLOCK_SIZE;

// Функция для бенчмарка с использованием кэширования ОС (стандартный вывод в файл)
void benchmarkOSCacheWrite(const std::string &filename) {
    std::vector<char> buffer(BLOCK_SIZE, 'A'); // Заполняем буфер символами 'A'

    auto start = std::chrono::high_resolution_clock::now();
    std::ofstream ofs(filename, std::ios::binary);
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
    ofs.close();
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    std::cout << "[WRITE] [OS Cache] Записано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
}

// Функция для бенчмарка без кэширования ОС
void benchmarkNoCacheWrite(const std::string &filename) {
#ifdef _WIN32
    // Открытие файла с флагами для отключения кэширования
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Ошибка CreateFile: " << filename << std::endl;
        return;
    }

    // Выделяем выровненный буфер. Выравнивание — как минимум размер сектора (предположим 4096)
    char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
    if (!buffer) {
        std::cerr << "Ошибка выделения выровненной памяти." << std::endl;
        CloseHandle(hFile);
        return;
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
    std::cout << "[WRITE] [No Cache] Записано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
#else
    std::cerr << "NoCache benchmark доступен только на Windows." << std::endl;
#endif
}

// Функция для бенчмарка с использованием кастомного кэша
void benchmarkCustomCacheWrite(const std::string &filename) {
    // Используем lab2_open для открытия файла
    int fd = lab2_open(filename.c_str());
    if (fd == -1) {
        std::cerr << "Ошибка lab2_open: " << filename << std::endl;
        return;
    }

    std::vector<char> buffer(BLOCK_SIZE, 'C'); // Заполняем буфер символами 'C'

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        ssize_t written = lab2_write(fd, buffer.data(), BLOCK_SIZE);
        if (written != BLOCK_SIZE) {
            std::cerr << "Ошибка lab2_write на блоке " << i << std::endl;
            break;
        }
        // fix
        lab2_fsync(fd);
    }
    // можно вызвать lab2_fsync(fd);
    lab2_close(fd);
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    std::cout << "[WRITE] [Custom Cache] Записано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
}

void benchmarkOSCacheReWrite(const std::string &filename) {
    std::vector<char> buffer(BLOCK_SIZE, 'A'); // Заполняем буфер символами 'A'

    // Создаем файл и записываем начальные данные
    {
        std::ofstream ofs(filename, std::ios::binary);
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            ofs.write(buffer.data(), BLOCK_SIZE);
        }
    }

    // Перезапись данных
    auto start = std::chrono::high_resolution_clock::now();
    std::ofstream ofs(filename, std::ios::binary | std::ios::in | std::ios::out);
    if (!ofs) {
        std::cerr << "Ошибка открытия файла: " << filename << std::endl;
        return;
    }

    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        ofs.seekp(i * BLOCK_SIZE); // Перемещаем указатель записи
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
    std::cout << "[REWRITE] [OS Cache] Перезаписано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
}

void benchmarkNoCacheReWrite(const std::string &filename) {
#ifdef _WIN32
    // Создаем файл и записываем начальные данные
    {
        HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
        char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
        memset(buffer, 'B', BLOCK_SIZE);

        DWORD bytesWritten;
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            WriteFile(hFile, buffer, BLOCK_SIZE, &bytesWritten, NULL);
        }

        _aligned_free(buffer);
        CloseHandle(hFile);
    }

    // Перезапись данных
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Ошибка CreateFile: " << filename << std::endl;
        return;
    }

    char *buffer = reinterpret_cast<char*>(_aligned_malloc(BLOCK_SIZE, BLOCK_SIZE));
    memset(buffer, 'B', BLOCK_SIZE);

    DWORD bytesWritten;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        LARGE_INTEGER pos;
        pos.QuadPart = i * BLOCK_SIZE;
        SetFilePointerEx(hFile, pos, NULL, FILE_BEGIN); // Перемещаем указатель
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
    std::cout << "[REWRITE] [No Cache] Перезаписано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
#else
    std::cerr << "NoCache benchmark доступен только на Windows." << std::endl;
#endif
}

void benchmarkCustomCacheReWrite(const std::string &filename) {
    /*// Создаем файл и записываем начальные данные
    {
        int fd = lab2_open(filename.c_str());
        std::vector<char> buffer(BLOCK_SIZE, 'C');
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            lab2_write(fd, buffer.data(), BLOCK_SIZE);
        }
        lab2_close(fd);
    }

    // Перезапись данных
    int fd = lab2_open(filename.c_str());
    if (fd == -1) {
        std::cerr << "Ошибка lab2_open: " << filename << std::endl;
        return;
    }*/

    int fd = lab2_open(filename.c_str());
    std::vector<char> buf(BLOCK_SIZE, 'C');
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        lab2_write(fd, buf.data(), BLOCK_SIZE);
    }

    std::vector<char> buffer(BLOCK_SIZE, 'C');
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_BLOCKS; ++i) {
        off_t offset = i * BLOCK_SIZE;
        lab2_lseek(fd, offset, SEEK_SET); // Перемещаем указатель
        ssize_t written = lab2_write(fd, buffer.data(), BLOCK_SIZE);
        if (written != BLOCK_SIZE) {
            std::cerr << "Ошибка lab2_write на блоке " << i << std::endl;
            break;
        }
    }
    lab2_close(fd);
    auto end = std::chrono::high_resolution_clock::now();

    double durationSec = std::chrono::duration<double>(end - start).count();
    double throughputMBs = (TOTAL_SIZE / (1024.0 * 1024.0)) / durationSec;
    std::cout << "[REWRITE] [Custom Cache] Перезаписано " << (TOTAL_SIZE / (1024 * 1024)) << " МБ за "
              << durationSec << " сек. ("
              << throughputMBs << " МБ/с)" << std::endl;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Запуск бенчмарков записи (блок " << BLOCK_SIZE / 1024 << " КБ, общий объём " << TOTAL_SIZE / 1024 / 1024 << " МБ)" << std::endl;

    // Файлы для каждого теста
    std::string fileOS = "benchmark_os.dat";
    std::string fileNoCache = "benchmark_nocache.dat";
    std::string fileCustom = "benchmark_custom.dat";
    std::string fileCustom2 = "benchmark_custom_2.dat";

    benchmarkOSCacheWrite(fileOS);
    benchmarkNoCacheWrite(fileNoCache);
    benchmarkCustomCacheWrite(fileCustom);

    benchmarkOSCacheReWrite(fileOS);
    benchmarkNoCacheReWrite(fileNoCache);
    benchmarkCustomCacheReWrite(fileCustom);

    return 0;
}
