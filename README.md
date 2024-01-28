# 💬 Linux Chat
### 📌 소개
> ####  **💬 TCP/IP 기반의 소켓 통신을 사용하는 리눅스 epoll 기반의 비동기 멀티스레드 채팅 서버입니다.**

- epoll 메커니즘이 해당 이벤트를 처리할 준비가 된 소켓을 식별합니다. 준비된 소켓의 이벤트는 스레드풀로 전달되어 처리됩니다. 
- 스레드풀은 여러 개의 스레드를 포함하고 있으며, 각 스레드는 독립적으로 클라이언트의 요청을 처리합니다.

### 📌 실행 영상
https://youtu.be/si3rGMB3ydk

### 📌 실행 방법
1. 컴파일하기
```
g++ -o server server.cpp -lpthread -std=c++11

g++ -o client client.cpp -lpthread -std=c++11
```

2. 실행하기 (서버가 실행된 상태에서 클라이언트가 입장해야 합니다)
```
./server

./client
```
### 📌 개발 환경

C++, CentOS 7, g++, VMware, Visual Studio Code

### 📌 구현 기능
- 클라이언트는 서버에 연결하여 실시간으로 메시지를 주고받을 수 있습니다.
- 서버는 스레드 풀을 사용하여 각 클라이언트의 요청을 병렬로 처리합니다.
- 채팅 기능
  - 입장 시 이름 작성 (중복 체크)
  - 메시지 송수신 시각
  - 전체 메시지 
  - 귓속말 메시지 (명령어 : `/whisper ’귓속말 상대’ ‘대화내용’`)
  - 채팅방에 있는 전원 이름 조회 (명령어 : `/list`)
  - 이름 바꾸기 (명령어 : `/name ‘바꾸고 싶은 이름‘`)

### 📌 서버 코드에서의 스레드풀 사용 방식

- 서버 초기화 시 `ThreadPool` 객체를 생성하고, 이때 스레드풀의 크기를 지정합니다. 이 크기는 서버가 동시에 처리할 수 있는 최대 요청 수를 결정합니다.
- 서버의 메인 루프에서 `epoll_wait` 를 사용하여 이벤트를 대기합니다.
- 이벤트가 발생하면, 연결 요청 처리(`accept_connections`) 또는 클라이언트 요청 처리(`handle_client`) 작업을 스레드풀의 큐(`tasks`)에 추가합니다.
- 각 요청은 작업 큐에 추가되고, 사용 가능한 스레드에 의해 순차적으로 처리됩니다. 스레드풀에서 대기 중인 스레드가 이 작업을 가져가 처리합니다. 
- 처리가 완료되면 스레드는 다음 작업을 위해 다시 대기 상태로 돌아갑니다.

### 📌 고민한 점
> #### **💬 _스레드를 어떻게 할당할까?_**

초기 서버 설계에서는 각 클라이언트 연결에 스레드 하나를 고정으로 할당하는 구조를 고려했습니다. 이 구조에서 스레드는 클라이언트가 접속 종료를 할 때까지 전담하여, 입력이 없는 동안에도 다른 작업에 재할당될 수 없었습니다. 이는 비효율적인 자원 사용으로 이어졌고, 클라이언트가 많을수록 서버의 스레드 자원이 소진되는 문제가 있습니다.

이러한 단점은 극복하되, 확장성과 병렬 처리 능력을 갖추기 위해서 저는 **_작업 기반 스레드 할당_**  모델을 선택했습니다. 이러한 접근에서는 스레드가 특정 클라이언트에 고정되는 것이 아니라, 요청된 작업(=패킷)을 처리할 때만 할당됩니다. 처리가 완료되면 스레드는 즉시 스레드 풀로 반환되어 다음 작업을 기다립니다. 이 모델은 스레드와 클라이언트 연결을 분리함으로써, 서버의 확장성과 자원 활용도가 높도록 개선했습니다. 

결과적으로, 스레드 수가 클라이언트 수와 독립적이 되어, 이론상 무한에 가까운 클라이언트가 서버에 접속할 수 있도록 하였습니다. 실제 테스트를 통해, 쿼터 코어 환경에서 4개의 스레드를 만들었을 때 동시에 50개의 클라이언트가 문제없이 접속하고 채팅하는 것을 확인했습니다.

> #### **💬 _스레드 간의 안전한 작업 큐 접근을 구현하려면?_**
서버는 스레드 풀을 사용하여 여러 클라이언트 연결을 동시에 관리하고, 클라이언트 요청을 병렬로 처리하도록 설계했습니다. 경쟁 상태를 방지하기 위해 스레드 풀 내의 작업 큐에 대한 스레드 안전한 접근을 보장합니다. 각 스레드는 뮤텍스를 통해 큐에 대한 접근을 동기화하고, 조건 변수를 사용하여 작업이 있을 때까지 대기합니다. 스레드 풀에 있는 작업 큐에 작업을 추가하고, 각 스레드가 조건 변수를 사용하여 대기하다가 작업을 가져와 실행하는 방식으로 사용 가능한 스레드에 작업을 분배했습니다.

> #### **💬 _로드밸런싱을 구현하려면?_**
로드 밸런싱을 구현하기 위해서는 서버가 작업을 스레드 풀에 분배할 때 각 스레드의 현재 작업량을 고려해야 합니다. 로드 밸런싱을 위한 기본적인 접근 방식 중 하나는 작업 큐 대신 스레드별로 개별 작업 큐를 두는 것이라고 알려져 있습니다. 이렇게 하면 각 스레드가 자신의 큐에 있는 작업만 처리하게 되며, 새 작업을 스케줄링할 때 가장 적은 작업을 가진 스레드의 큐에 작업을 추가할 수 있습니다.


### 📌 설계 다이어그램
![image](https://github.com/strurao/LinuxChat/assets/126440235/270ed830-132b-473a-a62f-5311a895342a)

### 📌 실행 캡쳐 화면
<img width="1057" alt="스크린샷 2024-01-27 오후 3 13 55" src="https://github.com/strurao/LinuxChat/assets/126440235/be751a52-f14d-4d0f-ba9e-5d4f2804f078">
