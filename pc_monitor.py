#!/usr/bin/env python3
"""Send Windows PC statistics to an ESP32-035 over USB serial."""

from __future__ import annotations

import argparse
import json
import logging
import sys
import time
from dataclasses import asdict, dataclass
from typing import Optional

import psutil
import serial
from serial.tools import list_ports

LOGGER = logging.getLogger("pc_monitor")
BAUD_RATE = 115200
SAMPLE_INTERVAL_SECONDS = 1.0
CH340_VENDOR_ID = 0x1A86
CH340_PRODUCT_ID = 0x7523


@dataclass(frozen=True)
class Metrics:
    cpu: float
    mem: float
    mem_used: float
    mem_total: float
    gpu: Optional[float]
    gpu_temp: Optional[float]
    gpu_mem: Optional[float]
    gpu_mem_total: Optional[float]
    down: float
    up: float
    uptime: int


class NvidiaMetrics:
    def __init__(self) -> None:
        self._nvml = None
        self._handle = None
        try:
            import pynvml
        except ImportError:
            LOGGER.info("NVIDIA GPU metrics disabled: install nvidia-ml-py to enable them")
            return

        try:
            pynvml.nvmlInit()
            self._nvml = pynvml
            self._handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        except pynvml.NVMLError as error:
            LOGGER.warning("NVIDIA GPU metrics unavailable: %s", error)

    def read(self) -> tuple[Optional[float], Optional[float], Optional[float], Optional[float]]:
        if self._nvml is None or self._handle is None:
            return None, None, None, None

        try:
            utilization = self._nvml.nvmlDeviceGetUtilizationRates(self._handle)
            memory = self._nvml.nvmlDeviceGetMemoryInfo(self._handle)
            temperature = self._nvml.nvmlDeviceGetTemperature(
                self._handle, self._nvml.NVML_TEMPERATURE_GPU
            )
        except self._nvml.NVMLError as error:
            LOGGER.warning("Could not read NVIDIA GPU metrics: %s", error)
            return None, None, None, None

        return (
            float(utilization.gpu),
            float(temperature),
            memory.used / (1024**3),
            memory.total / (1024**3),
        )


class StatsCollector:
    def __init__(self) -> None:
        self._gpu = NvidiaMetrics()
        self._started = time.monotonic()
        self._previous_network = psutil.net_io_counters()
        self._previous_network_time = self._started
        psutil.cpu_percent(interval=None)

    def sample(self) -> Metrics:
        now = time.monotonic()
        network = psutil.net_io_counters()
        elapsed = max(now - self._previous_network_time, 0.001)
        down = max(network.bytes_recv - self._previous_network.bytes_recv, 0) / elapsed
        up = max(network.bytes_sent - self._previous_network.bytes_sent, 0) / elapsed
        self._previous_network = network
        self._previous_network_time = now

        memory = psutil.virtual_memory()
        gpu, gpu_temp, gpu_mem, gpu_mem_total = self._gpu.read()
        return Metrics(
            cpu=psutil.cpu_percent(interval=None),
            mem=float(memory.percent),
            mem_used=(memory.total - memory.available) / (1024**3),
            mem_total=memory.total / (1024**3),
            gpu=gpu,
            gpu_temp=gpu_temp,
            gpu_mem=gpu_mem,
            gpu_mem_total=gpu_mem_total,
            down=down,
            up=up,
            uptime=int(now - self._started),
        )


def find_serial_port() -> str:
    ports = list(list_ports.comports())
    matching = [
        port.device
        for port in ports
        if port.vid == CH340_VENDOR_ID and port.pid == CH340_PRODUCT_ID
    ]
    if len(matching) == 1:
        return matching[0]
    if len(matching) > 1:
        raise RuntimeError(
            "More than one CH340 serial adapter is connected; specify one with --port."
        )
    available = ", ".join(port.device for port in ports) or "none"
    raise RuntimeError(
        f"No CH340 adapter found (available serial ports: {available}); "
        "specify the board's COM port with --port."
    )


def encode_sample(sample: Metrics) -> bytes:
    payload = json.dumps(asdict(sample), separators=(",", ":"), allow_nan=False)
    return (payload + "\n").encode("utf-8")


def send_samples(port: str) -> None:
    collector = StatsCollector()
    LOGGER.info("Sending PC statistics to %s at %d baud; press Ctrl+C to stop", port, BAUD_RATE)
    with serial.Serial(port, BAUD_RATE, timeout=1, write_timeout=2) as connection:
        while True:
            connection.write(encode_sample(collector.sample()))
            time.sleep(SAMPLE_INTERVAL_SECONDS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port (for example, COM5); auto-detect CH340 by default")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    try:
        send_samples(args.port or find_serial_port())
    except KeyboardInterrupt:
        LOGGER.info("Stopped.")
    except (OSError, serial.SerialException, RuntimeError) as error:
        LOGGER.error("%s", error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
