import os

installed_dir = r"C:\Program Files\Duwn Mirror"
build_dir = r"D:\Projects\Duwn Mirror\build-msvc\bin\Release"

not_installed = []
for root, dirs, files in os.walk(build_dir):
    for file in files:
        b_path = os.path.join(root, file)
        rel_path = os.path.relpath(b_path, build_dir)
        if rel_path.startswith("%SystemDrive%"):
            continue
        inst_path = os.path.join(installed_dir, rel_path)
        if not os.path.exists(inst_path):
            not_installed.append(rel_path)

print(f"Files in build but not installed: {len(not_installed)}")
for f in not_installed:
    print(f"  {f}")
