"""Publish a locally verified release after a successful CI run for the same main commit."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request
import zipfile

from verify_release import verify

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = json.loads((ROOT / "repository.json").read_text())
REPO = REPOSITORY["owner"] + "/" + REPOSITORY["name"]
SOURCE_HASH = "0bcc0f2b0914f89af68200fd811e8c96b9f28fcbdac8ff034bdf02a81fd13a0c"
HOSTS = {"api.github.com", "uploads.github.com", "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"}


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()


def sha(path):
    with Path(path).open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def credential():
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        return token
    environment = os.environ | {"GIT_TERMINAL_PROMPT": "0", "GCM_INTERACTIVE": "Never"}
    result = subprocess.run(["git", "-C", str(ROOT), "credential", "fill"],
                            input="protocol=https\nhost=github.com\n\n", capture_output=True,
                            text=True, env=environment, timeout=30)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if result.returncode or not values.get("password"):
        raise RuntimeError("GitHub authentication is unavailable; use GH_TOKEN or the existing Git credential manager")
    return values["password"]


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, newurl):
        destination = urllib.parse.urlparse(newurl)
        if destination.scheme != "https" or destination.hostname not in HOSTS:
            raise RuntimeError("Unexpected GitHub redirect")
        redirected = super().redirect_request(request, response, code, message, headers, newurl)
        if destination.hostname != urllib.parse.urlparse(request.full_url).hostname:
            redirected.remove_header("Authorization")
        return redirected


class GitHub:
    def __init__(self):
        self.token = credential()
        self.opener = urllib.request.build_opener(SafeRedirect())

    def request(self, path, method="GET", data=None, content_type="application/json", missing=False):
        url = path if path.startswith("https://") else "https://api.github.com" + path
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.hostname not in {"api.github.com", "uploads.github.com"}:
            raise RuntimeError("Unexpected authenticated GitHub origin")
        headers = {"Authorization": "Bearer " + self.token, "Accept": "application/vnd.github+json",
                   "X-GitHub-Api-Version": "2026-03-10", "User-Agent": "QIPReleasePublisher/1.1.1"}
        if data is not None:
            headers["Content-Type"] = content_type
            if not isinstance(data, bytes):
                data = json.dumps(data, ensure_ascii=False).encode()
        try:
            with self.opener.open(urllib.request.Request(url, data=data, method=method, headers=headers), timeout=45) as response:
                body = response.read()
                return json.loads(body) if body else None
        except urllib.error.HTTPError as error:
            if missing and error.code == 404:
                return None
            raise RuntimeError(f"GitHub request failed: HTTP {error.code}") from None

    def asset_digest(self, asset):
        digest = asset.get("digest")
        if digest and digest.startswith("sha256:"):
            return digest[7:]
        request = urllib.request.Request(asset["url"], headers={
            "Authorization": "Bearer " + self.token, "Accept": "application/octet-stream",
            "User-Agent": "QIPReleasePublisher/1.1.1",
        })
        value = hashlib.sha256()
        total = 0
        with self.opener.open(request, timeout=45) as response:
            while block := response.read(65536):
                total += len(block)
                if total > asset["size"]:
                    raise RuntimeError("Unexpected asset size")
                value.update(block)
        if total != asset["size"]:
            raise RuntimeError("Truncated asset")
        return value.hexdigest()


def prepare(directory):
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise RuntimeError("Stable version required")
    rights = json.loads((ROOT / "docs/redistribution.json").read_text())
    if not rights["qip_redistribution_approved"] or not (ROOT / rights["evidence"]).is_file():
        raise RuntimeError("Owner redistribution confirmation is missing")
    verify(directory)
    source = directory / "QIP2005_clean.7z"
    if sha(source) != SOURCE_HASH:
        raise RuntimeError("Source package was modified")
    archive = directory / f"qip2005-jimm-source-{version}.zip"
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            path = Path(name)
            if any(part.lower() in {"users", "history", "private", ".git", "backups"} for part in path.parts):
                raise RuntimeError("Private data in source archive")
            if path.suffix.lower() in {".exe", ".dll", ".7z", ".pfx", ".dpapi", ".log"}:
                raise RuntimeError("Unexpected source archive content")
    if git("status", "--porcelain"):
        raise RuntimeError("Commit the verified source tree before publishing")
    return version, [
        directory / f"QIP2005-Jimm-Setup-{version}.exe", directory / "SHA256SUMS.txt",
        directory / "update-manifest.json", directory / "update-manifest.sig", source, archive,
    ]


def publish(api, directory):
    version, files = prepare(directory)
    head = git("rev-parse", "HEAD")
    main = api.request(f"/repos/{REPO}/commits/main")
    if main["sha"] != head:
        raise RuntimeError("The verified commit is not current main")
    query = urllib.parse.urlencode({"head_sha": head, "per_page": 100})
    runs = api.request(f"/repos/{REPO}/actions/runs?{query}")["workflow_runs"]
    verified = [run for run in runs if run["path"] == ".github/workflows/ci.yml"
                and run["head_sha"] == head and run["status"] == "completed" and run["conclusion"] == "success"]
    if not verified:
        raise RuntimeError("Successful source CI for this main commit is required")
    tag = "v" + version
    release = api.request(f"/repos/{REPO}/releases/tags/{tag}", missing=True)
    if release and not release["draft"]:
        raise RuntimeError("Stable release already exists; it will not be overwritten")
    notes = (ROOT / "docs/RELEASE_NOTES.md").read_text(encoding="utf-8-sig")
    if release is None:
        release = api.request(f"/repos/{REPO}/releases", "POST", {
            "tag_name": tag, "target_commitish": head, "name": "QIP 2005 Jimm " + version,
            "body": notes, "draft": True, "prerelease": False,
        })
        print("Draft release created; uploading verified assets.", flush=True)
    if release["target_commitish"] not in {head, "main"}:
        raise RuntimeError("Draft release belongs to another commit")
    uploaded = {asset["name"]: asset for asset in release["assets"]}
    upload = release["upload_url"].split("{", 1)[0]
    for file in files:
        asset = uploaded.get(file.name)
        if asset is None:
            url = upload + "?" + urllib.parse.urlencode({"name": file.name})
            asset = api.request(url, "POST", file.read_bytes(), "application/octet-stream")
        if asset["size"] != file.stat().st_size or api.asset_digest(asset) != sha(file):
            raise RuntimeError("Uploaded asset failed integrity verification")
        print("Verified uploaded asset: " + file.name, flush=True)
    release = api.request(f"/repos/{REPO}/releases/{release['id']}", "PATCH", {
        "draft": False, "prerelease": False, "make_latest": "true", "body": notes,
    })
    api.request(f"/repos/{REPO}", "PATCH", {
        "description": "QIP 2005 для Jimm.online — установщик для Windows 11.",
        "homepage": f"https://github.com/{REPO}/releases/latest",
    })
    result = {"url": release["html_url"], "tag": tag, "commit": head,
              "ci_url": verified[0]["html_url"], "assets": [file.name for file in files]}
    (directory / "publication.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "dist")
    parser.add_argument("--check-auth", action="store_true")
    parser.add_argument("--publish", action="store_true")
    parser.add_argument("--dispatch-ci", action="store_true")
    args = parser.parse_args()
    try:
        api = GitHub()
        if args.check_auth:
            repository = api.request(f"/repos/{REPO}")
            if not repository.get("permissions", {}).get("push"):
                raise RuntimeError("GitHub repository write permission is missing")
            print(json.dumps({"repository": REPO, "authenticated": True, "can_publish": True}))
        elif args.publish:
            publish(api, args.directory.resolve())
        elif args.dispatch_ci:
            api.request(f"/repos/{REPO}/actions/workflows/ci.yml/dispatches", "POST", {"ref": "main"})
            print("Full CI dispatch requested.")
        else:
            raise RuntimeError("Choose --check-auth, --publish or --dispatch-ci")
    except Exception as error:
        if isinstance(error, (RuntimeError, AssertionError)):
            print(str(error) or "Release verification failed.")
        else:
            print("Release operation failed; no authentication data is logged.")
        raise SystemExit(1)
