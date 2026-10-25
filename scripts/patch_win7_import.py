import sys

OLD_NAME = b"GetSystemTimePreciseAsFileTime"
NEW_NAME = b"GetSystemTimeAsFileTime"


def patch(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    changed = 0
    idx = 0
    while True:
        idx = data.find(OLD_NAME, idx)
        if idx < 0:
            break
        end = idx + len(OLD_NAME)
        if end < len(data) and data[end] == 0:
            data[idx:idx + len(NEW_NAME)] = NEW_NAME
            data[idx + len(NEW_NAME):end] = bytes(len(OLD_NAME) - len(NEW_NAME))
            changed += 1
        idx = end

    if changed == 0:
        print("no change needed: " + path)
        return 0

    with open(path, "wb") as f:
        f.write(data)
    print("patched %d import name(s) in %s" % (changed, path))
    return changed


args = sys.argv
if len(args) >= 2:
    patch(args[1])
else:
    print("usage: patch_win7_import.py exe_path")
    sys.exit(2)
