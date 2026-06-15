#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

#define PORT 8080
#define BUFFER_SIZE 4096

// Función para enviar una línea de texto por el socket
bool send_line(int sock, const std::string& line) {
    std::string data = line + "\n";
    return send(sock, data.c_str(), data.size(), 0) != -1;
}

// Función para recibir una línea de texto (hasta '\n')
std::string recv_line(int sock) {
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) > 0) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

// Manejar subida de archivo
bool handle_upload(int client_sock, const std::string& filename) {
    // Recibir tamaño del archivo
    std::string size_str = recv_line(client_sock);
    long long file_size = std::stoll(size_str);
    if (file_size < 0) {
        std::cerr << "Tamaño inválido" << std::endl;
        return false;
    }

    // Crear directorio si no existe
    std::filesystem::create_directories("server_files");

    std::string filepath = "server_files/" + filename;
    std::ofstream outfile(filepath, std::ios::binary);
    if (!outfile) {
        send_line(client_sock, "ERROR No se pudo crear el archivo local");
        return false;
    }

    // Recibir datos del archivo
    char buffer[BUFFER_SIZE];
    long long received = 0;
    while (received < file_size) {
        long long to_receive = std::min((long long)BUFFER_SIZE, file_size - received);
        int bytes = recv(client_sock, buffer, to_receive, 0);
        if (bytes <= 0) {
            std::cerr << "Error recibiendo datos" << std::endl;
            return false;
        }
        outfile.write(buffer, bytes);
        received += bytes;
    }
    outfile.close();

    send_line(client_sock, "OK Archivo subido correctamente");
    std::cout << "Archivo " << filename << " recibido y guardado" << std::endl;
    return true;
}

// Manejar descarga de archivo
bool handle_download(int client_sock, const std::string& filename) {
    std::string filepath = "server_files/" + filename;
    std::ifstream infile(filepath, std::ios::binary | std::ios::ate);
    if (!infile) {
        send_line(client_sock, "ERROR Archivo no existe en el servidor");
        return false;
    }

    long long file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    // Enviar tamaño
    send_line(client_sock, std::to_string(file_size));

    // Enviar datos
    char buffer[BUFFER_SIZE];
    long long sent = 0;
    while (sent < file_size) {
        long long to_send = std::min((long long)BUFFER_SIZE, file_size - sent);
        infile.read(buffer, to_send);
        int bytes = send(client_sock, buffer, to_send, 0);
        if (bytes <= 0) {
            std::cerr << "Error enviando datos" << std::endl;
            return false;
        }
        sent += bytes;
    }
    infile.close();

    // Esperar confirmación del cliente (opcional, aquí simplemente leemos OK)
    std::string confirm = recv_line(client_sock);
    std::cout << "Archivo " << filename << " enviado correctamente" << std::endl;
    return true;
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Crear socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket falló");
        exit(EXIT_FAILURE);
    }

    // Configurar opciones para reutilizar puerto
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind falló");
        exit(EXIT_FAILURE);
    }

    // Escuchar
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Servidor escuchando en el puerto " << PORT << std::endl;

    while (true) {
        // Aceptar conexión
        if ((client_sock = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        std::cout << "Cliente conectado" << std::endl;

        // Recibir comando
        std::string command_line = recv_line(client_sock);
        std::istringstream iss(command_line);
        std::string command, filename;
        iss >> command >> filename;

        bool success = false;
        if (command == "UPLOAD") {
            success = handle_upload(client_sock, filename);
        } else if (command == "DOWNLOAD") {
            success = handle_download(client_sock, filename);
        } else {
            send_line(client_sock, "ERROR Comando desconocido");
        }

        close(client_sock);
        std::cout << "Cliente desconectado" << std::endl;
    }

    close(server_fd);
    return 0;
}