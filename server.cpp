#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <ctime>

struct Player {
    std::string nickname;
    sockaddr_in addr;
    socklen_t addr_len;
    std::string choice;
    time_t last_heartbeat;
    bool alive;
};

struct Session {
    Player player1, player2;
    int score[2];
    int rounds;
    bool active;

    Session(Player p1, Player p2) : player1(p1), player2(p2), rounds(0), active(true) {
        score[0] = 0;
        score[1] = 0;
        player1.last_heartbeat = time(nullptr);
        player2.last_heartbeat = time(nullptr);
        player1.alive = true;
        player2.alive = true;
    }
};

std::unordered_map<std::string, Player> players;
std::vector<Session> sessions;
int server_socket;

void send_to_player(const Player& player, const std::string& message) {
    sendto(server_socket, message.c_str(), message.length(), 0, 
           (struct sockaddr*)&player.addr, player.addr_len);
}

void handle_exit(const Player& player) {
    std::cout << "Player " << player.nickname << " from IP " 
              << inet_ntoa(player.addr.sin_addr) << " disconnected" << std::endl;
    players.erase(player.nickname);
    
    for (auto& session : sessions) {
        if (session.active && 
            (session.player1.nickname == player.nickname || 
             session.player2.nickname == player.nickname)) {
            session.active = false;
            Player& opponent = (session.player1.nickname == player.nickname) 
                             ? session.player2 : session.player1;
            if (opponent.alive) {
                send_to_player(opponent, "OPPONENT_LEFT");
            }
        }
    }
}

void handle_new_connection(sockaddr_in client_addr, socklen_t client_len, const std::string& nick) {
    if (players.count(nick) > 0 || nick.empty()) {
        Player temp_player = {"", client_addr, client_len, "", time(nullptr), true};
        send_to_player(temp_player, "NICK_TAKEN");
        return;
    }
    
    Player new_player = {nick, client_addr, client_len, "", time(nullptr), true};
    players[nick] = new_player;
    std::cout << "New player: " << nick << " connected from " 
              << inet_ntoa(client_addr.sin_addr) << std::endl;
    
    if (players.size() >= 2) {
        auto it = players.begin();
        Player p1 = it->second;
        players.erase(it);
        it = players.begin();
        Player p2 = it->second;
        players.erase(it);
        sessions.push_back(Session(p1, p2));
        send_to_player(p1, "SESSION " + p2.nickname + " 0:0");
        send_to_player(p2, "SESSION " + p1.nickname + " 0:0");
    } 
}

void process_choice(Session& session) {
    if (!session.active || session.player1.choice.empty() || session.player2.choice.empty()) 
        return;

    std::string result;
    if (session.player1.choice == session.player2.choice) {
        result = "DRAW";
    } else if ((session.player1.choice == "ROCK" && session.player2.choice == "SCISSORS") ||
               (session.player1.choice == "SCISSORS" && session.player2.choice == "PAPER") ||
               (session.player1.choice == "PAPER" && session.player2.choice == "ROCK")) {
        result = "P1_WINS";
        session.score[0]++;
    } else {
        result = "P2_WINS";
        session.score[1]++;
    }

    session.rounds++;

    std::string score_msg1 = "SCORE " + std::to_string(session.score[0]) + ":" + 
                           std::to_string(session.score[1]) + " " + result;
    std::string score_msg2 = "SCORE " + std::to_string(session.score[1]) + ":" + 
                           std::to_string(session.score[0]) + " " + result;

    if (session.score[0] >= 5) {
        send_to_player(session.player1, "WIN");
        send_to_player(session.player2, "LOSE");
        session.active = false;
    } else if (session.score[1] >= 5) {
        send_to_player(session.player1, "LOSE");
        send_to_player(session.player2, "WIN");
        session.active = false;
    } else {
        send_to_player(session.player1, score_msg1);
        send_to_player(session.player2, score_msg2);
    }

    session.player1.choice.clear();
    session.player2.choice.clear();
}

void check_heartbeats() {
    time_t now = time(nullptr);
    for (auto& session : sessions) {
        if (!session.active) continue;

        // Отправляем heartbeat-запрос каждые 5 секунд
        if (now - session.player1.last_heartbeat >= 5) {
            send_to_player(session.player1, "HEARTBEAT");
        }
        if (now - session.player2.last_heartbeat >= 5) {
            send_to_player(session.player2, "HEARTBEAT");
        }

        // Если ответа нет больше 10 секунд, считаем игрока отключенным
        if (now - session.player1.last_heartbeat > 10 && session.player1.alive) {
            session.player1.alive = false;
            if (session.player2.alive) {
                send_to_player(session.player2, "OPPONENT_LEFT");
            }
            session.active = false;
        }
        if (now - session.player2.last_heartbeat > 10 && session.player2.alive) {
            session.player2.alive = false;
            if (session.player1.alive) {
                send_to_player(session.player1, "OPPONENT_LEFT");
            }
            session.active = false;
        }
    }
}

int main() {
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    sockaddr_in server_addr;
    int server_port = 12345;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }
    
    char buffer[1024];
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    std::cout << "Server started on port " << server_port << std::endl;
    
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1; // Проверяем каждую секунду
        tv.tv_usec = 0;

        if (select(server_socket + 1, &readfds, NULL, NULL, &tv) >= 0) {
            if (FD_ISSET(server_socket, &readfds)) {
                memset(buffer, 0, sizeof(buffer));
                client_len = sizeof(client_addr);
                int bytes = recvfrom(server_socket, buffer, sizeof(buffer) - 1, 0, 
                                   (struct sockaddr*)&client_addr, &client_len);
                if (bytes < 0) continue;
                
                std::string message(buffer);
                
                if (message == "EXIT") {
                    for (auto it = players.begin(); it != players.end(); ++it) {
                        if (it->second.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                            it->second.addr.sin_port == client_addr.sin_port) {
                            handle_exit(it->second);
                            break;
                        }
                    }
                } else if (message == "HEARTBEAT_RESPONSE") {
                    for (auto& session : sessions) {
                        if (session.active) {
                            if (session.player1.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                                session.player1.addr.sin_port == client_addr.sin_port) {
                                session.player1.last_heartbeat = time(nullptr);
                            } else if (session.player2.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                                     session.player2.addr.sin_port == client_addr.sin_port) {
                                session.player2.last_heartbeat = time(nullptr);
                            }
                        }
                    }
                } else if (message == "ROCK" || message == "SCISSORS" || message == "PAPER") {
                    for (auto& session : sessions) {
                        if (!session.active) continue;
                        if (session.player1.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                            session.player1.addr.sin_port == client_addr.sin_port) {
                            session.player1.choice = message;
                        } else if (session.player2.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                                 session.player2.addr.sin_port == client_addr.sin_port) {
                            session.player2.choice = message;
                        }
                        process_choice(session);
                    }
                } else {
                    handle_new_connection(client_addr, client_len, message);
                }
            }
        }
        
        check_heartbeats(); // Проверяем подключение игроков
        sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
            [](const Session& s) { return !s.active; }), sessions.end());
    }
    
    close(server_socket);
    return 0;
}