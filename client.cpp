#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

#define PORT 8080
#define BUFFER_SIZE 4096

// Función para enviar línea
bool send_line(int sock, const std::string& line) {
    std::string data = line + "\n";
    return send(sock, data.c_str(), data.size(), 0) != -1;
}

// Función para recibir línea
std::string recv_line(int sock) {
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) > 0) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

// Subir archivo al servidor
bool upload_file(int sock, const std::string& local_path, const std::string& remote_name) {
    // Abrir archivo local
    std::ifstream infile(local_path, std::ios::binary | std::ios::ate);
    if (!infile) {
        std::cerr << "Error: no se pudo abrir el archivo local " << local_path << std::endl;
        return false;
    }

    long long file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    // Enviar comando UPLOAD
    if (!send_line(sock, "UPLOAD " + remote_name)) {
        std::cerr << "Error enviando comando" << std::endl;
        return false;
    }

    // Esperar respuesta del servidor (debe ser OK para continuar)
    std::string response = recv_line(sock);
    if (response != "OK") {
        std::cerr << "Servidor rechazó la subida: " << response << std::endl;
        return false;
    }

    // Enviar tamaño
    send_line(sock, std::to_string(file_size));

    // Enviar datos
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

    // Recibir confirmación final
    response = recv_line(sock);
    std::cout << "Servidor: " << response << std::endl;
    return true;
}

// Descargar archivo del servidor
bool download_file(int sock, const std::string& remote_name, const std::string& local_path) {
    // Enviar comando DOWNLOAD
    if (!send_line(sock, "DOWNLOAD " + remote_name)) {
        std::cerr << "Error enviando comando" << std::endl;
        return false;
    }

    // Recibir respuesta (puede ser tamaño o mensaje de error)
    std::string response = recv_line(sock);
    if (response == "ERROR" || response.find("ERROR") != std::string::npos) {
        std::cerr << "Servidor: " << response << std::endl;
        return false;
    }

    long long file_size = std::stoll(response);
    if (file_size < 0) {
        std::cerr << "Tamaño inválido" << std::endl;
        return false;
    }

    // Crear directorio si no existe
    std::filesystem::create_directories("client_files");

    std::string full_path = "client_files/" + local_path;
    std::ofstream outfile(full_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error creando archivo local" << std::endl;
        return false;
    }

    // Recibir datos
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

    // Enviar confirmación
    send_line(sock, "OK");
    std::cout << "Archivo descargado correctamente en " << full_path << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: " << argv[0] << " <server_ip> [upload|download] <archivo_origen> [destino_remoto]" << std::endl;
        std::cerr << "Ejemplos:" << std::endl;
        std::cerr << "  Subir:   " << argv[0] << " 192.168.1.10 upload ./foto.jpg mi_foto.jpg" << std::endl;
        std::cerr << "  Descargar:" << argv[0] << " 192.168.1.10 download documento.txt ./recibido.txt" << std::endl;
        return 1;
    }

    const char* server_ip = argv[1];
    std::string operation = argv[2];

    int sock = 0;
    struct sockaddr_in serv_addr;

    // Crear socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket error");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("Dirección inválida");
        return 1;
    }

    // Conectar
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Conexión fallida");
        return 1;
    }

    bool success = false;
    if (operation == "upload") {
        if (argc < 5) {
            std::cerr << "Para upload: " << argv[0] << " <ip> upload <archivo_local> <nombre_remoto>" << std::endl;
            return 1;
        }
        std::string local_file = argv[3];
        std::string remote_name = argv[4];
        success = upload_file(sock, local_file, remote_name);
    } else if (operation == "download") {
        if (argc < 5) {
            std::cerr << "Para download: " << argv[0] << " <ip> download <nombre_remoto> <archivo_local>" << std::endl;
            return 1;
        }
        std::string remote_name = argv[3];
        std::string local_file = argv[4];
        success = download_file(sock, remote_name, local_file);
    } else {
        std::cerr << "Operación debe ser 'upload' o 'download'" << std::endl;
    }

    close(sock);
    return success ? 0 : 1;
}