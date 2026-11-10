#!/usr/bin/env python3
"""Upload melodies (mp3) to the module's external flash. / Залить мелодии (mp3) во внешнюю флеш модуля.

  WUL_HOST=wul.local python3 tools/upload_melodies.py FOLDER [--format]

FOLDER must contain mp3 files named after the sound slots (see README). Missing files are skipped.
--format first wipes the catalog: re-uploading a file under the same name takes new space until the catalog is wiped.
"""
import json, os, sys, time, urllib.request, uuid

HOST = "http://" + os.environ.get("WUL_HOST", "wul.local")
NAMES = ["chuckoo", "forest_day", "cow_in_alpes", "sea_with_seagulls", "sati", "rooster_and_chickens", "yoga", "buddha2"]


def post_json(path, body, timeout=20):
    req = urllib.request.Request(HOST + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read() or b"{}")


def upload(name, path):
    boundary = uuid.uuid4().hex
    data = open(path, "rb").read()
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"f\"; filename=\"{name}.mp3\"\r\n"
            f"Content-Type: audio/mpeg\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
    req = urllib.request.Request(f"{HOST}/api/upload?name={name}", data=body,
                                 headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=600) as r:
        return r.read().decode()


def main():
    args = [a for a in sys.argv[1:] if a != "--format"]
    if len(args) != 1 or not os.path.isdir(args[0]):
        sys.exit(__doc__)
    if "--format" in sys.argv:
        print("catalog wiped:", post_json("/api/melodies", {"format": True, "confirm": True}))
    for name in NAMES:
        path = os.path.join(args[0], name + ".mp3")
        if not os.path.exists(path):
            print(f"{name}: no {name}.mp3 — skipped")
            continue
        t = time.time()
        print(f"{name}: {upload(name, path)}  ({time.time() - t:.0f} s)")
    print(post_json("/api/melodies", {}))


if __name__ == "__main__":
    main()
