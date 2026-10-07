import os
import sys
import subprocess
import time
import socket

def get_uxplay_env(sidecar_dir):
    env = os.environ.copy()
    plugins = os.path.join(sidecar_dir, "plugins")
    libexec = os.path.join(sidecar_dir, "libexec")
    tools = os.path.join(sidecar_dir, "tools")
    scanner = os.path.join(libexec, "gst-plugin-scanner.exe")

    orig_path = env.get("PATH", "")
    env["PATH"] = f"{sidecar_dir};{plugins};{tools};{libexec};C:\\Windows\\System32;C:\\Windows;{orig_path}"
    env["GST_PLUGIN_PATH"] = plugins
    env["GST_PLUGIN_SYSTEM_PATH"] = plugins
    env["GST_PLUGIN_SCANNER"] = scanner
    return env

def get_open_listeners(pid):
    # Check TCP listeners using netstat
    res = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True)
    tcp_ports = []
    for line in res.stdout.splitlines():
        if "LISTENING" in line and str(pid) in line:
            parts = line.split()
            if len(parts) >= 2:
                tcp_ports.append(parts[1])

    # Check UDP endpoints
    res_udp = subprocess.run(["netstat", "-ano", "-p", "udp"], capture_output=True, text=True)
    udp_ports = []
    for line in res_udp.stdout.splitlines():
        if str(pid) in line:
            parts = line.split()
            if len(parts) >= 2:
                udp_ports.append(parts[1])

    return tcp_ports, udp_ports

def run_test_case(name, extra_args):
    sidecar_dir = os.path.abspath(r"runtime\duwn-airplay")
    uxplay_exe = os.path.join(sidecar_dir, "uxplay.exe")
    env = get_uxplay_env(sidecar_dir)

    cmd = [uxplay_exe] + extra_args + ["-n", f"Test_{name}", "-nh", "-d"]
    print(f"\n========================================================")
    print(f"CASE: {name}")
    print(f"CMD:  {' '.join(cmd)}")
    print(f"========================================================")

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    time.sleep(2.0)
    pid = proc.pid
    tcp_ports, udp_ports = get_open_listeners(pid)

    # Terminate process
    proc.terminate()
    try:
        stdout, _ = proc.communicate(timeout=3.0)
    except Exception:
        proc.kill()
        stdout, _ = proc.communicate()

    print(f"PID: {pid}")
    print(f"Active TCP Listeners: {tcp_ports}")
    print(f"Active UDP Endpoints: {udp_ports}")
    print("\n[UxPlay Output Summary]")
    for line in stdout.splitlines():
        lower = line.lower()
        if any(k in lower for k in ["using network ports", "port", "airplay", "raop", "dnssd", "bind", "listen", "error", "fail", "warning"]):
            print(f"  {line}")

def main():
    run_test_case("Case1_MinusP_7000", ["-p", "7000"])
    run_test_case("Case2_MinusP_Triple7000", ["-p", "7000,7000,7000"])
    run_test_case("Case3_MinusP_7000_7001_7002", ["-p", "7000,7001,7002"])
    run_test_case("Case4_Default_NoP", [])

if __name__ == "__main__":
    main()
