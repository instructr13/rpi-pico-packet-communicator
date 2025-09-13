package dev.wycey.mido

import dev.wycey.mido.dev.wycey.mido.benchmark.BaseSerialDevice
import dev.wycey.mido.dev.wycey.mido.benchmark.packet.Packet

internal fun benchmark(port: String, frames: Int, frameSize: Int) {
  var allReceived = false
  var received = 0
  var start: Long = 0

  val device = object : BaseSerialDevice(921600, port) {
    override fun onRecv(packet: Packet) {
      if (packet.type.toInt() == 0x0004) {
        received += packet.payload.size

        if (received >= frames * frameSize) {
          val end = System.currentTimeMillis()
          val duration = end - start
          val seconds = duration.toDouble() / 1000.0
          val kbps = (received.toDouble() / 1024.0) * 8 / seconds

          allReceived = true

          println("Received $received bytes in $duration ms (${"%.3f".format(kbps)} kbps)")
        }
      }
    }
  }

  println("Wait for device to be available...")

  while (!device.available) {
    Thread.sleep(100)
  }

  println("Device is available, starting benchmark...")

  start = System.currentTimeMillis()

  for (i in 0 until frames) {
    val payload = ByteArray(frameSize) { (it % 256).toByte() }

    device.send(Packet(0x0004u, payload))
  }

  println("Sent $frames frames of size $frameSize bytes")

  var waited = 0
  var prevReceived = 0

  while (!allReceived) {
    Thread.sleep(200)
    waited += 200

    if (waited % 1000 == 0) {
      println("Waiting... $waited ms, received $received bytes so far")
      if (received == prevReceived && waited >= 30000) {
        println("No more data received for 30 seconds, stopping benchmark")

        break
      }
    }

    prevReceived = received
  }

  device.dispose()
}

fun main(vararg args: String) {
  if (args.isEmpty()) {
    println("Usage: benchmark <port>")

    return
  }

  BaseSerialDevice.enableDebugOutput = true

  benchmark(args[0], 2000, 1024)
}
