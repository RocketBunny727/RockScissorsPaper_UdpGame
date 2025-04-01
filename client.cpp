#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstring>
#include <vector>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ncurses.h>

#define CONFIG_FILE "conf.txt"

struct GameState {
    std::string opponentNick;
    int myScore;
    int opScore;
    int selection;
    bool waiting;
    std::string opponent_ip;
    int opponent_port;
    std::vector<std::string> chat_messages;
    bool in_chat;           // Флаг, показывающий, что пользователь в чате
    std::string chat_input; // Текущий ввод сообщения в чате

    GameState() : opponentNick("Unknown"), myScore(0), opScore(0), selection(0), waiting(false), 
                  opponent_port(0), in_chat(false) {}
};

bool is_valid_ip(const std::string& ip) {
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
    if (result == 0) {
        std::cerr << "Invalid IP: '" << ip << "'" << std::endl;
        return false;
    } else if (result < 0) {
        std::cerr << "Error while checking IP: '" << ip << "'" << std::endl;
        return false;
    }
    return true;
}

bool is_valid_port(int port) {
    return port > 1023 && port <= 65535;
}

bool load_config(std::string& ip, int& port) {
    std::ifstream config(CONFIG_FILE);
    if (!config.is_open()) {
        std::cerr << "Unable to open configuration file: '" << CONFIG_FILE << "'" << std::endl;
        return false;
    }

    std::string line;
    bool ip_found = false, port_found = false;

    while (std::getline(config, line)) {
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, ':') && std::getline(iss, value)) {
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
            
            if (key == "IP") {
                ip = value;
                ip_found = is_valid_ip(ip);
                if (!ip_found) {
                    std::cerr << "ERROR: invalid server IP in configuration file(" << CONFIG_FILE << "): '" << ip << "'" << std::endl;
                }
            } 
            else if (key == "port") {
                try {
                    port = std::stoi(value);
                    port_found = is_valid_port(port);
                    if (!port_found) {
                        std::cerr << "ERROR: invalid server port in configuration file (" << CONFIG_FILE << "): '" << port << "'" << std::endl;
                    }
                } catch (...) {
                    std::cerr << "ERROR: incorrect server port format in configuration file (" << CONFIG_FILE << "): '" << value << "'" << std::endl;
                }
            }
        }
    }

    if (!ip_found || !port_found) {
        std::cerr << "ERROR: incorrect data in configuration file (" << CONFIG_FILE << "). Need to input IP and port manually." << std::endl;
    }

    return ip_found && port_found;
}

void show_game_info(const GameState& state) {
    std::string output = "Your opponent: " + state.opponentNick + "\nSCORE - " + 
                        std::to_string(state.myScore) + ":" + std::to_string(state.opScore);
    printw("%s\n", output.c_str());
    if (!state.chat_messages.empty()) {
        printw("Last chat message: %s\n", state.chat_messages.back().c_str());
    }
}

void show_menu(const GameState& state) {
    const char* options[3] = {"ROCK", "SCISSORS", "PAPER"};
    for (int i = 0; i < 3; i++) {
        if (i == state.selection) printw("> %s\n", options[i]);
        else printw("  %s\n", options[i]);
    }
    if (state.waiting) {
        printw("Waiting for the opponent's choice...\n");
    }
}

bool show_end_menu(int& selection) {
    const char* options[2] = {"Yes", "No"};
    for (int i = 0; i < 2; i++) {
        if (i == selection) printw("> %s\n", options[i]);
        else printw("  %s\n", options[i]);
    }
    return selection == 0;
}

void show_chat(GameState& state) { // Убрали chat_socket и nickname
    printw("Chat with %s\n", state.opponentNick.c_str());
    printw("----------------\n");

    for (const auto& msg : state.chat_messages) {
        printw("%s\n", msg.c_str());
    }
    printw("----------------\n");
    printw("Type message (or press ESC to exit): %s", state.chat_input.c_str());
}

bool init_game(int client_socket, sockaddr_in& server_addr, socklen_t server_len, 
              std::string& nick_with_port, GameState& state) {
    bool nick_accepted = false;
    
    while (!nick_accepted) {
        sendto(client_socket, nick_with_port.c_str(), nick_with_port.length(), 0, 
               (struct sockaddr*)&server_addr, server_len);
        
        clear();
        printw("\n\n\t\t\tWaiting for an opponent...\n");
        refresh();

        char buffer[1024];
        int bytes = recvfrom(client_socket, buffer, sizeof(buffer) - 1, 0, 
                           (struct sockaddr*)&server_addr, &server_len);
        
        if (bytes <= 0) {
            endwin();
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }

        buffer[bytes] = '\0';
        std::string message(buffer);
        
        if (message == "NICK_TAKEN") {
            endwin();
            std::cout << "Nickname taken or invalid. Please choose another: ";
            std::cin >> nick_with_port; // Здесь нужно будет переформировать nick_with_port вручную
            initscr();
            keypad(stdscr, TRUE);
            noecho();
            curs_set(0);
            timeout(100);
            continue;
        }
        
        if (message.rfind("SESSION", 0) == 0) {
            char opponent[256], ip[16];
            int port;
            sscanf(buffer, "SESSION %s %d:%d %s %d", opponent, &state.myScore, &state.opScore, ip, &port);
            state.opponentNick = opponent;
            state.opponent_ip = ip;
            state.opponent_port = port;
            nick_accepted = true;
            return true;
        }
        
        endwin();
        std::cerr << "Unexpected server response: " << message << std::endl;
        return false;
    }
    
    return true;
}

int main() {
    std::string server_ip;
    int server_port = 0;

    if (!load_config(server_ip, server_port)) {
        std::cout << "Enter server IP: ";
        std::cin >> server_ip;
        while (!is_valid_ip(server_ip)) {
            std::cout << "Invalid IP. Enter again: ";
            std::cin >> server_ip;
        }

        std::cout << "Enter server port: ";
        std::cin >> server_port;
        while (!is_valid_port(server_port)) {
            std::cout << "Invalid port. Enter again: ";
            std::cin >> server_port;
        }
    } else {
        std::cout << "Loaded from config: IP = " << server_ip << ", Port = " << server_port << std::endl;
    }

    int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    int chat_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (chat_socket < 0) {
        std::cerr << "Chat socket creation failed" << std::endl;
        return 1;
    }

    sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = 0;
    if (bind(chat_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        std::cerr << "Chat socket bind failed" << std::endl;
        return 1;
    }

    // Получаем порт, который был назначен системой
    socklen_t client_len = sizeof(client_addr);
    getsockname(chat_socket, (struct sockaddr*)&client_addr, &client_len);
    int chat_port = ntohs(client_addr.sin_port);

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    socklen_t server_len = sizeof(server_addr);
    std::string nickname;

    std::vector<std::string> reserved_words = {"EXIT", "HEARTBEAT_RESPONSE", "NICK_TAKEN", 
                                               "SESSION", "SCORE", "WIN", "LOSE", "OPPONENT_LEFT"};

    while (true) {
        std::cout << "Enter your nickname: ";
        std::cin >> nickname;

        std::string upper_nick = nickname;
        std::transform(upper_nick.begin(), upper_nick.end(), upper_nick.begin(), ::toupper);

        if (std::find(reserved_words.begin(), reserved_words.end(), upper_nick) != reserved_words.end()) {
            std::cout << "This nickname is reserved. Please choose another one." << std::endl;
            continue;
        }

        if (nickname.length() < 3 || nickname.length() > 20) {
            std::cout << "Nickname must be between 3 and 20 characters." << std::endl;
            continue;
        }

        break;
    }

    std::string nick_with_port = nickname + ":" + std::to_string(chat_port);

    initscr();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    timeout(100);
    
    GameState state;
    if (!init_game(client_socket, server_addr, server_len, nick_with_port, state)) {
        endwin();
        close(client_socket);
        close(chat_socket);
        return 1;
    }
    
    char buffer[1024];
    while (true) {
        clear();
        if (state.in_chat) {
            show_chat(state);
        } else {
            show_game_info(state);
            show_menu(state);
        }
        refresh();
        
        int key = getch();
        if (key == 'C' || key == 'c') {
            state.in_chat = !state.in_chat; // Переключаем режим чата
            if (!state.in_chat) state.chat_input.clear(); // Очищаем ввод при выходе
        } else if (state.in_chat) {
            if (key == 27) { // ESC
                state.in_chat = false;
                state.chat_input.clear();
            } else if (key == 10 && !state.chat_input.empty()) { // Enter
                std::string message = nickname + ": " + state.chat_input;
                state.chat_messages.push_back(message);

                sockaddr_in opponent_addr;
                memset(&opponent_addr, 0, sizeof(opponent_addr));
                opponent_addr.sin_family = AF_INET;
                opponent_addr.sin_port = htons(state.opponent_port);
                inet_pton(AF_INET, state.opponent_ip.c_str(), &opponent_addr.sin_addr);
                if (sendto(chat_socket, state.chat_input.c_str(), state.chat_input.length(), 0, 
                           (struct sockaddr*)&opponent_addr, sizeof(opponent_addr)) < 0) {
                    state.chat_messages.push_back("Failed to send message!");
                }
                state.chat_input.clear();
            } else if (key >= 32 && key <= 126) { // Печатные символы
                state.chat_input += static_cast<char>(key);
            }
        } else if (key == KEY_UP) {
            state.selection = (state.selection + 2) % 3;
        } else if (key == KEY_DOWN) {
            state.selection = (state.selection + 1) % 3;
        } else if (key == 10 && !state.waiting) {
            const char* choices[3] = {"ROCK", "SCISSORS", "PAPER"};
            sendto(client_socket, choices[state.selection], strlen(choices[state.selection]), 0, 
                   (struct sockaddr*)&server_addr, server_len);
            state.waiting = true;
        }
        
        int bytes = recvfrom(client_socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, 
                           (struct sockaddr*)&server_addr, &server_len);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            std::string message(buffer);
            
            if (message == "HEARTBEAT") {
                sendto(client_socket, "HEARTBEAT_RESPONSE", 18, 0, 
                       (struct sockaddr*)&server_addr, server_len);
            } else if (message == "WIN" || message == "LOSE" || message == "OPPONENT_LEFT") {
                clear();
                printw("%s!\nPlay again?\n", 
                       (message == "WIN" ? "YOU WIN" : 
                        message == "LOSE" ? "YOU LOSE" : "OPPONENT LEFT"));
                refresh();
                
                int end_selection = 0;
                bool choice_made = false;
                while (!choice_made) {
                    clear();
                    printw("%s!\nPlay again?\n", 
                           (message == "WIN" ? "YOU WIN" : 
                            message == "LOSE" ? "YOU LOSE" : "OPPONENT LEFT"));
                    show_end_menu(end_selection);
                    refresh();
                    
                    int end_key = getch();
                    if (end_key == KEY_UP || end_key == KEY_DOWN) {
                        end_selection = (end_selection + 1) % 2;
                    } else if (end_key == 10) {
                        choice_made = true;
                    }
                }
                
                if (show_end_menu(end_selection)) {
                    state = GameState();
                    if (!init_game(client_socket, server_addr, server_len, nick_with_port, state)) {
                        endwin();
                        close(client_socket);
                        close(chat_socket);
                        return 1;
                    }
                } else {
                    break;
                }
            } else if (message.rfind("SCORE", 0) == 0) {
                sscanf(buffer, "SCORE %d:%d", &state.myScore, &state.opScore);
                state.waiting = false;
            }
        }

        sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        bytes = recvfrom(chat_socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, 
                         (struct sockaddr*)&sender_addr, &sender_len);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            std::string chat_msg = state.opponentNick + ": " + std::string(buffer);
            state.chat_messages.push_back(chat_msg);
        }
    }
    
    endwin();
    close(client_socket);
    close(chat_socket);
    return 0;
}