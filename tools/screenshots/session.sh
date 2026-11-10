#!/bin/bash
# Starts a minimal TDE session (Xvfb + dcopserver/tdeinit + TWin) on $DISPLAY.
# Used by take-screenshots.sh inside the tools/screenshots container.
export DISPLAY=${DISPLAY:-:1}
export PATH=/opt/trinity/bin:$PATH
Xvfb "$DISPLAY" -screen 0 ${SCREEN_SIZE:-1280x800x24} -nolisten tcp >/tmp/xvfb.log 2>&1 &
for i in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
eval "$(dbus-launch --sh-syntax)"
tdeinit >/tmp/tdeinit.log 2>&1
twin >/tmp/twin.log 2>&1 &
sleep 2
