plugins {
    id("com.android.application")
}
android {
    namespace = "com.fatemmo.game"
    compileSdk = 35
    ndkVersion = "27.1.12297006"
    defaultConfig {
        applicationId = "com.fatemmo.game"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DFATEMMO_BUILD_MOBILE=ON"
                arguments += "-DFATEMMO_PLATFORM_ANDROID=ON"
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    sourceSets {
        getByName("main") {
            assets.srcDirs("../../../assets")
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
}
