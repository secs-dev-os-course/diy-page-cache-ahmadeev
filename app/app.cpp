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
