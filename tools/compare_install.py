import os
import hashlib

def file_hash(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

installed_dir = r"C:\Program Files\Duwn Mirror"
build_dir = r"D:\Projects\Duwn Mirror\build-msvc\bin\Release"

mismatches = []
missing_in_install = []
missing_in_build = []

for root, dirs, files in os.walk(installed_dir):
    for file in files:
        if file.startswith("unins000"):
            continue
        inst_path = os.path.join(root, file)
        rel_path = os.path.relpath(inst_path, installed_dir)
        
        # Determine build path
        if rel_path in ["LICENSE", "THIRD_PARTY_NOTICES.txt", "UxPlay-GPL-3.0.txt"]:
            b_path = os.path.join(r"D:\Projects\Duwn Mirror", rel_path)
        elif rel_path.startswith("tools\\"):
            b_path = os.path.join(r"D:\Projects\Duwn Mirror", rel_path)
        else:
            b_path = os.path.join(build_dir, rel_path)
            
        if not os.path.exists(b_path):
            missing_in_build.append((rel_path, inst_path))
        else:
            h1 = file_hash(inst_path)
            h2 = file_hash(b_path)
            if h1 != h2:
                mismatches.append((rel_path, h1, h2))

print(f"Mismatches: {len(mismatches)}")
for m in mismatches:
    print(f"  Mismatch: {m[0]}")
print(f"Missing in build: {len(missing_in_build)}")
for m in missing_in_build:
    print(f"  Missing: {m[0]}")
