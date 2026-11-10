#!/usr/bin/env python3
"""
A stand-in for the services tde-ai-client talks to, used to take the
screenshots and to try the program without a GPU, an account or a network.

It imitates, with canned answers:
  * the AI Horde          at http://127.0.0.1:PORT/api/v2
  * KoboldCpp             at http://127.0.0.1:PORT
  * an OpenAI-style API   at http://127.0.0.1:PORT/v1

Only the Python standard library is used.  Every request is logged to
stderr with what it contained (attachment kinds and sizes), which is how
the payloads sent by the client were checked.

Environment:
  MOCK_PORT   port to listen on (default 5001)
  MOCK_DELAY  seconds between streamed words (default 0.04)
  MOCK_QUEUE  how many status polls a Horde job stays queued (default 2)
"""

import base64
import json
import math
import os
import struct
import sys
import threading
import time
import uuid
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("MOCK_PORT", "5001"))
DELAY = float(os.environ.get("MOCK_DELAY", "0.04"))
QUEUE = int(os.environ.get("MOCK_QUEUE", "2"))

TEXT_MODELS = [
    ("koboldcpp/L3-8B-Stheno-v3.2", 14), ("koboldcpp/Fimbulvetr-11B-v2", 9),
    ("koboldcpp/mistral-nemo-instruct-2407", 7), ("aphrodite/meta-llama/Llama-3.1-8B-Instruct", 5),
    ("koboldcpp/gemma-3-12b-it", 4), ("koboldcpp/Qwen2.5-14B-Instruct", 2),
]
IMAGE_MODELS = [
    ("AlbedoBase XL (SDXL)", 21), ("Deliberate", 17), ("Dreamshaper", 12), ("stable_diffusion", 11),
    ("Juggernaut XL", 8), ("Flux.1-Schnell fp8 (Compact)", 6), ("Anything Diffusion", 3),
]
OPENAI_MODELS = ["gpt-4o", "gpt-4o-mini", "gpt-4o-audio-preview", "gpt-image-1", "dall-e-3",
                 "tts-1", "tts-1-hd", "whisper-1", "o4-mini"]

jobs = {}
jobs_lock = threading.Lock()


def log(*args):
    print(time.strftime("%H:%M:%S"), *args, file=sys.stderr, flush=True)


# ----------------------------------------------------------------- media

def png(width, height, pixel):
    """A PNG image; pixel(x, y) returns (r, g, b)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def landscape(width=512, height=512, seed=0):
    """A lighthouse on a cliff at sunset - stands in for a generated image."""
    horizon = int(height * 0.62)
    sun_x, sun_y, sun_r = int(width * (0.30 + 0.05 * (seed % 3))), int(height * 0.50), width * 0.085

    def ridge(x, base, amp, freq, phase):
        return base - amp * (math.sin(x * freq + phase) * 0.6 + math.sin(x * freq * 2.3 + phase * 1.7) * 0.4)

    def mix(a, b, t):
        t = max(0.0, min(1.0, t))
        return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

    def pixel(x, y):
        # sky
        t = y / horizon
        if t < 0.5:
            c = mix((28, 30, 78), (150, 62, 120), t * 2)
        else:
            c = mix((150, 62, 120), (255, 170, 80), (t - 0.5) * 2)
        d = math.hypot(x - sun_x, y - sun_y)
        if d < sun_r:
            c = (255, 244, 200)
        elif d < sun_r * 3.2:
            c = mix((255, 226, 150), c, (d - sun_r) / (sun_r * 2.2))
        # sea
        if y >= horizon:
            depth = (y - horizon) / (height - horizon)
            c = mix((230, 130, 90), (20, 30, 70), depth ** 0.6)
            glint = abs(x - sun_x) < (6 + depth * 60) and (y * 7 + int(math.sin(y * 0.9) * 9) + x) % 9 < 4
            if glint:
                c = mix(c, (255, 220, 150), 0.65)
        # far hills
        if y > ridge(x, horizon - height * 0.02, height * 0.035, 0.021, 1.0 + seed) and y < horizon:
            c = (70, 40, 90)
        # cliff on the right
        cliff = ridge(x, height * 0.80, height * 0.03, 0.03, 2.0) - max(0, x - width * 0.45) * 0.42
        if x > width * 0.40 and y > cliff:
            c = (24, 18, 38)
        # lighthouse
        lx, top, base = int(width * 0.78), int(height * 0.27), int(height * 0.64)
        if top <= y <= base:
            half = 7 + (y - top) * 9 // (base - top)
            if abs(x - lx) <= half:
                band = ((y - top) * 5 // (base - top)) % 2
                c = (226, 222, 214) if band == 0 else (170, 52, 48)
        if top - 16 <= y < top and abs(x - lx) <= 10:
            c = (255, 240, 170) if y > top - 12 else (30, 24, 40)
        if top - 26 <= y < top - 16 and abs(x - lx) <= (y - (top - 26)):
            c = (30, 24, 40)
        return c

    return png(width, height, pixel)


def wav(seconds=2.4, rate=22050):
    """A short melody as a WAV file - stands in for synthesised speech."""
    notes = [392.0, 440.0, 523.25, 440.0, 392.0, 329.63]
    frames = bytearray()
    total = int(seconds * rate)
    for i in range(total):
        note = notes[min(len(notes) - 1, i * len(notes) // total)]
        pos = (i * len(notes) / total) % 1.0
        env = min(1.0, pos * 12) * (1.0 - pos) ** 0.5
        v = math.sin(2 * math.pi * note * i / rate) * 0.5 + math.sin(4 * math.pi * note * i / rate) * 0.15
        frames += struct.pack("<h", int(v * env * 20000))
    header = (b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVEfmt "
              + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(frames)))
    return header + bytes(frames)


# --------------------------------------------------------------- answers

GENERIC = """The **Trinity Desktop Environment** (TDE) is a continuation of KDE 3.5. It keeps the classic desktop layout and stays light on memory, which is why it is popular on older hardware.

A few things that make it pleasant to use:

- **Kicker**, the panel, with the TDE menu, task bar and system tray
- **Konqueror** as file manager and web browser in one
- **KHelpCenter**, where every program installs its handbook

You can check which version is installed from a terminal:

```
tde-config --version
```

Would you like a short list of useful keyboard shortcuts as well?"""

SHORTCUTS = """Here are some shortcuts that work out of the box:

- `Alt+F1` opens the TDE menu
- `Alt+F2` opens the *Run Command* dialog
- `Ctrl+Alt+Esc` lets you click on a window to end a frozen program
- `Alt+Tab` walks through the open windows
- `Ctrl+F1` ... `Ctrl+F4` switch between the virtual desktops

All of them can be changed in the Control Centre under **Regional & Accessibility - Keyboard Shortcuts**."""

IMAGE_ANSWER = """The picture shows a **lighthouse on a dark cliff at sunset**. The tower has red and white bands and its lamp is lit. The sun sits low over the sea on the left and leaves a bright, broken reflection on the water; a line of purple hills marks the horizon.

The mood is calm - warm orange near the horizon fading into deep blue at the top of the sky."""

FILE_ANSWER = """I read the attached file. In short:

- it is a **shell script** that makes a dated backup of a directory with `tar`
- old archives are removed after 14 days by the `find ... -mtime +14 -delete` line
- there is no check that the target directory exists

A small improvement would be to stop early if the target is missing:

```
[ -d "$TARGET" ] || { echo "no such directory: $TARGET" >&2; exit 1; }
```"""

AUDIO_ANSWER = """Thanks for the recording. You are asking how to make the panel hide by itself: right-click an empty part of the panel, choose **Configure Panel...**, go to **Hiding** and select *Hide automatically*."""

HORDE_ANSWER = """Volunteers all over the world run text and image models on their own computers and lend the spare time to the AI Horde. Your request goes into a queue; a worker that has the right model picks it up, does the work and sends back the result.

Using it is free. People who run a worker earn *kudos*, and requests from accounts with kudos are served first - that is why anonymous requests can take a little longer."""


def content_summary(messages):
    """What the last user message contained, for the log and to pick an answer."""
    kinds, text = [], ""
    for m in messages:
        if m.get("role") != "user":
            continue
        kinds, text = [], ""
        c = m.get("content")
        if isinstance(c, str):
            text = c
            continue
        for part in c or []:
            t = part.get("type")
            if t == "text":
                text += part.get("text", "")
            elif t == "image_url":
                kinds.append("image(%d b64 chars)" % len(part["image_url"]["url"]))
            elif t == "input_audio":
                kinds.append("audio/%s(%d b64 chars)" % (part["input_audio"].get("format"), len(part["input_audio"]["data"])))
            elif t == "video_url":
                kinds.append("video(%d b64 chars)" % len(part["video_url"]["url"]))
            elif t == "file":
                kinds.append("file %s(%d b64 chars)" % (part["file"].get("filename"), len(part["file"]["file_data"])))
            else:
                kinds.append(str(t))
    return kinds, text


def pick_answer(kinds, text):
    low = text.lower()
    if any(k.startswith("image") for k in kinds) or "attached image" in low:
        return IMAGE_ANSWER
    if any(k.startswith("audio") for k in kinds) or "transcript of the audio" in low:
        return AUDIO_ANSWER
    if any(k.startswith("video") for k in kinds):
        return "The clip shows a slow pan over the same coastline; the lighthouse lamp turns twice in the ten seconds."
    if any(k.startswith("file") for k in kinds):
        return "The document has two pages: an invoice and its terms of payment. The total is 148.50 EUR, due within 30 days."
    if 'file "' in low:
        return FILE_ANSWER
    if "shortcut" in low:
        return SHORTCUTS
    if "horde" in low:
        return HORDE_ANSWER
    return GENERIC


# ---------------------------------------------------------------- server

class Handler(BaseHTTPRequestHandler):
    server_version = "mock-ai/1.0"

    def log_message(self, fmt, *args):
        pass

    # -- helpers
    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def send_json(self, obj, status=200):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_bytes(self, data, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def unauthorised(self):
        auth = self.headers.get("Authorization", "")
        if auth == "Bearer wrong-key":
            self.send_json({"error": {"message": "Incorrect API key provided.", "type": "invalid_request_error"}}, 401)
            return True
        return False

    # -- GET
    def do_GET(self):
        path, _, query = self.path.partition("?")
        log("GET", self.path)

        if path == "/api/v2/status/models":
            models = IMAGE_MODELS if "type=image" in query else TEXT_MODELS
            self.send_json([{"name": n, "count": c, "queued": 0, "eta": 0, "type": "x"} for n, c in models])
        elif path == "/api/v2/find_user":
            if self.headers.get("apikey", "0000000000") == "0000000000":
                self.send_json({"username": "Anonymous#0", "kudos": 0})
            else:
                self.send_json({"username": "trinity#4242", "kudos": 15230})
        elif path.startswith("/api/v2/generate/text/status/") or path.startswith("/api/v2/generate/check/"):
            self.horde_status(path.rsplit("/", 1)[1], full=path.startswith("/api/v2/generate/text/"))
        elif path.startswith("/api/v2/generate/status/"):
            self.horde_status(path.rsplit("/", 1)[1], full=True)
        elif path.startswith("/api/v2/interrogate/status/"):
            job = jobs.get(path.rsplit("/", 1)[1])
            if not job:
                return self.send_json({"message": "Interrogation not found."}, 404)
            job["polls"] += 1
            if job["polls"] <= QUEUE:
                return self.send_json({"state": "waiting", "forms": [{"form": "caption", "state": "waiting"}]})
            self.send_json({"state": "done", "forms": [{"form": "caption", "state": "done", "result": {
                "caption": "a lighthouse with red and white bands on a dark cliff, the sun setting over the sea"}}]})
        elif path.startswith("/media/"):
            self.send_bytes(landscape(seed=len(path)), "image/png")
        elif path == "/api/extra/version":
            self.send_json({"result": "KoboldCpp", "version": "1.98.1", "protected": False, "txt2img": True,
                            "vision": True, "audio": False, "transcribe": True, "multiplayer": False,
                            "websearch": False, "tts": True, "embeddings": False})
        elif path == "/api/v1/model":
            self.send_json({"result": "koboldcpp/gemma-3-12b-it-Q4_K_M"})
        elif path == "/sdapi/v1/sd-models":
            self.send_json([{"title": "sd_xl_base_1.0", "model_name": "sd_xl_base_1.0"}])
        elif path == "/v1/models":
            if self.unauthorised():
                return
            self.send_json({"object": "list", "data": [{"id": m, "object": "model"} for m in OPENAI_MODELS]})
        else:
            self.send_json({"message": "not found: " + path}, 404)

    def horde_status(self, job_id, full):
        with jobs_lock:
            job = jobs.get(job_id)
            if not job:
                return self.send_json({"message": "Request with ID '%s' not found." % job_id}, 404)
            job["polls"] += 1
            polls = job["polls"]
        if polls <= QUEUE:
            return self.send_json({"done": False, "faulted": False, "is_possible": True, "processing": 0,
                                   "waiting": 1, "finished": 0, "queue_position": QUEUE - polls + 1,
                                   "wait_time": (QUEUE - polls + 1) * 6, "generations": []})
        if polls == QUEUE + 1 and job["kind"] == "text":
            return self.send_json({"done": False, "faulted": False, "is_possible": True, "processing": 1,
                                   "waiting": 0, "finished": 0, "queue_position": 0, "wait_time": 4,
                                   "generations": []})
        out = {"done": True, "faulted": False, "is_possible": True, "processing": 0, "waiting": 0,
               "finished": 1, "queue_position": 0, "wait_time": 0, "generations": []}
        if full:
            if job["kind"] == "text":
                out["generations"] = [{"text": " " + job["answer"] + "\n### Instruction:\n", "model": job["model"],
                                       "worker_name": "mock worker", "state": "ok"}]
            else:
                out["generations"] = [{"img": "http://127.0.0.1:%d/media/%s.png" % (PORT, job_id),
                                       "model": job["model"], "worker_name": "mock worker",
                                       "censored": False, "state": "ok"}]
        self.send_json(out)

    # -- DELETE
    def do_DELETE(self):
        log("DELETE", self.path)
        jobs.pop(self.path.rsplit("/", 1)[1], None)
        self.send_json({"done": False, "generations": []})

    # -- POST
    def do_POST(self):
        path = self.path.partition("?")[0]
        raw = self.body()
        ctype = self.headers.get("Content-Type", "")
        data = {}
        if "json" in ctype:
            try:
                data = json.loads(raw.decode("utf-8"))
            except ValueError as e:
                log("POST", path, "INVALID JSON:", e)
                return self.send_json({"error": {"message": "invalid JSON: %s" % e}}, 400)

        if path == "/api/v2/generate/text/async":
            prompt = data.get("prompt", "")
            model = (data.get("models") or [TEXT_MODELS[0][0]])[0]
            log("POST", path, "model=%s params=%s prompt=%r" % (model, data.get("params"), prompt[-160:]))
            last = prompt.rsplit("### Instruction:", 1)[-1]
            job_id = str(uuid.uuid4())
            jobs[job_id] = {"kind": "text", "polls": 0, "model": model, "answer": pick_answer([], last)}
            self.send_json({"id": job_id, "kudos": 12.0}, 202)
        elif path == "/api/v2/generate/async":
            model = (data.get("models") or [IMAGE_MODELS[0][0]])[0]
            log("POST", path, "model=%s params=%s prompt=%r source_image=%s" % (
                model, data.get("params"), data.get("prompt"),
                "%d b64 chars (%s)" % (len(data["source_image"]), data.get("source_processing"))
                if "source_image" in data else "none"))
            job_id = str(uuid.uuid4())
            jobs[job_id] = {"kind": "image", "polls": 0, "model": model}
            self.send_json({"id": job_id, "kudos": 8.0}, 202)
        elif path == "/api/v2/interrogate/async":
            log("POST", path, "forms=%s source_image=%d b64 chars" % (data.get("forms"), len(data.get("source_image", ""))))
            job_id = str(uuid.uuid4())
            jobs[job_id] = {"kind": "caption", "polls": 0}
            self.send_json({"id": job_id}, 202)
        elif path in ("/v1/chat/completions",):
            if self.unauthorised():
                return
            kinds, text = content_summary(data.get("messages", []))
            log("POST", path, "model=%s stream=%s messages=%d roles=%s attachments=%s text=%r" % (
                data.get("model"), data.get("stream"), len(data.get("messages", [])),
                [m.get("role") for m in data.get("messages", [])], kinds, text[:200]))
            self.stream_chat(data.get("model") or "mock-model", pick_answer(kinds, text), data.get("stream"))
        elif path == "/api/extra/abort":
            log("POST", path)
            self.send_json({"success": True})
        elif path == "/api/extra/transcribe":
            log("POST", path, "audio_data=%d chars" % len(data.get("audio_data", "")))
            time.sleep(0.6)
            self.send_json({"text": "How do I make the panel hide automatically?"})
        elif path == "/v1/audio/transcriptions":
            if self.unauthorised():
                return
            log("POST", path, "content-type=%s bytes=%d has_file=%s has_model=%s" % (
                ctype.split(";")[0], len(raw), b'name="file"' in raw, b'name="model"' in raw))
            self.send_json({"text": "How do I make the panel hide automatically?"})
        elif path in ("/api/extra/tts", "/v1/audio/speech"):
            log("POST", path, "voice=%s model=%s input=%r" % (data.get("voice"), data.get("model"), data.get("input", "")[:80]))
            time.sleep(0.8)
            self.send_bytes(wav(), "audio/wav")
        elif path in ("/sdapi/v1/txt2img", "/sdapi/v1/img2img"):
            log("POST", path, "%sx%s steps=%s init_images=%s prompt=%r" % (
                data.get("width"), data.get("height"), data.get("steps"),
                len(data.get("init_images", [])), data.get("prompt")))
            time.sleep(1.5)
            img = landscape(seed=len(data.get("prompt", "")))
            self.send_json({"images": [base64.b64encode(img).decode()], "parameters": {}, "info": ""})
        elif path == "/v1/images/generations":
            if self.unauthorised():
                return
            log("POST", path, "model=%s size=%s prompt=%r" % (data.get("model"), data.get("size"), data.get("prompt")))
            time.sleep(1.5)
            img = landscape(seed=len(data.get("prompt", "")))
            self.send_json({"created": int(time.time()), "data": [{"b64_json": base64.b64encode(img).decode()}]})
        else:
            log("POST", path, "-> 404")
            self.send_json({"message": "not found: " + path}, 404)

    def stream_chat(self, model, answer, stream):
        if not stream:
            return self.send_json({"model": model, "choices": [{"index": 0, "finish_reason": "stop",
                                   "message": {"role": "assistant", "content": answer}}]})
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        pieces = []
        for line in answer.split("\n"):
            words = line.split(" ")
            pieces += [w + " " for w in words[:-1]] + [words[-1] + "\n"]
        pieces[-1] = pieces[-1].rstrip("\n")
        try:
            for piece in pieces:
                chunk = {"id": "chatcmpl-mock", "object": "chat.completion.chunk", "model": model,
                         "choices": [{"index": 0, "delta": {"content": piece}, "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
                self.wfile.flush()
                time.sleep(DELAY)
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            log("client closed the stream")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--write-image":
        open(sys.argv[2], "wb").write(landscape())
        sys.exit(0)
    if len(sys.argv) > 2 and sys.argv[1] == "--write-audio":
        open(sys.argv[2], "wb").write(wav())
        sys.exit(0)
    log("mock AI services on http://127.0.0.1:%d" % PORT)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
