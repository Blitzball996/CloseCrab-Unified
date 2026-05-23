plugins {
    id("java")
    id("org.jetbrains.intellij") version "1.17.0"
}

group = "com.closecrab"
version = "0.5.0"

repositories {
    mavenCentral()
}

intellij {
    version.set("2023.3")
    type.set("IC")
}

tasks {
    patchPluginXml {
        sinceBuild.set("233")
        untilBuild.set("243.*")
    }
}
