#include <vlcrlib.h>
#include "vnetwork.h"
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_scancode.h>
#ifdef _WIN32
__declspec(dllimport) short __stdcall GetAsyncKeyState(int virtualKey);
#define GAME_VK_UP 0x26
#define GAME_VK_LEFT 0x25
#define GAME_VK_DOWN 0x28
#define GAME_VK_RIGHT 0x27
#define GAME_VK_SPACE 0x20
#else
#define GAME_VK_UP 0
#define GAME_VK_LEFT 0
#define GAME_VK_DOWN 0
#define GAME_VK_RIGHT 0
#define GAME_VK_SPACE 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_W 800
#define WINDOW_H 480
#define PORT_DEFAULT 42069
#define PADDLE_W 18.0f
#define PADDLE_H 100.0f
#define BALL_R 12.0f
#define PACKET_MAGIC 0x564C4352u

typedef struct InputPacket {
    uint32_t magic;
    float paddleY;
    int serve;
} InputPacket;

typedef struct StatePacket {
    uint32_t magic;
    float leftY;
    float rightY;
    float ballX;
    float ballY;
    int leftScore;
    int rightScore;
    int ballInPlay;
} StatePacket;

static float clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool parsePort(const char *text, int *port) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < 1 || value > 65535) return false;
    *port = (int)value;
    return true;
}

static bool gameKeyDown(int scancode, int virtualKey) {
#ifdef _WIN32
    (void)scancode;
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
#else
    (void)virtualKey;
    return IsKeyDown(scancode);
#endif
}

static bool gameKeyPressed(int scancode, int virtualKey) {
#ifdef _WIN32
    static bool wasDown[256] = { false };
    if (virtualKey < 0 || virtualKey >= 256) return false;
    bool isDown = gameKeyDown(scancode, virtualKey);
    bool pressed = isDown && !wasDown[virtualKey];
    wasDown[virtualKey] = isDown;
    return pressed;
#else
    (void)virtualKey;
    return IsKeyPressed(scancode);
#endif
}

static bool bounceOnPaddle(StatePacket *state, bool leftPaddle,
                           float paddleVelocity, float *ballVX,
                           float *ballVY, float *ballSpeed) {
    const float paddleX = leftPaddle ? 42.0f : WINDOW_W - 60.0f;
    const float paddleLeft = paddleX;
    const float paddleRight = paddleX + PADDLE_W;
    const float paddleY = leftPaddle ? state->leftY : state->rightY;
    const float ballLeft = state->ballX - BALL_R;
    const float ballRight = state->ballX + BALL_R;
    const float ballTop = state->ballY - BALL_R;
    const float ballBottom = state->ballY + BALL_R;
    const bool overlaps = ballRight >= paddleLeft && ballLeft <= paddleRight &&
                          ballBottom >= paddleY && ballTop <= paddleY + PADDLE_H;
    const bool movingTowardPaddle = leftPaddle ? (*ballVX < 0.0f) : (*ballVX > 0.0f);
    if (!overlaps || !movingTowardPaddle) return false;

    /* Resolve the face collision so the ball cannot get stuck inside a paddle. */
    state->ballX = leftPaddle ? paddleRight + BALL_R : paddleLeft - BALL_R;

    const float paddleCenter = paddleY + PADDLE_H * 0.5f;
    const float impact = clampf((state->ballY - paddleCenter) / (PADDLE_H * 0.5f),
                                -1.0f, 1.0f);
    const float motion = clampf(paddleVelocity / 360.0f, -1.0f, 1.0f);
    const float angle = clampf(impact * 0.95f + motion * 0.18f, -1.15f, 1.15f);

    *ballSpeed = fminf(*ballSpeed * 1.04f, 650.0f);
    const float horizontalSpeed = *ballSpeed * cosf(angle);
    *ballVX = leftPaddle ? horizontalSpeed : -horizontalSpeed;
    *ballVY = *ballSpeed * sinf(angle);
    return true;
}

static void drawGame(const StatePacket *state, bool isHost, bool connected) {
    BeginDrawing();
    ClearBackground((Color){ 8, 15, 27, 255 });

    for (int x = 0; x < WINDOW_W; x += 40)
        DrawLineEx((Vector2){ (float)x, 0 }, (Vector2){ (float)x, WINDOW_H },
                   1.0f, (Color){ 17, 29, 43, 255 });
    for (int y = 0; y < WINDOW_H; y += 40)
        DrawLineEx((Vector2){ 0, (float)y }, (Vector2){ WINDOW_W, (float)y },
                   1.0f, (Color){ 17, 29, 43, 255 });

    for (int y = 16; y < WINDOW_H; y += 28)
        DrawRectangle(WINDOW_W / 2 - 2, y, 4, 14, (Color){ 48, 63, 81, 255 });

    DrawRectangle(42, (int)state->leftY, (int)PADDLE_W, (int)PADDLE_H,
                  (Color){ 30, 229, 238, 255 });
    DrawRectangle(WINDOW_W - 60, (int)state->rightY, (int)PADDLE_W,
                  (int)PADDLE_H, (Color){ 241, 48, 183, 255 });
    DrawCircle((int)state->ballX, (int)state->ballY, BALL_R + 10,
               (Color){ 83, 68, 38, 150 });
    DrawCircle((int)state->ballX, (int)state->ballY, BALL_R,
               (Color){ 255, 222, 79, 255 });

    char score[64];
    snprintf(score, sizeof(score), "%d          %d", state->leftScore,
             state->rightScore);
    DrawText(score, WINDOW_W / 2 - 75, 20, 28, WHITE);
    DrawText(isHost ? "P1: W/S" : "P2: MOVE MOUSE UP/DOWN", 22,
             WINDOW_H - 34, 17, isHost ? (Color){ 30, 229, 238, 255 }
                                        : (Color){ 241, 48, 183, 255 });
    DrawText(connected ? "UDP CONNECTED" : (isHost ? "WAITING FOR PLAYER 2" : "CONNECTING"),
             WINDOW_W - 230, WINDOW_H - 32, 15,
             connected ? (Color){ 120, 225, 150, 255 }
                       : (Color){ 245, 190, 90, 255 });
    if (connected && !state->ballInPlay)
        DrawText("SPACE TO SERVE", WINDOW_W / 2 - 88, WINDOW_H / 2 + 52, 20, WHITE);
    EndDrawing();
}

int main(int argc, char **argv) {
    bool isHost = true;
    bool autoPlayer2 = true;
    const char *host = "127.0.0.1";
    int port = PORT_DEFAULT;

    if (argc > 1 && (strcmp(argv[1], "join") == 0 || strcmp(argv[1], "client") == 0)) {
        isHost = false;
        if (argc > 2) host = argv[2];
        if (argc > 3 && !parsePort(argv[3], &port)) {
            fprintf(stderr, "Invalid port: %s\n", argv[3]);
            return 1;
        }
    } else if (argc > 1) {
        if (strcmp(argv[1], "host-only") == 0) {
            autoPlayer2 = false;
            if (argc > 2 && !parsePort(argv[2], &port)) {
                fprintf(stderr, "Invalid port: %s\n", argv[2]);
                return 1;
            }
        } else if (strcmp(argv[1], "host") == 0) {
            if (argc > 2 && !parsePort(argv[2], &port)) {
                fprintf(stderr, "Invalid port: %s\n", argv[2]);
                return 1;
            }
        } else {
            fprintf(stderr, "Usage: network_example [host [port] | host-only [port]] | join [host [port]]\n");
            return 1;
        }
    }

    if (!InitNetwork()) {
        fprintf(stderr, "Could not initialize networking.\n");
        return 1;
    }

    NetSocket socket = NetCreateUDP();
    if (!socket.handle) {
        fprintf(stderr, "Could not create UDP socket.\n");
        CloseNetwork();
        return 1;
    }
    NetAddress remoteAddress = { "", 0 };
    if (isHost) {
        if (!NetBindUDP(&socket, port)) {
            fprintf(stderr, "Could not bind UDP port %d.\n", port);
            NetClose(&socket);
            CloseNetwork();
            return 1;
        }
        printf("UDP host ready on port %d.\n", port);
    } else {
        if (!NetSetUDPPeer(&socket, host, port)) {
            fprintf(stderr, "Could not set UDP peer to %s:%d.\n", host, port);
            NetClose(&socket);
            CloseNetwork();
            return 1;
        }
        remoteAddress.port = port;
        snprintf(remoteAddress.host, sizeof(remoteAddress.host), "%s", host);
        printf("Using UDP peer %s:%d.\n", host, port);
    }
    if (!NetSetNonBlocking(&socket, true)) {
        fprintf(stderr, "Could not set UDP socket nonblocking.\n");
        NetClose(&socket);
        CloseNetwork();
        return 1;
    }

    if (!InitWindow(WINDOW_W, WINDOW_H, isHost ? "vlcrlib Tennis - Host" : "vlcrlib Tennis - Player 2")) {
        NetClose(&socket);
        CloseNetwork();
        return 1;
    }
    SetTargetFPS(60);

    VInputState gameInput;
    VInputInit(&gameInput);
    VInputBindAxisKeys(&gameInput, "p1_move_y",
                       SDL_SCANCODE_W, SDL_SCANCODE_S);
    VInputSetAxisSmoothing(&gameInput, "p1_move_y", 0.0f);
    VInputBindKey(&gameInput, "serve", SDL_SCANCODE_SPACE);

    SDL_Process *localPlayer2 = NULL;
    if (isHost && autoPlayer2) {
        char portText[16];
        snprintf(portText, sizeof(portText), "%d", port);
        const char *childArgs[] = { argv[0], "join", "127.0.0.1", portText, NULL };
        localPlayer2 = SDL_CreateProcess(childArgs, false);
        if (!localPlayer2) {
            fprintf(stderr, "Could not open the second player window: %s\n", SDL_GetError());
        }
    }

    StatePacket state = { PACKET_MAGIC, WINDOW_H / 2.0f - PADDLE_H / 2.0f,
                          WINDOW_H / 2.0f - PADDLE_H / 2.0f,
                          WINDOW_W / 2.0f, WINDOW_H / 2.0f, 0, 0, 0 };
    InputPacket input = { PACKET_MAGIC, state.rightY, 0 };
    bool connected = false;
    bool serveRequested = false;
    float ballSpeed = 300.0f;
    float ballVX = 260.0f;
    float ballVY = 150.0f;

    while (!WindowShouldClose()) {
        float dt = clampf(GetFrameTime(), 0.0f, 0.05f);
        VInputUpdate(&gameInput, dt);
        const float previousLeftY = state.leftY;
        const float previousRightY = state.rightY;
        float *myPaddle = isHost ? &state.leftY : &state.rightY;
        float move = isHost ? VInputGetAxis(&gameInput, "p1_move_y") : 0.0f;
        if (isHost) {
            float globalMove = 0.0f;
            if (gameKeyDown(SDL_SCANCODE_W, 'W')) globalMove -= 1.0f;
            if (gameKeyDown(SDL_SCANCODE_S, 'S')) globalMove += 1.0f;
            if (globalMove != 0.0f) move = globalMove;
        } else {
            Vector2 mousePosition = VInputMousePosition(&gameInput);
            state.rightY = clampf(mousePosition.y - PADDLE_H * 0.5f,
                                  0.0f, WINDOW_H - PADDLE_H);
        }
        if (isHost)
            *myPaddle = clampf(*myPaddle + move * 360.0f * dt,
                               0.0f, WINDOW_H - PADDLE_H);

        if (!isHost) {
            bool inputServePressed = VInputIsActionPressed(&gameInput, "serve");
            bool globalServePressed = gameKeyPressed(SDL_SCANCODE_SPACE, GAME_VK_SPACE);
            if (inputServePressed || globalServePressed) serveRequested = true;
            input.paddleY = state.rightY;
            input.serve = serveRequested ? 1 : 0;
            NetSend(&socket, &input, sizeof(input));

            StatePacket latestState;
            int got = NetRecv(&socket, &latestState, sizeof(latestState));
            if (got == (int)sizeof(latestState) && latestState.magic == PACKET_MAGIC) {
                state = latestState;
                connected = true;
                if (state.ballInPlay) serveRequested = false;
            }
        } else {
            InputPacket latestInput;
            NetAddress sender;
            int got;
            while ((got = NetRecvFrom(&socket, &latestInput, sizeof(latestInput), &sender)) > 0) {
                if (got == (int)sizeof(latestInput) && latestInput.magic == PACKET_MAGIC) {
                    remoteAddress = sender;
                    if (!connected) printf("Player 2 connected over UDP from %s:%d.\n", sender.host, sender.port);
                    connected = true;
                    state.rightY = clampf(latestInput.paddleY, 0.0f,
                                          WINDOW_H - PADDLE_H);
                    if (latestInput.serve) state.ballInPlay = 1;
                }
            }
        }

        if (isHost && connected) {
            bool inputServePressed = VInputIsActionPressed(&gameInput, "serve");
            bool globalServePressed = gameKeyPressed(SDL_SCANCODE_SPACE, GAME_VK_SPACE);
            if (inputServePressed || globalServePressed) {
                state.ballInPlay = 1;
                ballSpeed = 300.0f;
                ballVX = state.rightScore > state.leftScore ? -260.0f : 260.0f;
                ballVY = 150.0f;
            }
            if (state.ballInPlay) {
                state.ballX += ballVX * dt;
                state.ballY += ballVY * dt;
                if (state.ballY - BALL_R < 0.0f) {
                    state.ballY = BALL_R;
                    ballVY = fabsf(ballVY);
                } else if (state.ballY + BALL_R > WINDOW_H) {
                    state.ballY = WINDOW_H - BALL_R;
                    ballVY = -fabsf(ballVY);
                }

                const float leftPaddleVelocity = dt > 0.0f
                    ? (state.leftY - previousLeftY) / dt : 0.0f;
                const float rightPaddleVelocity = dt > 0.0f
                    ? (state.rightY - previousRightY) / dt : 0.0f;
                bounceOnPaddle(&state, true, leftPaddleVelocity,
                               &ballVX, &ballVY, &ballSpeed);
                bounceOnPaddle(&state, false, rightPaddleVelocity,
                               &ballVX, &ballVY, &ballSpeed);

                if (state.ballX < -BALL_R) {
                    ++state.rightScore;
                    state.ballX = WINDOW_W / 2.0f;
                    state.ballY = WINDOW_H / 2.0f;
                    state.ballInPlay = 0;
                } else if (state.ballX > WINDOW_W + BALL_R) {
                    ++state.leftScore;
                    state.ballX = WINDOW_W / 2.0f;
                    state.ballY = WINDOW_H / 2.0f;
                    state.ballInPlay = 0;
                }
            }
            state.magic = PACKET_MAGIC;
            NetSendTo(&socket, &remoteAddress, &state, sizeof(state));
        }

        drawGame(&state, isHost, connected);
    }

    CloseWindow();
    if (localPlayer2) SDL_DestroyProcess(localPlayer2);
    NetClose(&socket);
    CloseNetwork();
    return 0;
}
