"""Runs instead of code.py when CircuitPython starts in safe mode: after a
crash, a brownout or a watchdog timeout. On the kitchen charger nobody is
there to press reset, so leave a note saying why and restart into code.py,
which reports the note to the PC.

The note lives in the last 64 bytes of microcontroller.nvm (wifisetup.py
keeps its networks at the start).
"""
import microcontroller
import supervisor
import time

reason = supervisor.runtime.safe_mode_reason
print("safe mode:", reason)
# USER means someone held the button at startup to get safe mode on purpose.
if reason != supervisor.SafeModeReason.USER:
    tag = ("safe mode " + str(reason))[:63].encode()
    off = len(microcontroller.nvm) - 64
    microcontroller.nvm[off:off + 64] = tag + bytes(64 - len(tag))
    time.sleep(5)
    microcontroller.reset()
