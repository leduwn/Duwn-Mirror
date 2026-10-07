import os
import sys
import time
import socket
import struct
import subprocess

def build_mdns_query(service_name):
    header = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0)
    query_parts = []
    for part in service_name.strip(".").split("."):
        query_parts.append(struct.pack("!B", len(part)) + part.encode("utf-8"))
    query_parts.append(b"\x00")
    qtype_qclass = struct.pack("!HH", 12, 1) # PTR, IN
    return header + b"".join(query_parts) + qtype_qclass

def parse_dns_name(data, offset):
    parts = []
    visited = set()
    orig_offset = offset
    stepped = False

    while offset < len(data):
        if offset in visited:
            break
        visited.add(offset)
        length = data[offset]
        if length == 0:
            offset += 1
            break
        elif (length & 0xC0) == 0xC0:
            pointer = struct.unpack("!H", data[offset:offset+2])[0] & 0x3FFF
            if not stepped:
                orig_offset = offset + 2
                stepped = True
            offset = pointer
        else:
            offset += 1
            parts.append(data[offset:offset+length].decode("utf-8", errors="replace"))
            offset += length

    return ".".join(parts), (orig_offset if stepped else offset)

def parse_mdns_packet(data):
    if len(data) < 12:
        return None
    _, flags, qdcount, ancount, nscount, arcount = struct.unpack("!HHHHHH", data[:12])
    offset = 12

    for _ in range(qdcount):
        _, offset = parse_dns_name(data, offset)
        offset += 4

    records = []
    total = ancount + nscount + arcount
    for _ in range(total):
        if offset >= len(data):
            break
        name, offset = parse_dns_name(data, offset)
        if offset + 10 > len(data):
            break
        rtype, rclass, ttl, rdlength = struct.unpack("!HHIH", data[offset:offset+10])
        offset += 10
        rdata = data[offset:offset+rdlength]
        rec = {
            "name": name,
            "type": rtype,
            "ttl": ttl,
            "rdlength": rdlength
        }

        if rtype == 12: # PTR
            ptr_name, _ = parse_dns_name(data, offset)
            rec["ptr"] = ptr_name
        elif rtype == 33: # SRV
            if rdlength >= 6:
                priority, weight, port = struct.unpack("!HHH", rdata[:6])
                target, _ = parse_dns_name(data, offset + 6)
                rec["priority"] = priority
                rec["weight"] = weight
                rec["port"] = port
                rec["target"] = target
        elif rtype == 16: # TXT
            txt_dict = {}
            t_off = 0
            while t_off < rdlength:
                t_len = rdata[t_off]
                t_off += 1
                txt_item = rdata[t_off:t_off+t_len].decode("utf-8", errors="replace")
                t_off += t_len
                if "=" in txt_item:
                    k, v = txt_item.split("=", 1)
                    txt_dict[k] = v
                else:
                    txt_dict[txt_item] = True
            rec["txt"] = txt_dict
        elif rtype == 1 and rdlength == 4: # A
            rec["ipv4"] = socket.inet_ntoa(rdata)
        elif rtype == 28 and rdlength == 16: # AAAA
            rec["ipv6"] = socket.inet_ntop(socket.AF_INET6, rdata)

        records.append(rec)
        offset += rdlength

    return records

def get_uxplay_env(sidecar_dir):
    env = os.environ.copy()
    plugins = os.path.join(sidecar_dir, "plugins")
    libexec = os.path.join(sidecar_dir, "libexec")
    tools = os.path.join(sidecar_dir, "tools")
    scanner = os.path.join(libexec, "gst-plugin-scanner.exe")
    env["PATH"] = f"{sidecar_dir};{plugins};{tools};{libexec};C:\\Windows\\System32;C:\\Windows;{env.get('PATH','')}"
    env["GST_PLUGIN_PATH"] = plugins
    env["GST_PLUGIN_SYSTEM_PATH"] = plugins
    env["GST_PLUGIN_SCANNER"] = scanner
    return env

def get_process_sockets(pid):
    res_tcp = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True)
    tcp_listeners = []
    for line in res_tcp.stdout.splitlines():
        if "LISTENING" in line and str(pid) in line:
            parts = line.split()
            if len(parts) >= 2:
                tcp_listeners.append(parts[1])

    res_udp = subprocess.run(["netstat", "-ano", "-p", "udp"], capture_output=True, text=True)
    udp_endpoints = []
    for line in res_udp.stdout.splitlines():
        if str(pid) in line:
            parts = line.split()
            if len(parts) >= 2:
                udp_endpoints.append(parts[1])
    return tcp_listeners, udp_endpoints

def audit_case(label, port_args):
    sidecar_dir = os.path.abspath(r"runtime\duwn-airplay")
    uxplay_exe = os.path.join(sidecar_dir, "uxplay.exe")
    env = get_uxplay_env(sidecar_dir)

    server_name = f"Duwn_{label}"
    base_args = [
        "-n", server_name,
        "-nh",
        "-d",
        "-nc",
        "-vrtp", "config-interval=1 ! udpsink host=127.0.0.1 port=7010 sync=false",
        "-artp", "pt=96 ! udpsink host=127.0.0.1 port=7011 sync=false"
    ]
    if port_args:
        cmd = [uxplay_exe] + port_args + base_args
    else:
        cmd = [uxplay_exe] + base_args

    print(f"\n================================================================================")
    print(f"CASE: {label}")
    print(f"CMD:  {' '.join(cmd)}")
    print(f"================================================================================")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", 5353))
        mreq = struct.pack("4sl", socket.inet_aton("224.0.0.251"), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        sock.settimeout(0.4)
    except Exception as e:
        print(f"[WARN] Could not bind multicast socket 5353: {e}")
        sock = None

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    time.sleep(1.5)
    pid = proc.pid
    tcp_listen, udp_listen = get_process_sockets(pid)

    captured_records = []
    if sock:
        q_airplay = build_mdns_query("_airplay._tcp.local")
        q_raop = build_mdns_query("_raop._tcp.local")
        for _ in range(3):
            try:
                sock.sendto(q_airplay, ("224.0.0.251", 5353))
                sock.sendto(q_raop, ("224.0.0.251", 5353))
            except Exception:
                pass
            time.sleep(0.15)

        start_t = time.time()
        while time.time() - start_t < 2.5:
            try:
                data, addr = sock.recvfrom(4096)
                recs = parse_mdns_packet(data)
                if recs:
                    for r in recs:
                        r_str = str(r)
                        if server_name.lower() in r_str.lower() or "_airplay" in r_str.lower() or "_raop" in r_str.lower():
                            captured_records.append(r)
            except socket.timeout:
                continue
            except Exception:
                break
        sock.close()

    proc.terminate()
    try:
        stdout, _ = proc.communicate(timeout=2.0)
    except Exception:
        proc.kill()
        stdout, _ = proc.communicate()

    print(f"PID: {pid}")
    print(f"TCP LISTEN sockets: {tcp_listen}")
    print(f"UDP endpoints:      {udp_listen}")

    srv_airplay = [r for r in captured_records if r.get("type") == 33 and "_airplay" in r.get("name", "")]
    srv_raop = [r for r in captured_records if r.get("type") == 33 and "_raop" in r.get("name", "")]
    ptr_records = [r for r in captured_records if r.get("type") == 12]
    txt_records = [r for r in captured_records if r.get("type") == 16]
    a_records = [r for r in captured_records if r.get("type") == 1]
    aaaa_records = [r for r in captured_records if r.get("type") == 28]

    print("\n--- [DNS-SD Discovery Records Captured via Multicast UDP 5353] ---")
    if srv_airplay:
        for s in srv_airplay:
            print(f"  [AirPlay SRV] Service={s['name']} -> Target={s.get('target')} Port={s.get('port')}")
    else:
        print("  [AirPlay SRV] None received during passive window")

    if srv_raop:
        for s in srv_raop:
            print(f"  [RAOP SRV]    Service={s['name']} -> Target={s.get('target')} Port={s.get('port')}")
    else:
        print("  [RAOP SRV]    None received during passive window")

    for p in ptr_records:
        print(f"  [PTR] {p['name']} -> {p.get('ptr')}")

    for t in txt_records:
        print(f"  [TXT] {t['name']}: {t.get('txt')}")

    for a in a_records:
        print(f"  [A Target] {a['name']} -> {a.get('ipv4')}")

    for aaaa in aaaa_records:
        print(f"  [AAAA Target] {aaaa['name']} -> {aaaa.get('ipv6')}")

    print("\n--- [UxPlay Internal Log Output] ---")
    for line in stdout.splitlines():
        lower = line.lower()
        if any(k in lower for k in ["using network ports", "server socket", "advertised", "register_dnssd", "features", "mac", "error", "fail", "fallback"]):
            print(f"  {line}")

    return {
        "label": label,
        "tcp": tcp_listen,
        "udp": udp_listen,
        "srv_airplay": srv_airplay,
        "srv_raop": srv_raop,
        "stdout": stdout
    }

def main():
    print("Starting DNS-SD Discovery & Port Semantics Verification Tool...")
    res_triple = audit_case("Triple7000", ["-p", "7000,7000,7000"])
    res_single = audit_case("Single7000", ["-p", "7000"])
    res_default = audit_case("DefaultNoP", [])

if __name__ == "__main__":
    main()
