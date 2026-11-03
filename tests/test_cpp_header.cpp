// Public headers must remain valid C++17, not only C.
#include "vlcrlib.h"

int main() {
    Color color = WHITE;
    Vector3 v = Vector3Add(Vector3{1.0f, 2.0f, 3.0f},
                           Vector3{4.0f, 5.0f, 6.0f});
    return (color.r == 255 && v.x == 5.0f && v.y == 7.0f && v.z == 9.0f)
        ? 0 : 1;
}
