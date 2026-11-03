plugins {
    id("com.android.application")
}

android {
    namespace = "org.vlcrlib.mobile"
    compileSdk = 35
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "org.vlcrlib.mobile"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DVLCRLIB_SKIP_VCPKG=ON",
                    "-DVLCRLIB_ENABLE_VLC=OFF",
                    "-DVLCRLIB_BUILD_EXAMPLES=OFF",
                    "-DVLCRLIB_BUILD_TESTS=OFF",
                    "-DVLCRLIB_BUILD_TOOLS=OFF",
                    "-DVLCRLIB_ENABLE_PHYSICS=OFF",
                    "-DVLCRLIB_ENABLE_JOLT=OFF",
                    "-DCMAKE_PROGRAM_PATH=" +
                        (System.getenv("ANDROID_NDK_HOME")
                            ?: "C:/Android/Sdk/ndk/26.1.10909125") +
                        "/shader-tools/windows-x86_64"
                )
                cppFlags += "-std=c++17"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDirs("src/main/assets")
    sourceSets["main"].java.srcDirs(
        "src/main/java",
        // If SDL3 is vendored as source, use its Java activity classes.
        "../../external/SDL3/android-project/app/src/main/java"
    )

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}
