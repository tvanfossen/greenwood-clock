#!/usr/bin/env python3
"""
Greenwood Clock OTA Server & File Management Tool

Simple HTTP server for serving firmware updates to ESP32 devices on local network.
Also provides file management commands to push/pull files to/from device SD card.

Usage:
    # OTA Server Mode
    python tools/ota_server.py [--port PORT] [--firmware PATH]

    # File Management Mode
    python tools/ota_server.py --push <local_file> <device_path> --device <ip>
    python tools/ota_server.py --pull <device_path> <local_file> --device <ip>
    python tools/ota_server.py --list <device_path> --device <ip>

Examples:
    # Start OTA server
    python tools/ota_server.py --port 8000 --firmware build/greenwood-clock.bin

    # Upload splash image to device
    python tools/ota_server.py --push assets/splash.png /splash.png --device 192.168.1.100

    # Download log file from device
    python tools/ota_server.py --pull /sdcard/logs/debug.log ./debug.log --device 192.168.1.100

    # List SD card root directory
    python tools/ota_server.py --list /sdcard --device 192.168.1.100
"""

import http.server
import socketserver
import os
import sys
import argparse
import socket
import json
import urllib.request
import urllib.error

DEFAULT_PORT = 8000
DEFAULT_FIRMWARE_PATH = "build/greenwood-clock.bin"
SERVE_PATH = "/greenwood-clock.bin"
DEVICE_API_PORT = 80


class OTAHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP request handler for OTA firmware updates"""

    firmware_path = DEFAULT_FIRMWARE_PATH

    def do_GET(self):
        """Handle GET requests for firmware"""
        if self.path == SERVE_PATH:
            self.serve_firmware()
        else:
            self.send_error(404, f"Only {SERVE_PATH} is available")

    def do_HEAD(self):
        """Handle HEAD requests for firmware availability check"""
        if self.path == SERVE_PATH:
            if os.path.exists(self.firmware_path):
                self.send_response(200)
                self.send_header("Content-type", "application/octet-stream")
                self.send_header("Content-Length", os.path.getsize(self.firmware_path))
                self.end_headers()
            else:
                self.send_error(404, "Firmware not found")
        else:
            self.send_error(404, f"Only {SERVE_PATH} is available")

    def serve_firmware(self):
        """Serve the firmware file"""
        if os.path.exists(self.firmware_path):
            file_size = os.path.getsize(self.firmware_path)

            self.send_response(200)
            self.send_header("Content-type", "application/octet-stream")
            self.send_header("Content-Length", file_size)
            self.send_header(
                "Content-Disposition", 'attachment; filename="greenwood-clock.bin"'
            )
            self.end_headers()

            with open(self.firmware_path, "rb") as f:
                chunk_size = 4096
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    self.wfile.write(chunk)

            client_ip = self.client_address[0]
            print(f"[INFO] Served firmware ({file_size:,} bytes) to {client_ip}")
        else:
            self.send_error(404, "Firmware not found")
            print(f"[ERROR] Firmware file not found: {self.firmware_path}")

    def log_message(self, format, *args):
        """Override to customize logging"""
        client_ip = self.client_address[0]
        print(f"[{client_ip}] {format % args}")


def get_local_ip():
    """Get the local IP address of this machine"""
    try:
        # Connect to external host to determine local IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        return local_ip
    except Exception:
        return "127.0.0.1"


def push_file(device_ip, local_path, device_path):
    """Upload a file to the device SD card via HTTP POST"""
    if not os.path.exists(local_path):
        print(f"[ERROR] Local file not found: {local_path}")
        return False

    file_size = os.path.getsize(local_path)
    file_size_mb = file_size / (1024 * 1024)

    # Remove leading /sdcard/ if present (API adds it automatically)
    if device_path.startswith("/sdcard/"):
        device_path = device_path[8:]
    elif device_path.startswith("/"):
        device_path = device_path[1:]

    url = f"http://{device_ip}:{DEVICE_API_PORT}/files/{device_path}"

    print("[INFO] Uploading file...")
    print(f"  Local:  {local_path} ({file_size:,} bytes, {file_size_mb:.2f} MB)")
    print(f"  Device: /sdcard/{device_path}")
    print(f"  URL:    {url}")

    try:
        with open(local_path, "rb") as f:
            data = f.read()

        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Content-Type", "application/octet-stream")
        req.add_header("Content-Length", str(len(data)))

        with urllib.request.urlopen(req, timeout=30) as response:
            result = response.read().decode("utf-8")
            print("[SUCCESS] File uploaded successfully")
            print(f"  Response: {result.strip()}")
            return True

    except urllib.error.HTTPError as e:
        print(f"[ERROR] HTTP {e.code}: {e.reason}")
        try:
            error_msg = e.read().decode("utf-8")
            print(f"  Details: {error_msg}")
        except Exception:
            pass
        return False
    except urllib.error.URLError as e:
        print(f"[ERROR] Connection failed: {e.reason}")
        print(f"  Is the device reachable at {device_ip}?")
        return False
    except Exception as e:
        print(f"[ERROR] Upload failed: {e}")
        return False


def pull_file(device_ip, device_path, local_path):
    """Download a file from the device SD card via HTTP GET"""
    # Remove leading /sdcard/ if present (API adds it automatically)
    if device_path.startswith("/sdcard/"):
        device_path = device_path[8:]
    elif device_path.startswith("/"):
        device_path = device_path[1:]

    url = f"http://{device_ip}:{DEVICE_API_PORT}/files/{device_path}"

    print("[INFO] Downloading file...")
    print(f"  Device: /sdcard/{device_path}")
    print(f"  Local:  {local_path}")
    print(f"  URL:    {url}")

    try:
        req = urllib.request.Request(url, method="GET")

        with urllib.request.urlopen(req, timeout=30) as response:
            data = response.read()

            # Create parent directory if needed
            local_dir = os.path.dirname(local_path)
            if local_dir and not os.path.exists(local_dir):
                os.makedirs(local_dir)

            with open(local_path, "wb") as f:
                f.write(data)

            file_size = len(data)
            file_size_mb = file_size / (1024 * 1024)
            print("[SUCCESS] File downloaded successfully")
            print(f"  Size: {file_size:,} bytes ({file_size_mb:.2f} MB)")
            return True

    except urllib.error.HTTPError as e:
        print(f"[ERROR] HTTP {e.code}: {e.reason}")
        if e.code == 404:
            print(f"  File not found on device: /sdcard/{device_path}")
        return False
    except urllib.error.URLError as e:
        print(f"[ERROR] Connection failed: {e.reason}")
        print(f"  Is the device reachable at {device_ip}?")
        return False
    except Exception as e:
        print(f"[ERROR] Download failed: {e}")
        return False


def list_files(device_ip, device_path):
    """List directory contents on the device SD card via HTTP GET"""
    # Remove leading /sdcard/ if present (API adds it automatically)
    if device_path.startswith("/sdcard/"):
        device_path = device_path[8:]
    elif device_path.startswith("/"):
        device_path = device_path[1:]

    # If empty, list root
    if not device_path:
        device_path = ""

    url = f"http://{device_ip}:{DEVICE_API_PORT}/files/{device_path}"

    print(
        f"[INFO] Listing directory: /sdcard/{device_path if device_path else '(root)'}"
    )
    print(f"  URL: {url}")
    print()

    try:
        req = urllib.request.Request(url, method="GET")

        with urllib.request.urlopen(req, timeout=10) as response:
            content_type = response.headers.get("Content-Type", "")

            if "application/json" in content_type:
                # Directory listing
                data = response.read().decode("utf-8")
                listing = json.loads(data)

                if "files" in listing and len(listing["files"]) > 0:
                    print(f"{'Name':<40} {'Type':<10} {'Size':<15}")
                    print("-" * 70)

                    for entry in listing["files"]:
                        name = entry.get("name", "")
                        entry_type = entry.get("type", "file")
                        size = entry.get("size", 0)

                        if entry_type == "dir":
                            size_str = "<DIR>"
                        else:
                            if size < 1024:
                                size_str = f"{size} B"
                            elif size < 1024 * 1024:
                                size_str = f"{size / 1024:.2f} KB"
                            else:
                                size_str = f"{size / (1024 * 1024):.2f} MB"

                        print(f"{name:<40} {entry_type:<10} {size_str:<15}")

                    print()
                    print(f"Total: {len(listing['files'])} items")
                else:
                    print("[INFO] Directory is empty")

                return True
            else:
                # It's a file, not a directory
                print("[ERROR] Path is a file, not a directory")
                print("  Use --pull to download this file")
                return False

    except urllib.error.HTTPError as e:
        print(f"[ERROR] HTTP {e.code}: {e.reason}")
        if e.code == 404:
            print(f"  Path not found on device: /sdcard/{device_path}")
        return False
    except urllib.error.URLError as e:
        print(f"[ERROR] Connection failed: {e.reason}")
        print(f"  Is the device reachable at {device_ip}?")
        return False
    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON response: {e}")
        return False
    except Exception as e:
        print(f"[ERROR] List failed: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="OTA firmware server & file management tool for Greenwood Clock",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start OTA server with default settings
  python tools/ota_server.py

  # Start OTA server on custom port
  python tools/ota_server.py --port 9000

  # Upload splash image to device
  python tools/ota_server.py --push assets/splash.png /splash.png --device 192.168.1.100

  # Download log file from device
  python tools/ota_server.py --pull /logs/debug.log ./debug.log --device 192.168.1.100

  # List SD card root directory
  python tools/ota_server.py --list / --device 192.168.1.100
        """,
    )

    # OTA Server mode arguments
    parser.add_argument(
        "--port",
        "-p",
        type=int,
        default=DEFAULT_PORT,
        help=f"OTA server port (default: {DEFAULT_PORT})",
    )

    parser.add_argument(
        "--firmware",
        "-f",
        type=str,
        default=DEFAULT_FIRMWARE_PATH,
        help=f"Path to firmware binary (default: {DEFAULT_FIRMWARE_PATH})",
    )

    # File management mode arguments
    parser.add_argument(
        "--device",
        "-d",
        type=str,
        help="Device IP address for file management commands",
    )

    parser.add_argument(
        "--push",
        nargs=2,
        metavar=("LOCAL_FILE", "DEVICE_PATH"),
        help="Upload file to device SD card (requires --device)",
    )

    parser.add_argument(
        "--pull",
        nargs=2,
        metavar=("DEVICE_PATH", "LOCAL_FILE"),
        help="Download file from device SD card (requires --device)",
    )

    parser.add_argument(
        "--list",
        type=str,
        metavar="DEVICE_PATH",
        help="List directory on device SD card (requires --device)",
    )

    args = parser.parse_args()

    # Check if file management command is specified
    file_mgmt_mode = args.push or args.pull or args.list

    if file_mgmt_mode:
        # File Management Mode
        if not args.device:
            print("[ERROR] --device is required for file management commands")
            print("\nExample:")
            print(
                "  python tools/ota_server.py --push splash.png /splash.png --device 192.168.1.100"
            )
            sys.exit(1)

        print("=" * 70)
        print("Greenwood Clock File Management Tool")
        print("=" * 70)
        print(f"Device IP: {args.device}")
        print("=" * 70)
        print()

        success = True

        if args.push:
            local_file, device_path = args.push
            success = push_file(args.device, local_file, device_path)
        elif args.pull:
            device_path, local_file = args.pull
            success = pull_file(args.device, device_path, local_file)
        elif args.list:
            success = list_files(args.device, args.list)

        sys.exit(0 if success else 1)

    else:
        # OTA Server Mode (default)
        # Change to project root directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(script_dir)
        os.chdir(project_root)

        # Resolve firmware path
        firmware_path = os.path.abspath(args.firmware)

        # Check if firmware exists
        if not os.path.exists(firmware_path):
            print(f"[ERROR] Firmware file not found: {firmware_path}")
            print("\nPlease build the firmware first:")
            print("  idf.py build")
            sys.exit(1)

        # Get firmware size
        firmware_size = os.path.getsize(firmware_path)
        firmware_size_mb = firmware_size / (1024 * 1024)

        # Set firmware path in handler class
        OTAHandler.firmware_path = firmware_path

        # Get local IP
        local_ip = get_local_ip()

        # Start server
        print("=" * 70)
        print("Greenwood Clock OTA Server")
        print("=" * 70)
        print(f"Firmware: {firmware_path}")
        print(f"Size:     {firmware_size:,} bytes ({firmware_size_mb:.2f} MB)")
        print(f"Port:     {args.port}")
        print(f"Local IP: {local_ip}")
        print()
        print(f"OTA URL:  http://{local_ip}:{args.port}")
        print(f"Endpoint: http://{local_ip}:{args.port}{SERVE_PATH}")
        print("=" * 70)
        print()
        print("Waiting for devices to connect...")
        print("Press Ctrl+C to stop")
        print()

        # Create and start server
        try:
            with socketserver.TCPServer(("", args.port), OTAHandler) as httpd:
                httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n\n[INFO] Server stopped by user")
            sys.exit(0)
        except OSError as e:
            if e.errno == 98:  # Address already in use
                print(f"\n[ERROR] Port {args.port} is already in use")
                print("Try a different port with --port option")
            else:
                print(f"\n[ERROR] {e}")
            sys.exit(1)


if __name__ == "__main__":
    main()
