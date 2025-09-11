plugins {
  kotlin("jvm") version "2.2.0"
  application
  id("com.gradleup.shadow") version "9.1.0"
}

group = "dev.wycey.mido"
version = "1.0-SNAPSHOT"

repositories {
  mavenCentral()
}

dependencies {
  implementation("io.github.java-native:jssc:2.10.2")
}

application {
  mainClass.set("dev.wycey.mido.MainKt")
}

kotlin {
  jvmToolchain(24)
}
