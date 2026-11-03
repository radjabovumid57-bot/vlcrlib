#include <vlcrlib.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_mouse.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_PATH_SIZE 1024

typedef struct AudioFileDialog {
    char path[AUDIO_PATH_SIZE];
    bool selected;
} AudioFileDialog;

static bool drawButton(Font font, const char *label, Rectangle bounds,
                       Vector2 mouse, bool pressed) {
    bool hovered = mouse.x >= bounds.x && mouse.x <= bounds.x + bounds.width &&
                   mouse.y >= bounds.y && mouse.y <= bounds.y + bounds.height;
    Color fill = pressed ? (Color){35, 85, 135, 255}
                         : hovered ? (Color){55, 110, 170, 255}
                                   : (Color){40, 75, 115, 255};
    Color border = hovered ? SKYBLUE : (Color){85, 125, 165, 255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLines((int)bounds.x, (int)bounds.y,
                       (int)bounds.width, (int)bounds.height, border);
    DrawTextEx(font, label,
               (Vector2){bounds.x + 18.0f, bounds.y + 9.0f}, 20.0f, 1.0f, WHITE);
    return hovered;
}

static void loadAudioFile(const char *path, Music *music, Sound *sound) {
    UnloadSound(*sound);
    UnloadMusicStream(*music);
    *music = LoadMusicStream(path);
    *sound = LoadSound(path);
    music->loop = true;
    SetMusicVolume(*music, 0.75f);
    SetSoundVolume(*sound, 0.75f);
    PlayMusicStream(*music);
}

static void audioFileDialogCallback(void *userdata,
                                    const char * const *filelist,
                                    int filter) {
    AudioFileDialog *dialog = (AudioFileDialog *)userdata;
    (void)filter;
    if (filelist && filelist[0]) {
        strncpy(dialog->path, filelist[0], sizeof(dialog->path) - 1);
        dialog->path[sizeof(dialog->path) - 1] = '\0';
        dialog->selected = true;
    }
}

static void openAudioFile(AudioFileDialog *dialog) {
#ifdef VLCRLIB_NO_VLC
    const SDL_DialogFileFilter filters[] = {
        { "WAV audio (SDL backend)", "wav" },
        { "All files", "*" }
    };
#else
    const SDL_DialogFileFilter filters[] = {
        { "Audio files", "mp3;wav;ogg;flac;m4a;aac" },
        { "All files", "*" }
    };
#endif
    dialog->selected = false;
    SDL_ShowOpenFileDialog(audioFileDialogCallback, dialog,
                           (SDL_Window *)vcore_GetWindow(), filters, 2, NULL, false);
}

int main(int argc, char **argv) {
    const char *musicPath = (argc > 1) ? argv[1] : NULL;
    const char *soundPath = (argc > 2) ? argv[2] : musicPath;

    if (!InitWindow(960, 540, "vlcrlib audio example")) return 1;
    SetTargetFPS(60);

    if (!InitAudioDevice()) {
        printf("[vlcrlib] audio unavailable\n");
        CloseWindow();
        return 1;
    }

    Music music = {0};
    Sound sound = {0};
    AudioFileDialog dialog = {0};
    Font uiFont = {0};
#ifdef _WIN32
    uiFont = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 32, 32, 96);
#endif
    bool customFontLoaded = uiFont.ok;
    if (!uiFont.ok) {
        uiFont = GetFontDefault();
    }
    char selectedPath[AUDIO_PATH_SIZE] = {0};
    if (musicPath) {
        strncpy(selectedPath, musicPath, sizeof(selectedPath) - 1);
        music = LoadMusicStream(musicPath);
    }
    if (soundPath) sound = LoadSound(soundPath);
    music.loop = true;
    SetMusicVolume(music, 0.75f);
    SetSoundVolume(sound, 0.75f);
    if (music.player) PlayMusicStream(music);

    while (!WindowShouldClose()) {
        UpdateMusicStream(music);

        if (IsKeyPressed(SDL_SCANCODE_SPACE)) {
            if (IsMusicStreamPlaying(music)) PauseMusicStream(music);
            else ResumeMusicStream(music);
        }
        if (IsKeyPressed(SDL_SCANCODE_RETURN)) PlaySound(sound);
        if (IsKeyPressed(SDL_SCANCODE_ESCAPE)) StopMusicStream(music);

        Vector2 mouse = GetMousePosition();
        Rectangle openButtonBounds = {40, 220, 240, 40};
        bool openButtonHovered = mouse.x >= openButtonBounds.x &&
                                 mouse.x <= openButtonBounds.x + openButtonBounds.width &&
                                 mouse.y >= openButtonBounds.y &&
                                 mouse.y <= openButtonBounds.y + openButtonBounds.height;
        bool openButtonPressed = openButtonHovered && IsMouseButtonDown(SDL_BUTTON_LEFT);
        if (openButtonHovered && IsMouseButtonPressed(SDL_BUTTON_LEFT)) {
            openAudioFile(&dialog);
        }
        if (dialog.selected) {
            dialog.selected = false;
            strncpy(selectedPath, dialog.path, sizeof(selectedPath) - 1);
            selectedPath[sizeof(selectedPath) - 1] = '\0';
            loadAudioFile(dialog.path, &music, &sound);
        }

        SetVulkanClearColor(18.0f / 255.0f, 22.0f / 255.0f, 32.0f / 255.0f, 1.0f);
        BeginDrawing();
        DrawTextEx(uiFont, "vlcrlib audio example", (Vector2){40, 36}, 32, 1, WHITE);
        DrawTextEx(uiFont, "SPACE  pause / resume music", (Vector2){40, 110}, 22, 1, LIGHTGRAY);
        DrawTextEx(uiFont, "ENTER  play sound effect", (Vector2){40, 145}, 22, 1, LIGHTGRAY);
        DrawTextEx(uiFont, "ESC    stop music", (Vector2){40, 180}, 22, 1, LIGHTGRAY);
        drawButton(uiFont, "Choose audio file", openButtonBounds,
               mouse, openButtonPressed);

        if (music.player) {
            float length = GetMusicTimeLength(music);
            float played = GetMusicTimePlayed(music);
            char status[128];
            snprintf(status, sizeof(status), "Music: %s  %.1f / %.1f s",
                     IsMusicStreamPlaying(music) ? "playing" : "paused",
                     played, length);
            DrawTextEx(uiFont, status, (Vector2){40, 270}, 24, 1, GREEN);
        } else {
            DrawTextEx(uiFont, "Music could not be loaded.", (Vector2){40, 270}, 24, 1, RED);
        }
        DrawTextEx(uiFont, sound.player ? "Sound effect: ready" : "Sound effect: unavailable",
                   (Vector2){40, 310}, 24, 1, sound.player ? SKYBLUE : RED);
        if (selectedPath[0]) {
            const char *name = strrchr(selectedPath, '\\');
            if (!name) name = strrchr(selectedPath, '/');
            DrawTextEx(uiFont, name ? name + 1 : selectedPath,
                       (Vector2){40, 350}, 20, 1, LIGHTGRAY);
        } else {
            DrawTextEx(uiFont, "Choose an audio file to begin.",
                       (Vector2){40, 350}, 20, 1, LIGHTGRAY);
        }
        EndDrawing();
    }

    if (customFontLoaded) UnloadFont(uiFont);
    UnloadSound(sound);
    UnloadMusicStream(music);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
