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

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++23"
                arguments += listOf(
                    "-DFATEMMO_BUILD_MOBILE=ON",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../assets")
        }
    }
}

dependencies {
    // Google Mobile Ads SDK (AdMob) for rewarded video.
    // Version pinned; bump deliberately and re-test consent flow.
    implementation("com.google.android.gms:play-services-ads:23.6.0")
    // User Messaging Platform — required for GDPR/CCPA consent collection
    // before initializing the ads SDK in EU/UK/California regions.
    implementation("com.google.android.ump:user-messaging-platform:3.0.0")
}
