#!/bin/bash
#
# Builds tde-ai-client and takes the screenshots in screenshots/ and doc/en/.
# Runs inside the tools/screenshots Docker image:
#
#   docker build -t tde-ai-client-build tools/
#   docker build -t tde-ai-client-shots -f tools/screenshots/Dockerfile tools/screenshots
#   docker run --rm -v "$PWD":/src tde-ai-client-shots /src/tools/screenshots/take-screenshots.sh
#
# NOTE: the program is real, the AI is not.  So that the screenshots can be
# made anywhere, without a GPU, an account or a network, and come out the
# same every time, tde-ai-client talks to tools/screenshots/mock-server.py,
# which imitates the AI Horde, KoboldCpp and an OpenAI-style service with
# canned answers.  The "generated" picture is drawn by that script and the
# "speech" is a short melody.

set -e
SRC=${SRC:-/src}
OUT_SHOTS=$SRC/screenshots
OUT_DOC=$SRC/doc/en
WORK=/tmp/shots
DEMO=$WORK/Documents
MOCK=$SRC/tools/screenshots/mock-server.py
mkdir -p "$WORK" "$DEMO" "$OUT_SHOTS"

# ---- build & install -------------------------------------------------------
"$SRC/tools/build.sh"

# ---- demo files and mock services --------------------------------------------
python3 "$MOCK" --write-image "$DEMO/lighthouse.png"
python3 "$MOCK" --write-audio "$DEMO/question.wav"
cat > "$DEMO/backup.sh" <<'DEMOFILE'
#!/bin/sh
# nightly backup of /home
TARGET=/srv/backup
tar czf "$TARGET/home-$(date +%F).tar.gz" /home
find "$TARGET" -name 'home-*.tar.gz' -mtime +14 -delete
DEMOFILE

MOCK_DELAY=0.05 MOCK_QUEUE=3 python3 "$MOCK" 2>"$WORK/mock.log" &
MOCK_PID=$!
trap 'kill $MOCK_PID 2>/dev/null || true' EXIT

# ---- session ------------------------------------------------------------------
export SCREEN_SIZE=1280x800x24
source "$SRC/tools/screenshots/session.sh"

rm -rf ~/.trinity/share/apps/tde-ai-client
mkdir -p ~/.trinity/share/config
cat > ~/.trinity/share/config/tde-ai-clientrc <<CFG
[General]
SystemPrompt=You are a helpful assistant on a Trinity desktop. Answer briefly.

[AI Horde]
Url=http://127.0.0.1:5001/api/v2
ApiKey=demo-key-for-screenshots

[KoboldCpp]
Url=http://127.0.0.1:5001

[OpenAI]
Url=http://127.0.0.1:5001/v1
ApiKey=sk-demo-key-for-screenshots

[Layout]
Splitter=210,880

[MainWindow]
Height 800=740
Width 1280=1100
CFG

app() { dcop "$(dcop | grep '^tde-ai-client' | head -1)" tde-ai-client-mainwindow "$@"; }
shot() { sleep "${2:-1}"; import -window root "$OUT_SHOTS/$1.png"; echo "took $1.png"; }
win() { xdotool search --sync --onlyvisible --name "$1" | head -1; }
key() { xdotool key --delay 120 "$@"; }
say() { xdotool type --delay 25 "$1"; }

placewin() {
    local w; w=$(win "TDE AI Client")
    xdotool windowmove "$w" 60 20 windowsize "$w" 1100 740
    xdotool windowactivate --sync "$w" 2>/dev/null || true
    sleep 0.5
}

start() { tde-ai-client "$@" >>"$WORK/app.log" 2>&1 & sleep 3; placewin; }
quit() { pkill -x tde-ai-client || true; sleep 1.5; }

# 1. a chat with KoboldCpp; the second answer is caught while it is written
start --backend koboldcpp --prompt "What is the Trinity Desktop Environment?"
shot main-window 8
say "Which keyboard shortcuts should I know?"; key Return
shot chat-streaming 2.2
sleep 6

# 2. the model list of the AI Horde
app activateAction backend_horde >/dev/null
sleep 2.5
w=$(win "TDE AI Client"); eval "$(xdotool getwindowgeometry --shell "$w")"
xdotool mousemove $((X + 648)) $((Y + 46)) click 1
shot model-selector 1.2
key Escape
quit

# 3. attachments: before sending, and the answer
start --backend koboldcpp "$DEMO/lighthouse.png" "$DEMO/backup.sh" "$DEMO/question.wav"
say "Here is a photo, my backup script and a voice memo. What is in the photo?"
shot attachments 1
key Return
shot vision 9
quit

# 4. image generation with the AI Horde: in the queue, then the picture
start --backend horde --mode image --prompt "a lighthouse on a cliff at sunset, oil painting"
shot horde-queue 1.5
shot image-generation 12
quit

# 5. text to speech with KoboldCpp
start --backend koboldcpp --mode speech --prompt "Good morning. This is your Trinity desktop speaking."
shot text-to-speech 4

# 6. settings dialog (all pages)
app activateAction options_configure >/dev/null &
sleep 3
d=$(win "Configure")
xdotool windowmove "$d" 300 140
sleep 0.5
eval "$(xdotool getwindowgeometry --shell "$d")"
shot settings-general 1
page() { xdotool mousemove $((X + 58)) $((Y + 70 + 62 * $1)) click 1; }
page 1; shot settings-horde 1
page 2; shot settings-koboldcpp 1
page 3; shot settings-openai 1
page 4; shot settings-generation 1
key Escape
sleep 1
quit

# 7. handbook in KHelpCenter
khelpcenter help:/tde-ai-client/index.html >"$WORK/khc.log" 2>&1 &
sleep 8
h=$(win "Help Center" || true)
[ -n "$h" ] && xdotool windowmove "$h" 40 10 windowsize "$h" 1200 770
shot handbook 4
pkill khelpcenter || true

# copies for the handbook
for f in main-window attachments image-generation settings-horde settings-koboldcpp \
         settings-openai settings-generation; do
    cp "$OUT_SHOTS/$f.png" "$OUT_DOC/$f.png"
done
chown -R --reference="$SRC" "$OUT_SHOTS" "$OUT_DOC" 2>/dev/null || true
echo done
