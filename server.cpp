#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <iostream>

#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <thread>
#include "ThreadPool.h"

// 소켓을 넌블로킹 모드로 설정하는 유틸리티 함수
void set_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0)
    {
        std::cerr << "fcntl(F_GETFL) 실패" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        std::cerr << "fcntl(F_SETFL) 실패" << std::endl;
        exit(EXIT_FAILURE);
    }
}

// 메인 서버 클래스
class EpollServer
{
public:
    EpollServer(int port, size_t threadPoolSize) : port(port), threadPool(threadPoolSize)
    {
        // 서버 소켓 생성
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1)
        {
            std::cerr << "socket 실패" << std::endl;
            exit(EXIT_FAILURE);
        }

        // 소켓을 넌블로킹으로 설정
        set_non_blocking(server_fd);

        // 지정된 포트에 바인드
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            std::cerr << "bind 실패" << std::endl;
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // 리스닝 시작
        if (listen(server_fd, SOMAXCONN) < 0)
        {
            std::cerr << "listen 실패" << std::endl;
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // epoll 인스턴스 생성
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1)
        {
            std::cerr << "epoll_create1 실패" << std::endl;
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // 서버 소켓을 epoll 이벤트 루프에 추가
        struct epoll_event event;
        event.data.fd = server_fd;
        event.events = EPOLLIN | EPOLLET; // 읽기 작업, 엣지 트리거 모드
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
        {
            std::cerr << "epoll_ctl 실패" << std::endl;
            close(server_fd);
            exit(EXIT_FAILURE);
        }
    }

    // 서버 메인 루프
    void run()
    {
        while (true)
        {
            epoll_event events[MAX_EVENTS];
            int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

            for (int i = 0; i < n; i++)
            {
                if (events[i].data.fd == server_fd)
                {
                    // 새로운 연결 처리
                    // accept_connections();
                    // 새로운 연결 처리를 스레드풀에 전달
                    threadPool.enqueue([this]
                                       { accept_connections(); });
                }
                else
                {
                    // 클라이언트의 읽기/쓰기 작업 처리
                    // handle_client(events[i].data.fd, client_names, client_mutex);
                    // 클라이언트의 읽기 작업 처리를 스레드풀에 전달
                    int fd = events[i].data.fd;
                    threadPool.enqueue([this, fd]
                                       { handle_client(fd); });
                }
            }
        }
    }

private:
    int server_fd, epoll_fd;
    int port;
    static const int MAX_EVENTS = 10;
    ThreadPool threadPool;

    std::unordered_map<int, std::string> client_names; // 클라이언트 이름 저장 해시맵
    std::mutex client_mutex;                           // 클라이언트 목록에 대한 동시 접근 관리를 위한 뮤텍스

    // 새로운 연결 수락
    void accept_connections()
    {
        while (true)
        {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
            if (client_fd < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 모든 들어오는 연결을 처리함
                    break;
                }
                else
                {
                    std::cerr << "accept 실패" << std::endl;
                    continue;
                }
            }

            // 클라이언트 소켓을 넌블로킹으로 설정
            set_non_blocking(client_fd);

            // 클라이언트 소켓을 epoll 인스턴스에 추가
            struct epoll_event event;
            event.data.fd = client_fd;
            event.events = EPOLLIN | EPOLLET; // 엣지 트리거 읽기
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0)
            {
                std::cerr << "epoll_ctl ADD client_fd 실패" << std::endl;
                close(client_fd);
            }
            else
            {
                std::lock_guard<std::mutex> lock(client_mutex);
                client_names[client_fd] = ""; // 초기 이름을 비워 둡니다.
                // 클라이언트에이름 입력 요청 메시지
                std::string welcome_msg = "서버에 연결되었습니다! 이름을 입력해 주세요: ";
                send(client_fd, welcome_msg.c_str(), welcome_msg.length(), 0);
            }
        }
    }

    std::string get_current_time()
    {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);

        char time_str[20]; // 충분한 크기의 문자열 버퍼 생성
        // 시간과 분만 표시하고, 시간을 초록색으로 표시
        std::strftime(time_str, sizeof(time_str), "\033[32m%H:%M\033[0m", std::localtime(&now_time_t));

        return time_str;
    }

    bool is_name_duplicate(const std::string &name, const std::unordered_map<int, std::string> &client_names)
    {
        for (const auto &client : client_names)
        {
            if (client.second == name)
            {
                return true;
            }
        }
        return false;
    }

    void handle_client(int client_fd)
    {
        char buffer[1024];
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        // 클라이언트 목록에 대한 동시 접근 방지
        std::lock_guard<std::mutex> lock(client_mutex);

        if (bytes_read <= 0)
        {
            // 클라이언트 연결이 끊어졌거나 에러 발생
            close(client_fd);
            client_names.erase(client_fd); // 클라이언트 목록에서 제거
        }
        else
        {
            buffer[bytes_read] = '\0'; // null-terminate the string

            std::string message(buffer);
            std::string current_time = get_current_time();

            // 클라이언트 이름 설정
            if (client_names[client_fd].empty())
            {
                if (is_name_duplicate(message, client_names))
                {
                    std::string error_message = "이미 사용 중인 이름입니다. 다른 이름을 선택해주세요.";
                    send(client_fd, error_message.c_str(), error_message.size(), 0);
                }
                else
                {
                    client_names[client_fd] = message;
                    std::string welcome_message = "환영합니다, " + message + "!";
                    send(client_fd, welcome_message.c_str(), welcome_message.size(), 0);

                    // 새로운 클라이언트가 입장했다는 메시지를 모든 클라이언트에게 전송
                    std::string entry_message = message + "님이 입장했습니다.";
                    for (const auto &client : client_names)
                    {
                        if (client_fd != client.first)
                        { // 방금 입장한 클라이언트 제외
                            send(client.first, entry_message.c_str(), entry_message.size(), 0);
                        }
                    }
                }
            }
            else if (message == "/exit")
            {
                // '/exit' 명령 처리 - 클라이언트 연결 종료
                std::string goodbye_message = client_names[client_fd] + "님이 채팅방을 떠났습니다.";
                for (const auto &client : client_names)
                {
                    if (client_fd != client.first)
                    { // 나가는 클라이언트 제외
                        send(client.first, goodbye_message.c_str(), goodbye_message.size(), 0);
                    }
                }

                close(client_fd);
                client_names.erase(client_fd);
            }
            else if (message == "/list")
            {
                // '/list' 명령 처리 - 모든 클라이언트의 이름 나열
                std::string list_of_names = "Room에 있는 사람들:\n";
                for (const auto &client : client_names)
                {
                    if (!client.second.empty())
                    { // 이름이 설정된 클라이언트만 포함
                        list_of_names += client.second + "\n";
                    }
                }
                send(client_fd, list_of_names.c_str(), list_of_names.size(), 0);
            }
            else if (message.rfind("/whisper", 0) == 0)
            {
                // 귓속말 명령어 처리
                size_t spacePos1 = message.find(' ', 0);
                size_t spacePos2 = message.find(' ', spacePos1 + 1);
                if (spacePos1 != std::string::npos && spacePos2 != std::string::npos)
                {
                    std::string recipient_name = message.substr(spacePos1 + 1, spacePos2 - spacePos1 - 1);
                    std::string whisper_message = message.substr(spacePos2 + 1);
                    bool found = false;

                    for (const auto &client : client_names)
                    {
                        if (client.second == recipient_name)
                        {
                            found = true;
                            std::string full_whisper_message = "귓속말 [" + current_time + "] (나 -> " + recipient_name + "): " + whisper_message;
                            send(client_fd, full_whisper_message.c_str(), full_whisper_message.size(), 0); // 귓속말을 보낸 사람에게도

                            // 귓속말을 받는 사람에게 메시지 보냄
                            full_whisper_message = "귓속말 [" + current_time + "] (" + client_names[client_fd] + " -> 나): " + whisper_message;
                            send(client.first, full_whisper_message.c_str(), full_whisper_message.size(), 0);
                            break; // 수신자를 찾았으므로 루프 종료
                        }
                    }

                    if (!found)
                    {
                        // 수신자를 찾지 못한 경우, 에러 메시지를 보냄
                        std::string error_message = recipient_name + "님을 찾을 수 없습니다.";
                        send(client_fd, error_message.c_str(), error_message.size(), 0);
                    }
                }
            }
            else if (message.rfind("/name", 0) == 0)
            {
                // '/name' 명령 처리: 사용자 이름 변경
                std::string new_name = message.substr(message.find(' ') + 1);
                if (is_name_duplicate(new_name, client_names))
                {
                    // 이미 사용 중인 이름인 경우 에러 메시지를 보냄
                    std::string error_message = "이미 사용 중인 이름입니다. 다른 이름을 선택해주세요.";
                    send(client_fd, error_message.c_str(), error_message.size(), 0);
                }
                else
                {
                    // 이름 변경 성공
                    std::string old_name = client_names[client_fd];
                    client_names[client_fd] = new_name;
                    std::string success_message = old_name + "님이 이름을 " + new_name + "(으)로 변경하셨습니다.";
                    for (const auto &client : client_names)
                    {
                        send(client.first, success_message.c_str(), success_message.size(), 0);
                    }
                }
            }
            else
            {
                // 일반 메시지 처리: 모든 클라이언트에게 메시지 전송
                std::string broadcast_message = client_names[client_fd] + " [" + current_time + "]: " + message;
                for (const auto &client : client_names)
                {
                    // if (client_fd != client.first) { // 메시지를 보낸 클라이언트 제외
                    send(client.first, broadcast_message.c_str(), broadcast_message.size(), 0);
                    //}
                }
            }
        }
    }

public:
    // 서버 중지 인터페이스
    void stop()
    {
        close(server_fd);
    }

    // 리소스 정리를 위한 소멸자
    ~EpollServer()
    {
        stop();
    }
};

int main()
{
    int port = 8080;
    size_t threadPoolSize = 4; // 스레드풀 크기를 원하는 값으로 설정
    EpollServer server(port, threadPoolSize);
    server.run();
    return 0;
}
