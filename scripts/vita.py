"""Stage Best Park in the Universe through VitaShell FTP and collect development logs."""
import argparse
from datetime import datetime, timezone
from ftplib import FTP, error_perm
import hashlib
import json
import os
from pathlib import Path
import socket
import time

ROOT = Path(__file__).resolve().parents[1]
TITLE = "BPARK0001"
REMOTE_DATA = "ux0:/data/bestpark"


def connect(host):
    ftp = FTP()
    ftp.connect(host, 1337, timeout=20)
    ftp.login()
    ftp.voidcmd("TYPE I")
    return ftp


def mkdir(ftp, path):
    try:
        ftp.mkd(path)
    except error_perm:
        previous = ftp.pwd()
        ftp.cwd(path)  # Only suppress an error if the directory actually exists.
        ftp.cwd(previous)


def size(ftp, remote):
    try:
        return ftp.size(remote)
    except error_perm as exc:
        if str(exc).startswith("550"):
            return None
        raise


def upload(ftp, local, remote, resume=False):
    expected = local.stat().st_size
    if resume and size(ftp, remote) == expected:
        print(f"Already staged: {local.name} ({expected:,} bytes)", flush=True)
        return
    temporary = remote + ".part"
    offset = (size(ftp, temporary) or 0) if resume else 0
    if offset > expected:
        raise RuntimeError(f"Unexpected oversized partial file: {temporary}")
    sent = offset
    started = time.monotonic()
    last_report = started - 30
    def progress(block):
        nonlocal sent, last_report
        sent += len(block)
        now = time.monotonic()
        if now - last_report >= 30:
            speed = (sent - offset) / max(now - started, .01) / 1048576
            print(f"{local.name}: {sent / 1048576:.1f}/{expected / 1048576:.1f} MiB, {speed:.2f} MiB/s", flush=True)
            last_report = now
    if offset < expected:
        with local.open("rb") as stream:
            stream.seek(offset)
            ftp.storbinary("STOR " + temporary, stream, blocksize=65536,
                           callback=progress, rest=offset or None)
    if size(ftp, temporary) != expected:
        raise RuntimeError("Transfer length mismatch; partial file retained for retry")
    previous = size(ftp, remote)
    backup = remote + ".previous-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    if previous is not None:
        ftp.rename(remote, backup)
    try:
        ftp.rename(temporary, remote)
    except Exception:
        if previous is not None:
            ftp.rename(backup, remote)
        raise
    print(f"Staged {remote}: {expected:,} bytes", flush=True)


def command(host, text):
    # Never open/close an empty Companion connection: its old parser crashes.
    with socket.create_connection((host, 1338), timeout=5) as sock:
        sock.sendall((text + "\n").encode("ascii"))
        sock.settimeout(5)
        print(sock.recv(8192).decode(errors="replace"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("stage-data", "stage-vpk", "update", "logs", "launch"))
    parser.add_argument("--host", default=os.environ.get("PSVITAIP"),
                        help="Vita IP address (or set PSVITAIP)")
    args = parser.parse_args()
    if not args.host:
        parser.error("provide --host or set PSVITAIP")
    if args.action == "launch":
        command(args.host, "launch " + TITLE)
        return
    with connect(args.host) as ftp:
        if args.action == "stage-data":
            mkdir(ftp, REMOTE_DATA)
            for name in ("libgame.so", "game.apk", "main.16.com.turner.bestparkintheuniverse.obb", "assets.idx"):
                upload(ftp, ROOT / "data" / name, REMOTE_DATA + "/" + name, resume=True)
        elif args.action == "stage-vpk":
            upload(ftp, ROOT / "build/best_park_vita.vpk", REMOTE_DATA + "/best_park_vita.vpk")
        elif args.action == "update":
            # Use only while the game is closed and VitaShell FTP is running.
            # Do not issue destroy: it also kills VitaShell and this transfer.
            local = ROOT / "build/eboot.bin"
            remote = "ux0:/app/" + TITLE + "/eboot.bin"
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
            backup = ROOT / "analysis/hardware" / stamp
            backup.mkdir(parents=True)
            with (backup / "eboot-before.bin").open("wb") as out:
                ftp.retrbinary("RETR " + remote, out.write)
            upload(ftp, local, remote)
            actual = hashlib.sha256()
            ftp.retrbinary("RETR " + remote, actual.update)
            if actual.digest() != hashlib.sha256(local.read_bytes()).digest():
                raise RuntimeError("Executable readback mismatch")
            (backup / "deployment.json").write_text(json.dumps({"host":args.host,"title_id":TITLE,"sha256":actual.hexdigest()}, indent=2))
        elif args.action == "logs":
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
            output = ROOT / "analysis/hardware" / stamp
            output.mkdir(parents=True)
            for remote, name in ((REMOTE_DATA + "/loader.log", "loader.log"), ("ux0:/data/vitaGL.log", "vitaGL.log")):
                try:
                    with (output / name).open("wb") as out:
                        ftp.retrbinary("RETR " + remote, out.write)
                    print((output / name).read_text(errors="replace")[-10000:])
                except error_perm as exc:
                    print(f"{name}: {exc}")


if __name__ == "__main__":
    main()
