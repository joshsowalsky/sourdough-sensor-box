"""Publish one-minute readings straight to GitHub, viewable from anywhere.

Writes latest.json on the data branch of GITHUB_REPO, in the same format the
PC bridge writes and live.html reads. Each update is a parentless commit
force-moved onto the branch, so the branch never grows a history.

settings.toml needs GITHUB_TOKEN: a fine-grained token with Contents
read/write on that one repo. GITHUB_REPO and GITHUB_BRANCH are optional.
The clock comes from the Date header of GitHub's replies (no NTP needed).
"""
import binascii
import gc
import os
import time
from array import array

MAX_POINTS = 360  # 6 hours at one a minute: what live.html charts
NO_TEMP = -999999
MONTHS = (b"Jan", b"Feb", b"Mar", b"Apr", b"May", b"Jun",
          b"Jul", b"Aug", b"Sep", b"Oct", b"Nov", b"Dec")


def days_from_civil(y, m, d):
    y -= m <= 2
    era = y // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def civil_from_days(z):
    z += 719468
    era = z // 146097
    doe = z - era * 146097
    yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    mp = (5 * doy + 2) // 153
    d = doy - (153 * mp + 2) // 5 + 1
    m = mp + (3 if mp < 10 else -9)
    return yoe + era * 400 + (m <= 2), m, d


def iso(epoch):
    y, m, d = civil_from_days(epoch // 86400)
    s = epoch % 86400
    return "%04d-%02d-%02dT%02d:%02d:%02d+00:00" % (y, m, d, s // 3600, s // 60 % 60, s % 60)


def parse_iso(b):
    """b'2026-11-01T03:36:03+00:00' -> epoch seconds (UTC)."""
    return (days_from_civil(int(b[0:4]), int(b[5:7]), int(b[8:10])) * 86400
            + int(b[11:13]) * 3600 + int(b[14:16]) * 60 + int(b[17:19]))


def parse_http_date(s):
    """'Sun, 01 Nov 2026 04:40:00 GMT' -> epoch seconds."""
    p = s.encode().split()
    h, mi, se = p[4].split(b":")
    return (days_from_civil(int(p[3]), MONTHS.index(p[2]) + 1, int(p[1])) * 86400
            + int(h) * 3600 + int(mi) * 60 + int(se))


def parse_history(data, since):
    """Pull {"t":..,"d":..,"c":..} points out of latest.json bytes without
    building the whole JSON object (too big for this board's memory)."""
    out = []
    i = 0
    while True:
        i = data.find(b'"t":"', i)
        if i < 0:
            return out
        try:
            t = parse_iso(data[i + 5:i + 30])
            j = data.find(b'"d":', i) + 4
            k = data.find(b",", j)
            dv = float(data[j:k])
            j = data.find(b'"c":', k) + 4
            k = data.find(b"}", j)
            cv = data[j:k]
            if t >= since:
                out.append((t, int(round(dv * 100)), NO_TEMP if cv == b"null" else int(round(float(cv) * 100))))
        except (ValueError, IndexError):
            pass
        i += 5


def _num(v):
    return "null" if v == NO_TEMP else "%.2f" % (v / 100)


class Publisher:
    def __init__(self, pool, ssl_context):
        import adafruit_requests
        self.token = os.getenv("GITHUB_TOKEN") or ""
        self.repo = os.getenv("GITHUB_REPO") or "joshsowalsky/sourdough-sensor-box"
        self.branch = os.getenv("GITHUB_BRANCH") or "data"
        self.enabled = self.token.startswith("github_pat_") or self.token.startswith("ghp_")
        self.session = adafruit_requests.Session(pool, ssl_context)
        self.clock = None  # (epoch, monotonic_ns) from GitHub's Date header
        self.loaded = False
        self.t, self.d, self.c = array("l"), array("l"), array("l")
        self.api = "https://api.github.com/repos/" + self.repo

    def _headers(self, accept="application/vnd.github+json"):
        return {"Authorization": "Bearer " + self.token, "Accept": accept,
                "User-Agent": "sourdough-feather", "X-GitHub-Api-Version": "2022-11-28"}

    def _note_clock(self, r):
        for k, v in r.headers.items():
            if k.lower() == "date":
                try:
                    self.clock = (parse_http_date(v), time.monotonic_ns())
                except (ValueError, IndexError):
                    pass

    def now(self):
        return self.clock[0] + (time.monotonic_ns() - self.clock[1]) // 1000000000

    def load(self):
        """Fetch the clock and the recent history already on GitHub."""
        url = "%s/contents/latest.json?ref=%s" % (self.api, self.branch)
        with self.session.get(url, headers=self._headers("application/vnd.github.raw+json"),
                              stream=True) as r:
            self._note_clock(r)
            if r.status_code == 401:
                print("github: token rejected (401)")
                self.enabled = False
                return
            if not self.clock:
                return
            since = self.now() - MAX_POINTS * 60
            if r.status_code == 200:
                # Read in small pieces: the whole file doesn't fit in memory at once.
                carry = b""
                for chunk in r.iter_content(1024):
                    buf = carry + chunk
                    cut = buf.rfind(b"}")
                    if cut < 0:
                        carry = buf
                        continue
                    for t, d, c in parse_history(buf[:cut + 1], since):
                        self.t.append(t)
                        self.d.append(d)
                        self.c.append(c)
                    carry = buf[cut + 1:]
        self._trim()
        self.loaded = True
        print("github: clock set, loaded", len(self.t), "recent points, free", gc.mem_free())

    def _trim(self):
        if len(self.t) > MAX_POINTS:
            self.t, self.d, self.c = self.t[-MAX_POINTS:], self.d[-MAX_POINTS:], self.c[-MAX_POINTS:]

    def add(self, distance_cm, temp_c):
        if not self.loaded:
            self.load()
        if not self.clock:
            return False
        self.t.append(self.now())
        self.d.append(int(round(distance_cm * 100)))
        self.c.append(NO_TEMP if temp_c is None else int(round(temp_c * 100)))
        self._trim()
        return True

    def _pieces(self):
        """latest.json, a piece at a time."""
        n = len(self.t)
        yield ('{"updated":"%s","distance_cm":%s,"temp_c":%s,"history":[' % (
            iso(self.t[-1]), _num(self.d[-1]), _num(self.c[-1]))).encode()
        for i in range(n):
            yield ('{"t":"%s","d":%s,"c":%s}%s' % (
                iso(self.t[i]), _num(self.d[i]), _num(self.c[i]), "," if i < n - 1 else "")).encode()
        yield b"]}"

    def _blob_body(self):
        """The /git/blobs request body with latest.json base64-encoded into one
        buffer sized up front, so the file never sits in memory twice."""
        head, tail = b'{"encoding":"base64","content":"', b'"}'
        size = sum(len(p) for p in self._pieces())
        body = bytearray(len(head) + (size + 2) // 3 * 4 + len(tail))
        body[0:len(head)] = head
        pos = len(head)
        pend = b""
        for p in self._pieces():
            pend += p
            k = len(pend) // 3 * 3
            if k:
                enc = binascii.b2a_base64(pend[:k]).strip()
                body[pos:pos + len(enc)] = enc
                pos += len(enc)
                pend = pend[k:]
        if pend:
            enc = binascii.b2a_base64(pend).strip()
            body[pos:pos + len(enc)] = enc
            pos += len(enc)
        body[pos:pos + len(tail)] = tail
        return body

    def _post(self, path, body=None, json=None):
        with self.session.post(self.api + path, data=body, json=json, headers=self._headers()) as r:
            self._note_clock(r)
            if r.status_code not in (200, 201):
                raise RuntimeError("%s -> HTTP %d %s" % (path, r.status_code, r.text[:120]))
            return r.json()["sha"]

    def publish(self):
        """Write latest.json to the branch. Returns True on success."""
        if not self.t:
            return False
        gc.collect()
        body = self._blob_body()
        blob = self._post("/git/blobs", body=body)
        del body
        gc.collect()
        tree = self._post("/git/trees", json={"tree": [
            {"path": "latest.json", "mode": "100644", "type": "blob", "sha": blob}]})
        commit = self._post("/git/commits", json={"message": "sensor data", "tree": tree, "parents": []})
        url = "%s/git/refs/heads/%s" % (self.api, self.branch)
        with self.session.request("PATCH", url, json={"sha": commit, "force": True},
                                  headers=self._headers()) as r:
            if r.status_code != 200:
                raise RuntimeError("ref -> HTTP %d %s" % (r.status_code, r.text[:120]))
        return True
