# USB Serial Packet Communicator for Raspberry Pi Pico

## Build & Upload

Before building, ensure you run the `scripts/apply-patch.sh` to apply necessary patches to the Arduino Pico.

```bash
pio project metadata # Recommended to run this first
./scripts/apply-patch.sh
```

Then, build and upload the code using PlatformIO:

```bash
pio run -t upload
```

## Running benchmark

Requires JVM 21 or later.

```bash
cd benchmark
./gradlew run --args="/dev/ttyACM0"
```
