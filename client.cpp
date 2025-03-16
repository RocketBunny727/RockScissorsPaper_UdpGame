#include <iostream>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ncurses.h>

struct GameState {
    std::string opponentNick;
    int myScore;
    int opScore;
    int selection;
    bool waiting;

    GameState() : opponentNick("Unknown"), myScore(0), opScore(0), selection(0), waiting(false) {}
};

void show_game_info(const GameState& state) {
    std::string output = "Your opponent: " + state.opponentNick + "\nSCORE - " + 
                        std::to_string(state.myScore) + ":" + std::to_string(state.opScore);
    printw("%s\n", output.c_str());
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

bool init_game(int client_socket, sockaddr_in& server_addr, socklen_t server_len, 
              std::string& nickname, GameState& state) {
    bool nick_accepted = false;
    
    while (!nick_accepted) {
        sendto(client_socket, nickname.c_str(), nickname.length(), 0, 
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
            std::cin >> nickname;
            initscr();
            keypad(stdscr, TRUE);
            noecho();
            curs_set(0);
            timeout(100);
            continue;
        }
        
        if (message.rfind("SESSION", 0) == 0) {
            char opponent[256];
            sscanf(buffer, "SESSION %s %d:%d", opponent, &state.myScore, &state.opScore);
            state.opponentNick = opponent;
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
    int server_port;
    
    std::cout << "Enter server IP: ";
    std::cin >> server_ip;
    std::cout << "Enter server port: ";
    std::cin >> server_port;

    int client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);
    
    socklen_t server_len = sizeof(server_addr);
    std::string nickname;
    
    std::cout << "Enter your nickname: ";
    std::cin >> nickname;
    
    initscr();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    timeout(100);
    
    GameState state;
    if (!init_game(client_socket, server_addr, server_len, nickname, state)) {
        endwin();
        close(client_socket);
        return 1;
    }
    
    char buffer[1024];
    while (true) {
        clear();
        show_game_info(state);
        show_menu(state);
        refresh();
        
        int key = getch();
        if (key == KEY_UP) state.selection = (state.selection + 2) % 3;
        else if (key == KEY_DOWN) state.selection = (state.selection + 1) % 3;
        else if (key == 10 && !state.waiting) {
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
                    if (!init_game(client_socket, server_addr, server_len, nickname, state)) {
                        endwin();
                        close(client_socket);
                        return 1;
                    }
                } else {
                    break; // Просто выходим, сервер уже удалил игрока при WIN/LOSE
                }
            } else if (message.rfind("SCORE", 0) == 0) {
                sscanf(buffer, "SCORE %d:%d", &state.myScore, &state.opScore);
                state.waiting = false;
            }
        }
    }
    
    endwin();
    close(client_socket);
    return 0;
}