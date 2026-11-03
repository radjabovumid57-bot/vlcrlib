#include "vscene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void SceneInit(Scene *scene) {
    if (scene) memset(scene, 0, sizeof(*scene));
}

int SceneAddNode(Scene *scene, const SceneNode *node) {
    if (!scene || !node || scene->count >= SCENE_MAX_NODES) return -1;
    scene->nodes[scene->count] = *node;
    if (scene->nodes[scene->count].scale.x == 0.0f &&
        scene->nodes[scene->count].scale.y == 0.0f &&
        scene->nodes[scene->count].scale.z == 0.0f)
        scene->nodes[scene->count].scale = (Vector3){1,1,1};
    scene->nodes[scene->count].visible = node->visible ? 1 : 0;
    if (node->parent < -1 || node->parent >= scene->count)
        scene->nodes[scene->count].parent = -1;
    return scene->count++;
}

static const char *skipWs(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static bool expectChar(const char **p, char c) {
    *p = skipWs(*p);
    if (**p != c) return false;
    (*p)++;
    return true;
}

static bool parseString(const char **p, char *out, size_t cap) {
    *p = skipWs(*p);
    if (**p != '"') return false;
    (*p)++;
    size_t n = 0;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\' && (*p)[1]) {
            (*p)++;
            c = **p;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
        }
        if (out && n + 1 < cap) out[n++] = c;
        (*p)++;
    }
    if (**p != '"') return false;
    (*p)++;
    if (out && cap) out[n] = 0;
    return true;
}

static bool parseNumber(const char **p, float *out) {
    *p = skipWs(*p);
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) return false;
    *p = end;
    if (out) *out = (float)v;
    return true;
}

static bool parseVec3(const char **p, Vector3 *v) {
    if (!expectChar(p, '[')) return false;
    float x, y, z;
    if (!parseNumber(p, &x) || !expectChar(p, ',') ||
        !parseNumber(p, &y) || !expectChar(p, ',') ||
        !parseNumber(p, &z) || !expectChar(p, ']')) return false;
    if (v) *v = (Vector3){x, y, z};
    return true;
}

static bool parseColor(const char **p, Color *c) {
    if (!expectChar(p, '[')) return false;
    float r, g, b, a;
    if (!parseNumber(p, &r) || !expectChar(p, ',') ||
        !parseNumber(p, &g) || !expectChar(p, ',') ||
        !parseNumber(p, &b) || !expectChar(p, ',') ||
        !parseNumber(p, &a) || !expectChar(p, ']')) return false;
    if (c) *c = (Color){ (unsigned char)r, (unsigned char)g,
                         (unsigned char)b, (unsigned char)a };
    return true;
}

static bool skipValue(const char **p) {
    *p = skipWs(*p);
    if (**p == '"') {
        char tmp[8];
        return parseString(p, tmp, sizeof(tmp));
    }
    if (**p == '[' || **p == '{') {
        char open = **p, close = open == '[' ? ']' : '}';
        int depth = 0;
        while (**p) {
            if (**p == '"') { char tmp[8]; if (!parseString(p, tmp, sizeof(tmp))) return false; continue; }
            if (**p == open) depth++;
            else if (**p == close && --depth == 0) { (*p)++; return true; }
            (*p)++;
        }
        return false;
    }
    float tmp;
    return parseNumber(p, &tmp);
}

static void writeJsonString(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", (unsigned)*p);
                else fputc(*p, f);
                break;
        }
    }
    fputc('"', f);
}

bool SceneSave(const Scene *scene, const char *path) {
    if (!scene || !path) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "{\n  \"version\": 1,\n  \"nodes\": [\n");
    for (int i = 0; i < scene->count; ++i) {
        const SceneNode *n = &scene->nodes[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"model\": ");
        writeJsonString(f, n->model);
        fprintf(f, ",\n");
        fprintf(f, "      \"position\": [%g, %g, %g],\n",
                n->position.x, n->position.y, n->position.z);
        fprintf(f, "      \"rotation\": [%g, %g, %g],\n",
                n->rotation.x, n->rotation.y, n->rotation.z);
        fprintf(f, "      \"scale\": [%g, %g, %g],\n",
                n->scale.x, n->scale.y, n->scale.z);
        fprintf(f, "      \"tint\": [%u, %u, %u, %u],\n",
                n->tint.r, n->tint.g, n->tint.b, n->tint.a);
        fprintf(f, "      \"visible\": %d,\n", n->visible ? 1 : 0);
        fprintf(f, "      \"parent\": %d\n", n->parent);
        fprintf(f, "    }%s\n", i + 1 < scene->count ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

bool SceneLoad(Scene *scene, const char *path) {
    if (!scene || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 8 * 1024 * 1024) { fclose(f); return false; }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return false; }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text); fclose(f); return false;
    }
    text[size] = 0;
    fclose(f);

    Scene out;
    SceneInit(&out);
    const char *p = skipWs(text);
    if (!expectChar(&p, '{')) { free(text); return false; }
    while (1) {
        p = skipWs(p);
        if (*p == '}') { p++; break; }
        char key[64];
        if (!parseString(&p, key, sizeof(key)) || !expectChar(&p, ':')) {
            free(text); return false;
        }
        if (strcmp(key, "version") == 0) {
            float v = 0;
            if (!parseNumber(&p, &v)) { free(text); return false; }
        } else if (strcmp(key, "nodes") == 0) {
            if (!expectChar(&p, '[')) { free(text); return false; }
            while (1) {
                p = skipWs(p);
                if (*p == ']') { p++; break; }
                if (!expectChar(&p, '{')) { free(text); return false; }
                SceneNode node;
                memset(&node, 0, sizeof(node));
                node.scale = (Vector3){1,1,1};
                node.tint = (Color){255,255,255,255};
                node.visible = 1;
                node.parent = -1;
                while (1) {
                    p = skipWs(p);
                    if (*p == '}') { p++; break; }
                    char nk[64];
                    if (!parseString(&p, nk, sizeof(nk)) || !expectChar(&p, ':')) {
                        free(text); return false;
                    }
                    bool ok = true;
                    if (strcmp(nk, "model") == 0)
                        ok = parseString(&p, node.model, sizeof(node.model));
                    else if (strcmp(nk, "position") == 0)
                        ok = parseVec3(&p, &node.position);
                    else if (strcmp(nk, "rotation") == 0)
                        ok = parseVec3(&p, &node.rotation);
                    else if (strcmp(nk, "scale") == 0)
                        ok = parseVec3(&p, &node.scale);
                    else if (strcmp(nk, "tint") == 0)
                        ok = parseColor(&p, &node.tint);
                    else if (strcmp(nk, "visible") == 0) {
                        float v = 0;
                        ok = parseNumber(&p, &v);
                        node.visible = v != 0.0f;
                    } else if (strcmp(nk, "parent") == 0) {
                        float v = -1;
                        ok = parseNumber(&p, &v);
                        node.parent = (int)v;
                    } else {
                        ok = skipValue(&p);
                    }
                    if (!ok) { free(text); return false; }
                    p = skipWs(p);
                    if (*p == ',') { p++; continue; }
                }
                if (SceneAddNode(&out, &node) < 0) {
                    free(text);
                    return false;
                }
                p = skipWs(p);
                if (*p == ',') { p++; continue; }
            }
        } else {
            if (!skipValue(&p)) { free(text); return false; }
        }
        p = skipWs(p);
        if (*p == ',') { p++; continue; }
    }
    free(text);
    *scene = out;
    return true;
}

static void joinPath(char *out, size_t cap, const char *scenePath, const char *rel) {
    const char *slash = strrchr(scenePath, '/');
    const char *bslash = strrchr(scenePath, '\\');
    if (bslash > slash) slash = bslash;
    if (!slash) { snprintf(out, cap, "%s", rel); return; }
    size_t n = (size_t)(slash - scenePath) + 1;
    if (n >= cap) n = cap - 1;
    memcpy(out, scenePath, n);
    out[n] = 0;
    snprintf(out + n, cap - n, "%s", rel);
}

bool SceneLoadModels(Scene *scene, const char *scenePath, SceneRuntime *runtime) {
    if (!scene || !scenePath || !runtime) return false;
    memset(runtime, 0, sizeof(*runtime));
    for (int i = 0; i < scene->count; ++i) {
        char path[1024];
        joinPath(path, sizeof(path), scenePath, scene->nodes[i].model);
        runtime->models[i] = LoadModelAuto(path);
        runtime->loaded[i] = runtime->models[i].id != 0;
    }
    return true;
}

static Matrix sceneWorld(const Scene *scene, int index,
                         Matrix *world, unsigned char *done,
                         unsigned char *visiting) {
    if (index < 0 || index >= scene->count) return MatrixIdentity();
    if (done[index]) return world[index];
    if (visiting[index]) return MatrixIdentity();
    visiting[index] = 1;
    const SceneNode *n = &scene->nodes[index];
    Matrix local = MatrixMultiply(
        MatrixTranslate(n->position.x, n->position.y, n->position.z),
        MatrixMultiply(MatrixRotateXYZ(n->rotation),
                       MatrixScale(n->scale.x, n->scale.y, n->scale.z)));
    Matrix parent = MatrixIdentity();
    if (n->parent >= 0 && n->parent < scene->count && n->parent != index)
        parent = sceneWorld(scene, n->parent, world, done, visiting);
    world[index] = MatrixMultiply(parent, local);
    done[index] = 1;
    visiting[index] = 0;
    return world[index];
}

void SceneDraw(const Scene *scene, const SceneRuntime *runtime) {
    if (!scene || !runtime) return;
    Matrix world[SCENE_MAX_NODES];
    unsigned char done[SCENE_MAX_NODES] = {0};
    unsigned char visiting[SCENE_MAX_NODES] = {0};
    for (int i = 0; i < scene->count; ++i) {
        if (!scene->nodes[i].visible || !runtime->loaded[i]) continue;
        Matrix m = sceneWorld(scene, i, world, done, visiting);
        Vector3 t = scene->nodes[i].position;
        Vector3 r = scene->nodes[i].rotation;
        Vector3 s = scene->nodes[i].scale;
        if (!MatrixDecomposeTRS(m, &t, &r, &s)) {
            t = scene->nodes[i].position;
            r = scene->nodes[i].rotation;
            s = scene->nodes[i].scale;
        }
        DrawModel(runtime->models[i], t, r, s, scene->nodes[i].tint);
    }
}

void SceneUnloadModels(SceneRuntime *runtime) {
    if (!runtime) return;
    for (int i = 0; i < SCENE_MAX_NODES; ++i) {
        if (runtime->loaded[i] && runtime->models[i].id)
            UnloadModel(runtime->models[i]);
        runtime->models[i] = (Model){0};
        runtime->loaded[i] = 0;
    }
}
