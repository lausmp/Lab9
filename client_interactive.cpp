#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

#define BUFFER_SIZE 4096

bool send_line(int sock, const std::string& line) {
    std::string data = line + "\n";
    return send(sock, data.c_str(), data.size(), 0) != -1;
}

std::string recv_line(int sock) {
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) > 0) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

// CORREGIDO: No espera un OK después del comando UPLOAD
bool upload_file(int sock, const std::string& local_path, const std::string& remote_name) {
    std::ifstream infile(local_path, std::ios::binary | std::ios::ate);
    if (!infile) {
        std::cerr << "Error: no se pudo abrir el archivo local " << local_path << std::endl;
        return false;
    }
    long long file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    // Enviar comando UPLOAD (sin esperar respuesta intermedia)
    if (!send_line(sock, "UPLOAD " + remote_name)) {
        std::cerr << "Error enviando comando" << std::endl;
        return false;
    }

    // Enviar el tamaño del archivo
    if (!send_line(sock, std::to_string(file_size))) {
        std::cerr << "Error enviando tamaño" << std::endl;
        return false;
    }

    // Enviar el contenido binario
    char buffer[BUFFER_SIZE];
    long long sent = 0;
    while (sent < file_size) {
        long long to_send = std::min((long long)BUFFER_SIZE, file_size - sent);
        infile.read(buffer, to_send);
        int bytes = send(sock, buffer, to_send, 0);
        if (bytes <= 0) {
            std::cerr << "Error enviando datos" << std::endl;
            return false;
        }
        sent += bytes;
    }
    infile.close();

    // Recibir confirmación final del servidor (OK o ERROR)
    std::string response = recv_line(sock);
    std::cout << "Servidor: " << response << std::endl;
    return (response.find("OK") != std::string::npos);
}

// CORREGIDO: Lee directamente la línea que puede ser el tamaño o un error
bool download_file(int sock, const std::string& remote_name, const std::string& local_path) {
    if (!send_line(sock, "DOWNLOAD " + remote_name)) {
        std::cerr << "Error enviando comando" << std::endl;
        return false;
    }

    // El servidor responde con el tamaño del archivo o un mensaje de error
    std::string response = recv_line(sock);
    if (response.find("ERROR") != std::string::npos) {
        std::cerr << "Servidor: " << response << std::endl;
        return false;
    }

    long long file_size;
    try {
        file_size = std::stoll(response);
    } catch (...) {
        std::cerr << "Respuesta inválida del servidor" << std::endl;
        return false;
    }

    if (file_size < 0) {
        std::cerr << "Tamaño inválido" << std::endl;
        return false;
    }

    // Crear carpeta client_files si no existe
    std::filesystem::create_directories("client_files");
    std::string full_path = "client_files/" + local_path;
    std::ofstream outfile(full_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error creando archivo local" << std::endl;
        return false;
    }

    char buffer[BUFFER_SIZE];
    long long received = 0;
    while (received < file_size) {
        long long to_receive = std::min((long long)BUFFER_SIZE, file_size - received);
        int bytes = recv(sock, buffer, to_receive, 0);
        if (bytes <= 0) {
            std::cerr << "Error recibiendo datos" << std::endl;
            return false;
        }
        outfile.write(buffer, bytes);
        received += bytes;
    }
    outfile.close();

    // Enviar confirmación al servidor (opcional, el servidor no espera nada, pero por si acaso)
    send_line(sock, "OK");
    std::cout << "Archivo descargado correctamente en " << full_path << std::endl;
    return true;
}

// Función para conectar al servidor
int conectar(const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int main() {
    std::string server_ip;
    int port;

    std::cout << "=== CLIENTE INTERACTIVO (CORREGIDO) ===" << std::endl;
    std::cout << "IP del servidor (ej. 127.0.0.1): ";
    std::cin >> server_ip;
    std::cout << "Puerto (default 8080): ";
    std::cin >> port;
    if (std::cin.fail()) { port = 8080; std::cin.clear(); }
    std::cin.ignore();

    int opcion;
    do {
        std::cout << "\n--- MENÚ ---\n1. Subir archivo\n2. Descargar archivo\n3. Salir\nOpción: ";
        std::cin >> opcion;
        std::cin.ignore();

        if (opcion == 1) {
            std::string local_path, remote_name;
            std::cout << "Ruta archivo LOCAL (ej. client_files/prueba_subida.txt): ";
            std::getline(std::cin, local_path);
            std::cout << "Nombre en SERVIDOR (ej. copia.txt): ";
            std::getline(std::cin, remote_name);
            int sock = conectar(server_ip, port);
            if (sock == -1) {
                std::cerr << "No se pudo conectar al servidor" << std::endl;
                continue;
            }
            upload_file(sock, local_path, remote_name);
            close(sock);
        }
        else if (opcion == 2) {
            std::string remote_name, local_path;
            std::cout << "Nombre archivo en SERVIDOR: ";
            std::getline(std::cin, remote_name);
            std::cout << "Ruta para guardar LOCAL (ej. bajado.txt): ";
            std::getline(std::cin, local_path);
            int sock = conectar(server_ip, port);
            if (sock == -1) {
                std::cerr << "No se pudo conectar al servidor" << std::endl;
                continue;
            }
            download_file(sock, remote_name, local_path);
            close(sock);
        }
    } while (opcion != 3);

    return 0;
}