#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <mutex>

std::mutex cout_mutex;

// 서버로부터 메시지를 수신하는 함수
void receive_messages(int sock) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            std::cerr << "서버로부터 연결이 끊어졌습니다." << std::endl;
            break;
        } else {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << buffer << std::endl;
        }
    }
}

int main() {
    const char* server_ip = "127.0.0.1"; // 서버 IP 주소
    const int server_port = 8080;        // 서버 포트 번호

    // 서버 주소 구조체 설정
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    // 소켓 생성
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패" << std::endl;
        return 1;
    }

    // 서버에 연결
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "서버에 연결 실패" << std::endl;
        return 1;
    }

    // 서버로부터의 메시지 수신을 위한 별도의 스레드 시작
    std::thread receive_thread(receive_messages, sock);

    // 메시지 전송 루프
    std::string message;
    while (true) {
        std::getline(std::cin, message);

        // 서버로 메시지 전송
        if (send(sock, message.c_str(), message.size(), 0) < 0) {
            std::cerr << "메시지 전송 실패" << std::endl;
            break;
        }
    }

    // 소켓 닫기 및 프로그램 종료
    close(sock);
    receive_thread.join(); // 수신 스레드가 끝날 때까지 기다림
    return 0;
}
