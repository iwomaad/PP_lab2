#define CONSOLE_CP 65001

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
using namespace chrono;

// Результат проверки числа
struct Result {
    int id;             
    long long number;   
    bool isPrime;       
    int node;           
};

// Структура одного узла
struct Node {
    int id = 0;
    queue<Result> Q1;

    mutex queue_mutex;
    condition_variable queue_cv;

    bool closed = false;

    // Задержки для моделирования работы потоков (мс)
    int read_delay_ms = 0;
    int compute_delay_ms = 0;
    int write_delay_ms = 0;


    int processed = 0;        
    size_t max_queue = 0;     
};

// Общие ресурсы всех узлов
struct Shared {
    ifstream input;
    ofstream output;
    mutex input_mutex;
    mutex output_mutex;
    mutex log_mutex;
    int counter = 0;                
    atomic<int> total{ 0 };           
    bool verbose = true;             // выводить подробный журнал
    steady_clock::time_point start; 
};

// Время от запуска в секундах
double elapsed(Shared& sh) {
    return duration<double>(steady_clock::now() - sh.start).count();
}

// Строка журнала
void print_log(Shared& sh, int node, int thread_no, const string& text) {
    if (!sh.verbose)
        return;
    lock_guard<mutex> lock(sh.log_mutex);
    cout << "[" << fixed << setprecision(3) << setw(7) << elapsed(sh) << " c] "
        << "Узел " << node << ", поток " << thread_no << ": " << text << endl;
}

// Задержка для моделирования
void delay(int ms) {
    if (ms > 0)
        this_thread::sleep_for(milliseconds(ms));
}

// Проверка числа на простоту
bool is_prime(long long number) {
    if (number < 2)
        return false;

    for (long long i = 2; i <= number / i; i++) {
        if (number % i == 0)
            return false;
    }

    return true;
}

// Поток 1: чтение и вычисление
void reader(Node& node, Shared& sh) {
    while (true) {
        long long number;
        int id;

        // Блокировка входного файла
        {
            lock_guard<mutex> lock(sh.input_mutex);

            if (!(sh.input >> number))
                break;

            id = ++sh.counter;
            delay(node.read_delay_ms);      // задержка чтения
        }

        print_log(sh, node.id, 1, "прочитал число " + to_string(id)
            + " = " + to_string(number));

        bool prime = is_prime(number);
        delay(node.compute_delay_ms);       // задержка вычисления

        // Помещение результата в очередь
        {
            lock_guard<mutex> lock(node.queue_mutex);

            node.Q1.push({ id, number, prime, node.id });
            if (node.Q1.size() > node.max_queue)
                node.max_queue = node.Q1.size();
        }

        node.queue_cv.notify_one();
        node.processed++;

        print_log(sh, node.id, 1, "число " + to_string(id)
            + (prime ? " простое" : " составное")
            + ", результат в очереди Q1");
    }

    // Закрытие очереди после окончания чтения
    {
        lock_guard<mutex> lock(node.queue_mutex);
        node.closed = true;
    }

    node.queue_cv.notify_one();
    print_log(sh, node.id, 1, "входной файл закончился, очередь закрыта");
}

// Поток 2: запись результатов
void writer(Node& node, Shared& sh) {
    while (true) {
        Result result;

        // Ожидание появления результата
        {
            unique_lock<mutex> lock(node.queue_mutex);

            node.queue_cv.wait(lock, [&]() {
                return !node.Q1.empty() || node.closed;
                });

            // Проверка завершения работы
            if (node.Q1.empty() && node.closed)
                break;

            // Извлечение результата из очереди
            result = node.Q1.front();
            node.Q1.pop();
        }

        // Блокировка выходного файла
        {
            lock_guard<mutex> lock(sh.output_mutex);

            delay(node.write_delay_ms);     // задержка записи

            // Запись одного результата
            sh.output << result.id << ". " << result.number << " - "
                << (result.isPrime ? "да" : "нет")
                << " (узел " << result.node << ")" << endl;
            sh.total++;
        }

        print_log(sh, node.id, 2, "записал результат " + to_string(result.id));
    }

    print_log(sh, node.id, 2, "очередь закрыта и пуста, поток завершён");
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CONSOLE_CP);
    SetConsoleCP(CONSOLE_CP);
#endif

    int N;

    cout << "Введите количество узлов: ";
    if (!(cin >> N) || N <= 0) {
        cout << "Количество узлов должно быть положительным.\n";
        return 1;
    }

    // Выбор режима моделирования
    cout << "\nРежим моделирования:\n"
        << "  0 - без задержек\n"
        << "  1 - ситуация 1: медленная запись\n"
        << "  2 - ситуация 2: медленное вычисление\n"
        << "  3 - ситуация 3: узел 1 медленнее остальных\n"
        << "  4 - свои параметры\n"
        << "Ваш выбор: ";

    int mode;
    cin >> mode;

    int read_ms = 0, compute_ms = 0, write_ms = 0;
    int slow_node = 0, slow_factor = 1;

    switch (mode) {
    case 1: read_ms = 0; compute_ms = 0;  write_ms = 120; break;
    case 2: read_ms = 0; compute_ms = 150; write_ms = 0;  break;
    case 3: read_ms = 0; compute_ms = 50;  write_ms = 0;
        slow_node = 1; slow_factor = 4; break;
    case 4:
        cout << "Задержка чтения, мс: ";      cin >> read_ms;
        cout << "Задержка вычисления, мс: ";  cin >> compute_ms;
        cout << "Задержка записи, мс: ";      cin >> write_ms;
        cout << "Номер медленного узла (0 - нет): ";
        cin >> slow_node;
        if (slow_node > 0) {
            cout << "Во сколько раз он считает медленнее: ";
            cin >> slow_factor;
        }
        break;
    default: break;
    }

    if (read_ms < 0) read_ms = 0;
    if (compute_ms < 0) compute_ms = 0;
    if (write_ms < 0) write_ms = 0;
    if (slow_factor < 1) slow_factor = 1;

    int verbose = 1;
    cout << "Выводить подробный журнал работы? (1 - да, 0 - нет): ";
    cin >> verbose;

    Shared sh;
    sh.verbose = (verbose != 0);

    // Открытие файлов
    sh.input.open("input.txt");
    sh.output.open("output.txt");

    if (!sh.input.is_open()) {
        cout << "Ошибка открытия input.txt\n";
        return 1;
    }

    if (!sh.output.is_open()) {
        cout << "Ошибка открытия output.txt\n";
        return 1;
    }

    // Создание узлов
    vector<unique_ptr<Node>> nodes;

    for (int i = 0; i < N; i++) {
        nodes.push_back(make_unique<Node>());
        nodes[i]->id = i + 1;
        nodes[i]->read_delay_ms = read_ms;
        nodes[i]->write_delay_ms = write_ms;
        nodes[i]->compute_delay_ms = compute_ms;
        if (i + 1 == slow_node)
            nodes[i]->compute_delay_ms = compute_ms * slow_factor;
    }

    cout << "\nЗапуск: узлов " << N << ", задержки (чтение/вычисление/запись): "
        << read_ms << "/" << compute_ms << "/" << write_ms << " мс";
    if (slow_node > 0 && slow_node <= N)
        cout << ", узел " << slow_node << ": вычисление дольше в " << slow_factor << " р.";
    cout << "\n\n";

    // Создание потоков
    vector<thread> readers;
    vector<thread> writers;

    sh.start = steady_clock::now();

    // Запуск потоков-писателей
    for (int i = 0; i < N; i++)
        writers.emplace_back(writer, ref(*nodes[i]), ref(sh));

    // Запуск потоков-читателей
    for (int i = 0; i < N; i++)
        readers.emplace_back(reader, ref(*nodes[i]), ref(sh));

    // Ожидание завершения потоков-читателей
    for (auto& t : readers)
        t.join();

    // Ожидание завершения потоков-писателей
    for (auto& t : writers)
        t.join();

    double total_time = elapsed(sh);

    // Закрытие файлов
    sh.input.close();
    sh.output.close();

    // Статистика
    cout << "\nСтатистика\n";
    cout << "Общее время работы: " << fixed << setprecision(3)
        << total_time << " c\n";
    cout << "Записано результатов: " << sh.total.load() << "\n";
    for (auto& n : nodes) {
        cout << "Узел " << n->id << ": обработано чисел " << n->processed
            << ", макс. длина очереди " << n->max_queue << "\n";
    }

    // Содержимое файла результатов
    cout << "\nФайл output.txt\n";
    ifstream result_file("output.txt");
    string line;
    while (getline(result_file, line))
        cout << line << "\n";

    return 0;
}