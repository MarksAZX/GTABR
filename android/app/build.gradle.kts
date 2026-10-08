plugins {
    id("com.android.application")
}

// Game data (ASTC textures, sprite index, fonts) lives in ../../assets/data and is packaged as APK assets under data/.
val apkAssetsDir = layout.buildDirectory.dir("apkassets")
val copyGameAssets by tasks.registering(Copy::class) {
    from("../../assets/data") {
        // character/vehicle billboard atlases are only a fallback now that both are real 3D models
        exclude("chr_*.gtex", "veh_*.gtex")
    }
    into(apkAssetsDir.map { it.dir("data") })
}

android {
    namespace = "com.marksazx.gtabr"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.marksazx.gtabr"
        minSdk = 26
        targetSdk = 34
        versionCode = 12
        versionName = "0.10.0"
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared", "-DCMAKE_BUILD_TYPE=Release")
                cppFlags += listOf("-std=c++20", "-O2")
            }
        }
    }

    buildTypes {
        getByName("debug") {
            isJniDebuggable = false
            externalNativeBuild { cmake { arguments += listOf("-DCMAKE_BUILD_TYPE=Release") } }
        }
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1+"
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(apkAssetsDir)
        }
    }

    androidResources {
        // ASTC/GTEX files are already compressed; storing them avoids inflating on load
        // gtex is zip-compressed: fonts/masks shrink a lot and ASTC still gains ~20%
    }
}

tasks.named("preBuild") { dependsOn(copyGameAssets) }
