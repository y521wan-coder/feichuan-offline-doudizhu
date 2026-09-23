import argparse
import hashlib
import json
import subprocess
import tempfile
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

PRODUCT_NAME = "飞船斗地主"
PRODUCT_KEY = "feichuan_offline_doudizhu"
PLATFORM = "windows"
CHANNEL = "stable"
REMOTE_ROOT = "/opt/update-server"


def run(command, *, capture=False):
    result = subprocess.run(command, check=False, text=True, encoding="utf-8", errors="replace", capture_output=capture)
    if result.returncode != 0:
        detail = result.stderr.strip() if capture else ""
        raise RuntimeError(f"命令失败（{result.returncode}）：{' '.join(command)}\n{detail}")
    return result.stdout.strip() if capture else ""


def check_api(version, manifest):
    endpoint = "https://update.327802521.xyz/api/v1/updates/check"
    def request(current_version):
        query = urllib.parse.urlencode({
            "product_key": PRODUCT_KEY,
            "platform": PLATFORM,
            "channel": CHANNEL,
            "current_version": current_version,
        })
        with urllib.request.urlopen(f"{endpoint}?{query}", timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))

    old_result = request("0.9")
    current_result = request(version)
    data = old_result.get("data") or {}
    if old_result.get("code") != 0 or not data.get("update_available") or data.get("latest_version") != version:
        raise RuntimeError("旧版本更新检查没有返回预期的新版本。")
    for key in ("sha256", "signature", "signing_key_id"):
        if data.get(key) != manifest[key]:
            raise RuntimeError(f"服务器 API 字段 {key} 与本地发布清单不一致。")
    if int(data.get("file_size") or 0) != int(manifest["file_size"]):
        raise RuntimeError("服务器 API 文件大小与本地发布清单不一致。")
    if current_result.get("code") != 0 or (current_result.get("data") or {}).get("update_available"):
        raise RuntimeError("当前版本应当显示无更新。")
    return data["download_url"]


def main():
    root = Path(__file__).resolve().parent.parent
    version = (root / "version.txt").read_text(encoding="utf-8-sig").strip()
    parser = argparse.ArgumentParser(description="安全上传并发布飞船斗地主 stable 安装包")
    parser.add_argument("--ssh-key", required=True)
    parser.add_argument("--remote-host", default="root@154.23.163.68")
    parser.add_argument("--manifest", default=str(root / "releases" / f"{PRODUCT_NAME}-Setup-{version}.release-manifest.json"))
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    ssh_key = Path(args.ssh_key).resolve()
    manifest_path = Path(args.manifest).resolve()
    if not ssh_key.is_file() or not manifest_path.is_file():
        raise SystemExit("SSH 私钥或发布清单不存在。")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    expected_name = f"{PRODUCT_NAME}-Setup-{version}.exe"
    installer = manifest_path.parent / expected_name
    expected = {
        "product_name": PRODUCT_NAME,
        "product_key": PRODUCT_KEY,
        "platform": PLATFORM,
        "channel": CHANNEL,
        "version": version,
        "package_filename": expected_name,
        "signature_algorithm": "rsa-pkcs1-sha256",
        "signature_payload_version": 1,
    }
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise SystemExit(f"发布清单字段 {key} 不正确。")
    if not installer.is_file():
        raise SystemExit(f"缺少安装包：{installer}")
    digest = hashlib.sha256(installer.read_bytes()).hexdigest()
    if digest != manifest.get("sha256") or installer.stat().st_size != int(manifest.get("file_size") or 0):
        raise SystemExit("安装包大小或 SHA-256 与发布清单不一致。")
    print(f"VALIDATED={installer}")
    print(f"SHA256={digest}")
    if args.validate_only:
        return

    staging_id = uuid.uuid4().hex
    remote_stage = f"{REMOTE_ROOT}/deploy/packages_uploaded/.staging/{staging_id}"
    ssh_base = ["ssh", "-i", str(ssh_key), args.remote_host]
    try:
        run(ssh_base + [f"install -d -- '{remote_stage}'"])
        run(["scp", "-i", str(ssh_key), str(installer), f"{args.remote_host}:{remote_stage}/{expected_name}"])
        run(["scp", "-i", str(ssh_key), str(manifest_path), f"{args.remote_host}:{remote_stage}/release-manifest.json"])
        output = run(ssh_base + [
            f"cd {REMOTE_ROOT} && docker compose -f deploy/docker-compose.yml exec -T web "
            f"python manage.py publish_signed_release --staging-id {staging_id}"
        ], capture=True)
        print(output)
        download_url = check_api(version, manifest)
        with tempfile.TemporaryDirectory(prefix="fpdz-publish-check-") as directory:
            downloaded = Path(directory) / expected_name
            with urllib.request.urlopen(download_url, timeout=300) as response, downloaded.open("wb") as target:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    target.write(chunk)
            if downloaded.stat().st_size != installer.stat().st_size or hashlib.sha256(downloaded.read_bytes()).hexdigest() != digest:
                raise RuntimeError("服务器下载文件与本地安装包不一致。")
        print(f"PUBLISHED_VERSION={version}")
        print("SERVER_VERIFICATION=OK")
    finally:
        subprocess.run(ssh_base + [
            f"cd {REMOTE_ROOT} && docker compose -f deploy/docker-compose.yml exec -T web "
            f"python manage.py cleanup_release_staging --staging-id {staging_id}"
        ], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
